#!/usr/bin/env python3
"""
ESP32 Key Tool - Direct interaction with ESP32 SSH Agent

Usage:
    python3 esp32_keytool.py --port /dev/ttyUSB0 --generate
    python3 esp32_keytool.py --port /dev/ttyUSB0 --generate --password "mysecret"
    python3 esp32_keytool.py --port /dev/ttyUSB0 --get-key
    python3 esp32_keytool.py --port /dev/ttyUSB0 --unlock --password "mysecret"
    python3 esp32_keytool.py --port /dev/ttyUSB0 --lock
    python3 esp32_keytool.py --port /dev/ttyUSB0 --sign <hex-challenge>
"""

import os
import sys
import serial
import time
import argparse
import struct
import base64

PROTOCOL_SIGN_CHALLENGE = 0x01
PROTOCOL_GET_PUBLIC_KEY = 0x02
PROTOCOL_UNLOCK = 0x03
PROTOCOL_LOCK = 0x04
PROTOCOL_GENERATE_ENCRYPTED = 0x05

SSH_AGENT_FAILURE = 5
SSH_AGENT_SUCCESS = 6
SSH_AGENT_SIGN_RESPONSE = 14
SSH_AGENT_LOCKED = 0x20


def send_packet(ser, msg_type, data):
    length = len(data)
    header = bytes([msg_type]) + struct.pack(">I", length)[1:4]
    ser.write(header + data)
    ser.flush()


def read_packet(ser, timeout=10.0):
    ser.timeout = timeout

    header = ser.read(4)
    if len(header) < 4:
        return None, None

    length = struct.unpack(">I", header)[0]

    if length < 1 or length > 4096:
        return None, None

    data = ser.read(length)
    if len(data) < length:
        return None, None

    msg_type = data[0]
    payload = data[1:]
    return msg_type, payload


def generate_key(port, password=None, baudrate=115200):
    """Generate key on ESP32, optionally encrypted with password"""
    try:
        ser = serial.Serial(port, baudrate, timeout=2.0)
        time.sleep(2)
        ser.reset_input_buffer()

        if password:
            print("Generating encrypted key on ESP32...")
            send_packet(ser, PROTOCOL_GENERATE_ENCRYPTED, password.encode('utf-8'))
            msg_type, data = read_packet(ser, timeout=30.0)
            ser.close()

            if msg_type == SSH_AGENT_SUCCESS:
                print("[+] Encrypted key generated successfully")
                return True
            else:
                print(f"[-] Key generation failed: type={msg_type}")
                return False
        else:
            print("Sending keygen command via serial...")

            timeout = time.time() + 30
            while time.time() < timeout:
                line = ser.readline().decode('utf-8', errors='ignore').strip()
                if 'Key generation successful' in line:
                    print("[+] Key generated successfully")
                    ser.close()
                    return True
                elif 'Ed25519 key generated' in line:
                    print("[+] Key generation started")

            ser.close()
            return True

    except Exception as e:
        print(f"[!] Error: {e}")
        return False


def get_public_key(port, baudrate=115200):
    """Get public key from ESP32"""
    try:
        ser = serial.Serial(port, baudrate, timeout=5.0)
        time.sleep(2)
        ser.reset_input_buffer()

        send_packet(ser, PROTOCOL_GET_PUBLIC_KEY, b"")

        msg_type, data = read_packet(ser)

        ser.close()

        if msg_type == PROTOCOL_GET_PUBLIC_KEY and data and len(data) >= 32:
            pubkey = data[:32]
            key_type = b"ssh-ed25519"
            blob = struct.pack(">I", len(key_type)) + key_type + struct.pack(">I", len(pubkey)) + pubkey
            ssh_key = "ssh-ed25519 " + base64.b64encode(blob).decode()
            print(ssh_key)
            return pubkey
        else:
            print(f"[!] Failed to get public key: type={msg_type}, len={len(data) if data else 0}")
            return None

    except Exception as e:
        print(f"[!] Error: {e}")
        return None


