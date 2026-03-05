package main

import (
	"flag"
	"fmt"
	"net"
	"os"
)

func main() {
	agentMode := flag.String("agent", "", "Use ESP32 SSH agent (specify socket path, e.g., /tmp/esp32-agent.sock)")
	serialPort := flag.String("serial", "/dev/ttyUSB0", "Serial port for ESP32")

	flag.Parse()

	if len(os.Args) < 6 && *agentMode == "" {
		fmt.Println("Usage:")
		fmt.Println("  client <manager_host> <port> <user_id> <container_id> <private_key.pem>")
		fmt.Println("  client -agent <socket_path> <manager_host> <port> <user_id> <container_id>")
		fmt.Println("")
		fmt.Println("Options:")
		fmt.Println("  -agent string    Use ESP32 SSH agent (socket path)")
		fmt.Println("  -serial string  Serial port for ESP32 (default: /dev/ttyUSB0)")
		os.Exit(1)
	}

	if *agentMode != "" {
		if len(os.Args) < 5 {
			fmt.Println("Usage: client -agent <socket> <manager_host> <port> <user_id> <container_id>")
			os.Exit(1)
		}

		host := os.Args[len(os.Args)-4]
		port := os.Args[len(os.Args)-3]
		userID := os.Args[len(os.Args)-2]
		containerID := os.Args[len(os.Args)-1]

		err := ConnectWithESP32Agent(host, port, userID, containerID, *agentMode)
		if err != nil {
			fmt.Println("[-] ESP32 Agent error:", err)
			RunESP32AgentMode(*serialPort, 115200)
		}
		return
	}

	host := os.Args[1]
	port := os.Args[2]
	userID := os.Args[3]
	containerID := os.Args[4]
	keyPath := os.Args[5]

	priv, err := loadPrivateKey(keyPath)
	if err != nil {
		fmt.Println("[-] Failed to load private key:", err)
		return
	}

	openSSHPriv, _, err := createTempSSHKeypair(priv)
	if err != nil {
		fmt.Println("[-] Failed to convert to OpenSSH:", err)
		return
	}

	fmt.Println("[+] Connecting to manager:", net.JoinHostPort(host, port))

	conn, err := connectToManager(host, port)
	if err != nil {
		fmt.Println("[-] Connection failed:", err)
		return
	}
	defer conn.Close()

	writeLine(conn, []byte(userID))

	challenge, err := readLine(conn)
	if err != nil {
		fmt.Println("[-] Failed to read challenge:", err)
		return
	}
	fmt.Println("[+] Challenge received")

	sig, err := signChallenge(priv, []byte(challenge))
	if err != nil {
		fmt.Println("[-] Failed to sign challenge:", err)
		return
	}

	writeLine(conn, sig)

	reply, err := readLine(conn)
	if err != nil {
		fmt.Println("[-] Failed to read reply:", err)
		return
	}

	if reply != "REQ CID" {
		fmt.Println("[-] Access denied:", reply)
		return
	}

	writeLine(conn, []byte(containerID))

	reply, err = readLine(conn)
	if err != nil {
		fmt.Println("[-] Failed to read reply:", err)
		return
	}
	if reply != "OK" {
		fmt.Println("[-] Received Error:", reply)
		return
	}

	sshInfo, err := readLine(conn)
	if err != nil {
		fmt.Println("[-] Failed to read SSH info:", err)
		return
	}

	var sshPort int
	_, _ = fmt.Sscanf(sshInfo, "%d", &sshPort)

	fmt.Printf("[+] Container unlocked: SSH at %s:%d\n", host, sshPort)
	fmt.Println("[+] Authentication successful")

	conn.Close()

	startSSH(host, sshPort, userID, openSSHPriv)
}
