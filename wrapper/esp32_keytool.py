#!/usr/bin/env python3
"""
ESP32 Key Tool - Direct interaction with ESP32 SSH Agent

Usage:
    python3 esp32_keytool.py --port /dev/ttyUSB0 --generate
    python3 esp32_keytool.py --port /dev/ttyUSB0 --get-key
    python3 esp32_keytool.py --port /dev/ttyUSB0 --sign <hex-challenge>
"""

import os
import sys
import serial
import time
import argparse
import struct

PROTOCOL_SIGN_CHALLENGE = 0x01
PROTOCOL_GET_PUBLIC_KEY = 0x02

SSH_AGENT_FAILURE = 5
SSH_AGENT_SUCCESS = 6
SSH_AGENT_SIGN_RESPONSE = 14


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
        
    msg_type = header[0]
    length = struct.unpack(">I", b'\x00' + header[1:4])[0]
    
    if length > 4096:
        return None, None
        
    data = ser.read(length)
    if len(data) < length:
        return None, None
        
    return msg_type, data


def generate_key(port, baudrate=115200):
    """Trigger key generation on ESP32"""
    try:
        ser = serial.Serial(port, baudrate, timeout=2.0)
        time.sleep(2)
        
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
        
        send_packet(ser, PROTOCOL_GET_PUBLIC_KEY, b"")
        
        msg_type, data = read_packet(ser)
        
        ser.close()
        
        if msg_type == PROTOCOL_GET_PUBLIC_KEY and data and len(data) >= 32:
            pubkey = data[:32]
            print("ssh-ed25519 " + pubkey.hex())
            return pubkey
        else:
            print(f"[!] Failed to get public key: type={msg_type}, len={len(data) if data else 0}")
            return None
            
    except Exception as e:
        print(f"[!] Error: {e}")
        return None


def sign_challenge(port, challenge_hex, baudrate=115200):
    """Sign a challenge with ESP32"""
    try:
        challenge = bytes.fromhex(challenge_hex)
    except ValueError:
        print("[!] Invalid hex challenge")
        return None
        
    try:
        ser = serial.Serial(port, baudrate, timeout=30.0)
        time.sleep(0.5)
        
        send_packet(ser, PROTOCOL_SIGN_CHALLENGE, challenge)
        
        msg_type, data = read_packet(ser, timeout=30.0)
        
        ser.close()
        
        if msg_type == SSH_AGENT_SIGN_RESPONSE and data and len(data) >= 64:
            signature = data[:64]
            print(f"[+] Signature: {signature.hex()}")
            return signature
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
    
    args = parser.parse_args()
    
    if not args.generate and not args.get_key and not args.sign:
        parser.print_help()
        sys.exit(1)
    
    if args.generate:
        if generate_key(args.port, args.baudrate):
            print("[+] Key generation complete")
            sys.exit(0)
        else:
            print("[-] Key generation failed")
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