def unlock_agent(port, password, baudrate=115200):
    """Unlock the ESP32 agent with password"""
    try:
        ser = serial.Serial(port, baudrate, timeout=5.0)
        time.sleep(2)
        ser.reset_input_buffer()

        print("Sending unlock command...")
        send_packet(ser, PROTOCOL_UNLOCK, password.encode('utf-8'))

        msg_type, data = read_packet(ser, timeout=30.0)
        ser.close()

        if msg_type == SSH_AGENT_SUCCESS:
            print("[+] Agent unlocked successfully")
            return True
        else:
            print("[-] Unlock failed (wrong password?)")
            return False

    except Exception as e:
        print(f"[!] Error: {e}")
        return False


def lock_agent(port, baudrate=115200):
    """Lock the ESP32 agent"""
    try:
        ser = serial.Serial(port, baudrate, timeout=5.0)
        time.sleep(2)
        ser.reset_input_buffer()

        print("Sending lock command...")
        send_packet(ser, PROTOCOL_LOCK, b"")

        msg_type, data = read_packet(ser)
        ser.close()

        if msg_type == SSH_AGENT_SUCCESS:
            print("[+] Agent locked")
            return True
        else:
            print("[-] Lock failed")
            return False

    except Exception as e:
        print(f"[!] Error: {e}")
        return False


def sign_challenge(port, challenge_hex, baudrate=115200):
    """Sign a challenge with ESP32"""
    try:
        challenge = bytes.fromhex(challenge_hex)
    except ValueError:
        print("[!] Invalid hex challenge")
        return None

    try:
        ser = serial.Serial(port, baudrate, timeout=30.0)
        time.sleep(2)
        ser.reset_input_buffer()

        send_packet(ser, PROTOCOL_SIGN_CHALLENGE, challenge)

        msg_type, data = read_packet(ser, timeout=30.0)

        ser.close()

        if msg_type == SSH_AGENT_SIGN_RESPONSE and data and len(data) >= 64:
            signature = data[:64]
            print(f"[+] Signature: {signature.hex()}")
            return signature
        elif msg_type == SSH_AGENT_LOCKED:
            print("[!] Agent is locked. Unlock it first with --unlock")
            return None
        elif msg_type == SSH_AGENT_FAILURE:
            print("[!] ESP32 returned failure")
            return None
        else:
            print(f"[!] Unexpected response: type={msg_type}, len={len(data) if data else 0}")
            return None

    except Exception as e:
        print(f"[!] Error: {e}")
        return None


def main():
    parser = argparse.ArgumentParser(description="ESP32 Key Tool")
    parser.add_argument("--port", "-p", default="/dev/ttyUSB0", help="Serial port")
    parser.add_argument("--baudrate", "-b", type=int, default=115200, help="Baud rate")
    parser.add_argument("--generate", "-g", action="store_true", help="Generate new key on ESP32")
    parser.add_argument("--get-key", "-k", action="store_true", help="Get public key from ESP32")
    parser.add_argument("--sign", "-s", type=str, help="Sign hex challenge with ESP32")
    parser.add_argument("--password", "-P", type=str, help="Password for encrypted key generation/unlock")
    parser.add_argument("--unlock", "-u", action="store_true", help="Unlock the agent")
    parser.add_argument("--lock", "-l", action="store_true", help="Lock the agent")

    args = parser.parse_args()

    if not args.generate and not args.get_key and not args.sign and not args.unlock and not args.lock:
        parser.print_help()
        sys.exit(1)

    if args.generate:
        if generate_key(args.port, args.password, args.baudrate):
            print("[+] Key generation complete")
            sys.exit(0)
        else:
            print("[-] Key generation failed")
            sys.exit(1)

    if args.unlock:
        if not args.password:
            print("[!] --password is required for --unlock")
            sys.exit(1)
        if unlock_agent(args.port, args.password, args.baudrate):
            sys.exit(0)
        else:
            sys.exit(1)

    if args.lock:
        if lock_agent(args.port, args.baudrate):
            sys.exit(0)
        else:
            sys.exit(1)

    if args.get_key:
        key = get_public_key(args.port, args.baudrate)
        if key:
            sys.exit(0)
        else:
            sys.exit(1)

    if args.sign:
        sig = sign_challenge(args.port, args.sign, args.baudrate)
        if sig:
            sys.exit(0)
        else:
            sys.exit(1)


if __name__ == "__main__":
    main()
