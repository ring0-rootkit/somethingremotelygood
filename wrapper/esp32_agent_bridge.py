#!/usr/bin/env python3
"""
ESP32 SSH Agent Bridge

Bridges SSH Agent protocol to ESP32 over serial (USB-to-UART).
Works as a drop-in replacement for ssh-agent, usable with SSH_AUTH_SOCK.

Usage:
    python esp32_agent_bridge.py /dev/ttyUSB0
    SSH_AUTH_SOCK=/tmp/esp32-agent.sock ssh user@host
"""

import os
import sys
import socket
import struct
import threading
import serial
import time
import signal
import argparse
import base64
import hashlib
from typing import Optional, Tuple

SSH_AGENTC_REQUEST_IDENTITIES = 11
SSH_AGENTC_SIGN_REQUEST = 13
SSH_AGENTC_ADD_IDENTITY = 17
SSH_AGENTC_REMOVE_IDENTITY = 18
SSH_AGENTC_REMOVE_ALL_IDENTITIES = 19
SSH_AGENTC_ADD_ID_CONSTRAINED = 25
SSH_AGENTC_EXTENSION = 27

SSH_AGENT_FAILURE = 5
SSH_AGENT_SUCCESS = 6
SSH_AGENT_IDENTITIES_ANSWER = 12
SSH_AGENT_SIGN_RESPONSE = 14
SSH_AGENT_EXTENSION = 27

SSH_AGENT_RSA_SHA2_256 = 2
SSH_AGENT_RSA_SHA2_512 = 4


