#!/usr/bin/env python3

import os
import sys
import socket
import struct
import threading
import serial
import time
import signal
import argparse
from typing import Optional, Tuple

SSH_AGENTC_REQUEST_IDENTITIES = 11
SSH_AGENTC_SIGN_REQUEST = 13
SSH_AGENTC_ADD_IDENTITY = 17
SSH_AGENTC_REMOVE_IDENTITY = 18
SSH_AGENTC_REMOVE_ALL_IDENTITIES = 19

SSH_AGENT_FAILURE = 5
SSH_AGENT_SUCCESS = 6
SSH_AGENT_IDENTITIES_ANSWER = 12
SSH_AGENT_SIGN_RESPONSE = 14

SSH_AGENTC_EXTENSION = 27

PROTOCOL_UNLOCK = 0x03
PROTOCOL_LOCK = 0x04
SSH_AGENT_LOCKED = 0x20


class ESP32AgentBridge:
    def __init__(self, serial_port: str, socket_path: str = "/tmp/esp32-agent.sock", baudrate: int = 115200):
        self.serial_port = serial_port
        self.socket_path = socket_path
        self.baudrate = baudrate
        self.serial: Optional[serial.Serial] = None
        self.serial_lock = threading.Lock()
        self.running = True

    def connect_serial(self) -> bool:
        try:
            self.serial = serial.Serial(
                port=self.serial_port,
                baudrate=self.baudrate,
                bytesize=serial.EIGHTBITS,
                parity=serial.PARITY_NONE,
                stopbits=serial.STOPBITS_ONE,
                timeout=5.0
            )
            time.sleep(2)
            # drain any boot messages from ESP32
            self.serial.reset_input_buffer()
            print(f"[*] Connected to ESP32 on {self.serial_port}")
            return True
        except Exception as e:
            print(f"[!] Failed to connect to serial: {e}")
            return False

    def _serial_read_exact(self, n: int, timeout: float = 10.0) -> Optional[bytes]:
        if not self.serial:
            return None
        old_timeout = self.serial.timeout
        self.serial.timeout = timeout
        data = self.serial.read(n)
        self.serial.timeout = old_timeout
        if len(data) < n:
            return None
        return data

    def send_to_esp32(self, msg_type: int, data: bytes) -> bool:
        if not self.serial:
            return False
        try:
            length = len(data)
            header = bytes([msg_type, (length >> 16) & 0xFF, (length >> 8) & 0xFF, length & 0xFF])
            self.serial.write(header + data)
            self.serial.flush()
            return True
        except Exception as e:
            print(f"[!] Error sending to ESP32: {e}")
            return False

    def recv_from_esp32(self, timeout: float = 10.0) -> Optional[Tuple[int, bytes]]:
        try:
            deadline = time.time() + timeout
            header = b""
            old_timeout = self.serial.timeout if self.serial else None

            if not self.serial:
                return None

            self.serial.timeout = timeout

            # Skip debug text until we find 0x00 (start of length header)
            while time.time() < deadline:
                b = self.serial.read(1)
                if len(b) == 0:
                    continue
                if len(header) == 0:
                    if b[0] == 0x00:
                        header = b
                else:
                    header += b
                    if len(header) == 4:
                        break

            if len(header) < 4:
                if old_timeout is not None:
                    self.serial.timeout = old_timeout
                return None

            length = struct.unpack(">I", header)[0]
            if length < 1 or length > 4096:
                print(f"[!] Bad packet length from ESP32: {length}")
                if old_timeout is not None:
                    self.serial.timeout = old_timeout
                return None

            remaining = deadline - time.time()
            if remaining > 0:
                self.serial.timeout = remaining
            data = self.serial.read(length)

            if old_timeout is not None:
                self.serial.timeout = old_timeout

            if len(data) < length:
                return None

            msg_type = data[0]
            payload = data[1:]
            return (msg_type, payload)

        except Exception as e:
            print(f"[!] Error reading from ESP32: {e}")
            return None

    def _ssh_string(self, data: bytes) -> bytes:
        return struct.pack(">I", len(data)) + data

    def _build_ed25519_key_blob(self, raw_pubkey: bytes) -> bytes:
        return self._ssh_string(b"ssh-ed25519") + self._ssh_string(raw_pubkey)

    def _extract_raw_key(self, ssh_key_blob: bytes) -> bytes:
        offset = 0
        type_len = struct.unpack(">I", ssh_key_blob[offset:offset + 4])[0]
        offset += 4 + type_len  # skip type string
        key_len = struct.unpack(">I", ssh_key_blob[offset:offset + 4])[0]
        offset += 4
        return ssh_key_blob[offset:offset + key_len]

    def _build_ed25519_sig_blob(self, raw_sig: bytes) -> bytes:
        return self._ssh_string(b"ssh-ed25519") + self._ssh_string(raw_sig)

    def unlock(self, password: bytes) -> bytes:
        with self.serial_lock:
            if not self.send_to_esp32(PROTOCOL_UNLOCK, password):
                return self._create_failure()

            result = self.recv_from_esp32(timeout=30.0)
            if not result:
                return self._create_failure()

            msg_type, _ = result
            if msg_type == SSH_AGENT_SUCCESS:
                print("[*] Agent unlocked successfully")
                return self._create_success()

            print("[!] Unlock failed")
            return self._create_failure()

    def lock(self) -> bytes:
        with self.serial_lock:
            if not self.send_to_esp32(PROTOCOL_LOCK, b""):
                return self._create_failure()

            result = self.recv_from_esp32()
            if not result:
                return self._create_failure()

            msg_type, _ = result
            if msg_type == SSH_AGENT_SUCCESS:
                print("[*] Agent locked")
                return self._create_success()
            return self._create_failure()

    def request_identities(self) -> bytes:
        with self.serial_lock:
            if not self.send_to_esp32(SSH_AGENTC_REQUEST_IDENTITIES, b""):
                return self._create_failure()

            result = self.recv_from_esp32()

        if not result:
            print("[!] No response from ESP32 for identity request")
            return self._create_failure()

        msg_type, payload = result
        if msg_type != SSH_AGENT_IDENTITIES_ANSWER:
            print(f"[!] Unexpected response type: {msg_type}")
            return self._create_failure()

        # ESP32 sends: [nkeys(4)][key_blob_len(4)][raw_key(32)][comment_len(4)]
        if len(payload) < 8:
            return self._create_failure()

        nkeys = struct.unpack(">I", payload[0:4])[0]
        if nkeys == 0:
            # no keys, just forward
            resp = bytes([SSH_AGENT_IDENTITIES_ANSWER])
            resp += struct.pack(">I", 0)
            return resp

        # parse raw key from ESP32's response
        raw_key_len = struct.unpack(">I", payload[4:8])[0]
        raw_key = payload[8:8 + raw_key_len]

        # build proper SSH wire format key blob
        key_blob = self._build_ed25519_key_blob(raw_key)
        comment = b"ESP32-SSH-Agent"

        resp = bytes([SSH_AGENT_IDENTITIES_ANSWER])
        resp += struct.pack(">I", 1)  # nkeys
        resp += self._ssh_string(key_blob)
        resp += self._ssh_string(comment)
        return resp

    def sign_request(self, key_blob: bytes, data: bytes, flags: int) -> bytes:
        # extract raw 32-byte key from SSH wire format key blob
        raw_key = self._extract_raw_key(key_blob)

        # build payload for ESP32: [key_len(4)][raw_key][data_len(4)][data][flags(1)]
        payload = self._ssh_string(raw_key) + self._ssh_string(data) + bytes([flags])

        with self.serial_lock:
            if not self.send_to_esp32(SSH_AGENTC_SIGN_REQUEST, payload):
                return self._create_failure()

            result = self.recv_from_esp32(timeout=30.0)

        if not result:
            print("[!] No response from ESP32 for sign request")
            return self._create_failure()

        msg_type, payload = result

        # If locked, pass through to client so it can prompt for password
        if msg_type == SSH_AGENT_LOCKED:
            print("[*] ESP32 reports key is locked")
            return bytes([SSH_AGENT_LOCKED])

        if msg_type != SSH_AGENT_SIGN_RESPONSE:
            print(f"[!] Sign response type: {msg_type}")
            return self._create_failure()

        # ESP32 sends: [sig_len(4)][raw_sig(64)]
        if len(payload) < 4:
            return self._create_failure()

        raw_sig_len = struct.unpack(">I", payload[0:4])[0]
        raw_sig = payload[4:4 + raw_sig_len]

        # build SSH wire format signature blob
        sig_blob = self._build_ed25519_sig_blob(raw_sig)

        resp = bytes([SSH_AGENT_SIGN_RESPONSE])
        resp += self._ssh_string(sig_blob)
        return resp

    def _create_failure(self) -> bytes:
        return bytes([SSH_AGENT_FAILURE])

    def _create_success(self) -> bytes:
        return bytes([SSH_AGENT_SUCCESS])

    def handle_agent_request(self, data: bytes) -> bytes:
        if len(data) < 1:
            return self._create_failure()

        msg_type = data[0]
        payload = data[1:]

        if msg_type == SSH_AGENTC_REQUEST_IDENTITIES:
            return self.request_identities()

        elif msg_type == SSH_AGENTC_SIGN_REQUEST:
            if len(payload) < 8:
                return self._create_failure()

            key_len = struct.unpack(">I", payload[0:4])[0]
            if len(payload) < 4 + key_len + 4:
                return self._create_failure()

            key_blob = payload[4:4 + key_len]
            data_offset = 4 + key_len
            data_len = struct.unpack(">I", payload[data_offset:data_offset + 4])[0]
            sign_data = payload[data_offset + 4:data_offset + 4 + data_len]

            flags_offset = data_offset + 4 + data_len
            flags = 0
            if flags_offset + 4 <= len(payload):
                flags = struct.unpack(">I", payload[flags_offset:flags_offset + 4])[0]

            return self.sign_request(key_blob, sign_data, flags)

        elif msg_type == SSH_AGENTC_EXTENSION:
            # Parse extension name
            if len(payload) < 4:
                return self._create_failure()
            name_len = struct.unpack(">I", payload[0:4])[0]
            if len(payload) < 4 + name_len:
                return self._create_failure()
            ext_name = payload[4:4 + name_len]
            ext_data = payload[4 + name_len:]

            if ext_name == b"esp32-unlock":
                # ext_data = [4-byte len][password bytes]
                if len(ext_data) < 4:
                    return self._create_failure()
                pw_len = struct.unpack(">I", ext_data[0:4])[0]
                password = ext_data[4:4 + pw_len]
                return self.unlock(password)
            elif ext_name == b"esp32-lock":
                return self.lock()
            else:
                return self._create_failure()

        elif msg_type in (SSH_AGENTC_ADD_IDENTITY, SSH_AGENTC_REMOVE_IDENTITY, SSH_AGENTC_REMOVE_ALL_IDENTITIES):
            return self._create_success()

        else:
            print(f"[*] Unknown message type: {msg_type}")
            return self._create_failure()

    def handle_client(self, client_sock: socket.socket):
        try:
            while self.running:
                try:
                    length_data = client_sock.recv(4)
                    if len(length_data) < 4:
                        break

                    length = struct.unpack(">I", length_data)[0]
                    if length > 65536:
                        break

                    data = b""
                    while len(data) < length:
                        chunk = client_sock.recv(length - len(data))
                        if not chunk:
                            break
                        data += chunk

                    if len(data) < length:
                        break

                    response = self.handle_agent_request(data)
                    client_sock.send(struct.pack(">I", len(response)) + response)

                except socket.timeout:
                    continue
                except Exception:
                    break
        finally:
            client_sock.close()

    def start_unix_server(self):
        if os.path.exists(self.socket_path):
            os.unlink(self.socket_path)

        server = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        server.bind(self.socket_path)
        server.listen(5)
        server.settimeout(1.0)

        os.chmod(self.socket_path, 0o600)
        print(f"[*] SSH Agent socket listening on {self.socket_path}")

        while self.running:
            try:
                client_sock, _ = server.accept()
                client_sock.settimeout(30.0)
                threading.Thread(target=self.handle_client, args=(client_sock,), daemon=True).start()
            except socket.timeout:
                continue
            except Exception as e:
                if self.running:
                    print(f"[!] Server error: {e}")

        server.close()
        if os.path.exists(self.socket_path):
            os.unlink(self.socket_path)

    def run(self):
        print(f"[*] ESP32 SSH Agent Bridge")
        print(f"[*] Serial: {self.serial_port} @ {self.baudrate} baud")
        print(f"[*] Socket: {self.socket_path}")

        if not self.connect_serial():
            sys.exit(1)

        signal.signal(signal.SIGINT, lambda s, f: self.shutdown())
        signal.signal(signal.SIGTERM, lambda s, f: self.shutdown())

        self.start_unix_server()

    def shutdown(self):
        print("\n[*] Shutting down...")
        self.running = False
        if self.serial:
            self.serial.close()
        if os.path.exists(self.socket_path):
            os.unlink(self.socket_path)
        sys.exit(0)


def main():
    parser = argparse.ArgumentParser(description="ESP32 SSH Agent Bridge")
    parser.add_argument("serial_port", help="Serial port (e.g., /dev/ttyACM0)")
    parser.add_argument("-s", "--socket", default="/tmp/esp32-agent.sock", help="Unix socket path")
    parser.add_argument("-b", "--baudrate", type=int, default=115200, help="Baud rate")

    args = parser.parse_args()

    bridge = ESP32AgentBridge(args.serial_port, args.socket, args.baudrate)
    bridge.run()


if __name__ == "__main__":
    main()
