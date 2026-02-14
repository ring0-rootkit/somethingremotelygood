package main

import (
	"fmt"
	"net"
	"os"
)

func main() {
	if len(os.Args) < 6 {
		fmt.Println("Usage:")
		fmt.Println("  client <manager_host> <port> <user_id> <container_id> <private_key.pem>")
		os.Exit(1)
	}

	host := os.Args[1]
	port := os.Args[2]
	userID := os.Args[3]
	containerID := os.Args[4]
	keyPath := os.Args[5]

	// Load RSA PEM private key
	priv, err := loadPrivateKey(keyPath)
	if err != nil {
		fmt.Println("[-] Failed to load private key:", err)
		return
	}

	// Convert RSA PEM → OpenSSH and write to tmp directory
	openSSHPriv, _, err := createTempSSHKeypair(priv)
	if err != nil {
		fmt.Println("[-] Failed to convert to OpenSSH:", err)
		return
	}

	// Connect to manager
	fmt.Println("[+] Connecting to manager:", net.JoinHostPort(host, port))

	conn, err := connectToManager(host, port)
	if err != nil {
		fmt.Println("[-] Connection failed:", err)
		return
	}
	defer conn.Close()

	// Send USERID
	writeLine(conn, []byte(userID))

	// Receive challenge
	challenge, err := readLine(conn)
	if err != nil {
		fmt.Println("[-] Failed to read challenge:", err)
		return
	}
	fmt.Println("[+] Challenge received")

	// Sign challenge
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

	// Receive SSH mapping
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

	// Launch SSH with the converted key
	startSSH(host, sshPort, userID, openSSHPriv)
}