class ESP32AgentBridge:
    def __init__(self, serial_port: str, socket_path: str = "/tmp/esp32-agent.sock", baudrate: int = 115200):
        self.serial_port = serial_port
        self.socket_path = socket_path
        self.baudrate = baudrate
        self.serial: Optional[serial.Serial] = None
        self.running = True
        self.client_sockets = []
        
    def connect_serial(self) -> bool:
        """Connect to ESP32 over serial"""
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
            print(f"[*] Connected to ESP32 on {self.serial_port}")
            return True
        except Exception as e:
            print(f"[!] Failed to connect to serial: {e}")
            return False

    def read_packet(self, timeout: float = 10.0) -> Optional[Tuple[int, bytes]]:
        """Read a packet from ESP32"""
        if not self.serial:
            return None
            
        try:
            self.serial.timeout = timeout
            
            header = self.serial.read(4)
            if len(header) < 4:
                return None
                
            msg_type = header[0]
            length = struct.unpack(">I", header[1:4] + b'\x00')[0] >> 8
            
            if length > 4096:
                return None
                
            data = self.serial.read(length)
            if len(data) < length:
                return None
                
            return (msg_type, data)
            
        except Exception as e:
            print(f"[!] Error reading packet: {e}")
            return None

    def send_packet(self, msg_type: int, data: bytes) -> bool:
        """Send a packet to ESP32"""
        if not self.serial:
            return False
            
        try:
            length = len(data)
            header = bytes([msg_type]) + struct.pack(">I", length)[1:4]
            self.serial.write(header + data)
            self.serial.flush()
            return True
        except Exception as e:
            print(f"[!] Error sending packet: {e}")
            return False

    def request_identities(self) -> bytes:
        """Request identities from ESP32"""
        if not self.send_packet(SSH_AGENTC_REQUEST_IDENTITIES, b""):
            return self._create_failure()
            
        result = self.read_packet()
        if not result:
            return self._create_failure()
            
        return bytes([SSH_AGENT_IDENTITIES_ANSWER]) + result[1]
        
    def sign_request(self, key_blob: bytes, data: bytes, flags: int) -> bytes:
        """Send sign request to ESP32"""
        payload = struct.pack(">I", len(key_blob)) + key_blob + struct.pack(">I", len(data)) + data + bytes([flags])
        
        if not self.send_packet(SSH_AGENTC_SIGN_REQUEST, payload):
            return self._create_failure()
            
        result = self.read_packet(timeout=30.0)
        if not result:
            return self._create_failure()
            
        return bytes([SSH_AGENT_SIGN_RESPONSE]) + struct.pack(">I", len(result[1]))[1:4] + result[1]

    def _create_failure(self) -> bytes:
        """Create failure response"""
        return bytes([SSH_AGENT_FAILURE])
    
    def _create_success(self) -> bytes:
        """Create success response"""
        return bytes([SSH_AGENT_SUCCESS])

    def handle_agent_request(self, data: bytes) -> bytes:
        """Handle incoming SSH agent request"""
        if len(data) < 1:
            return self._create_failure()
            
        msg_type = data[0]
        payload = data[1:]
        
        if msg_type == SSH_AGENTC_REQUEST_IDENTITIES:
            return self.request_identities()
            
        elif msg_type == SSH_AGENTC_SIGN_REQUEST:
            if len(payload) < 36:
                return self._create_failure()
                
            key_len = struct.unpack(">I", payload[0:4])[0]
            if len(payload) < 4 + key_len + 4 + 1:
                return self._create_failure()
                
            key_blob = payload[4:4+key_len]
            data_len = struct.unpack(">I", payload[4+key_len:8+key_len])[0]
            sign_data = payload[8+key_len:8+key_len+data_len]
            flags = payload[8+key_len+data_len]
            
            return self.sign_request(key_blob, sign_data, flags)
            
        elif msg_type == SSH_AGENTC_ADD_IDENTITY:
            return self._create_success()
            
        elif msg_type == SSH_AGENTC_REMOVE_IDENTITY:
            return self._create_success()
            
        elif msg_type == SSH_AGENTC_REMOVE_ALL_IDENTITIES:
            return self._create_success()
            
        else:
            print(f"[*] Unknown message type: {msg_type}")
            return self._create_failure()

    def handle_client(self, client_sock: socket.socket):
        """Handle a single SSH client connection"""
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
                    
                    response = self.handle_agent_request(data)
                    
                    client_sock.send(struct.pack(">I", len(response)) + response)
                    
                except socket.timeout:
                    continue
                except Exception as e:
                    break
                    
        except Exception as e:
            print(f"[!] Client handler error: {e}")
        finally:
            client_sock.close()

    def start_unix_server(self):
        """Start Unix socket server for SSH agent"""
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
        """Run the bridge"""
        print(f"[*] ESP32 SSH Agent Bridge")
        print(f"[*] Serial: {self.serial_port} @ {self.baudrate} baud")
        print(f"[*] Socket: {self.socket_path}")
        
        if not self.connect_serial():
            sys.exit(1)
            
        signal.signal(signal.SIGINT, lambda s, f: self.shutdown())
        signal.signal(signal.SIGTERM, lambda s, f: self.shutdown())
        
        self.start_unix_server()
        
    def shutdown(self):
        """Shutdown the bridge"""
        print("\n[*] Shutting down...")
        self.running = False
        if self.serial:
            self.serial.close()
        if os.path.exists(self.socket_path):
            os.unlink(self.socket_path)
        sys.exit(0)


def get_public_key(serial_port: str, baudrate: int = 115200) -> Optional[str]:
    """Get public key from ESP32"""
    try:
        ser = serial.Serial(serial_port, baudrate, timeout=2.0)
        time.sleep(2)
        
        timeout = time.time() + 5
        while time.time() < timeout:
            line = ser.readline().decode('utf-8', errors='ignore').strip()
            if 'ssh-ed25519' in line or len(line) == 64:
                ser.close()
                return line
                
        ser.close()
    except Exception as e:
        print(f"[!] Error getting public key: {e}")
    return None


def main():
    parser = argparse.ArgumentParser(description="ESP32 SSH Agent Bridge")
    parser.add_argument("serial_port", help="Serial port (e.g., /dev/ttyUSB0)")
    parser.add_argument("-s", "--socket", default="/tmp/esp32-agent.sock", help="Unix socket path")
    parser.add_argument("-b", "--baudrate", type=int, default=115200, help="Baud rate")
    parser.add_argument("-g", "--get-key", action="store_true", help="Just get public key and exit")
    
    args = parser.parse_args()
    
    if args.get_key:
        key = get_public_key(args.serial_port, args.baudrate)
        if key:
            print(f"Public key: {key}")
            sys.exit(0)
        else:
            print("Failed to get public key")
            sys.exit(1)
            
    bridge = ESP32AgentBridge(args.serial_port, args.socket, args.baudrate)
    bridge.run()


if __name__ == "__main__":
    main()
