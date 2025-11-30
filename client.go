package main

import (
    "crypto"
    "crypto/rand"
    "crypto/rsa"
    "crypto/sha256"
    "crypto/x509"
    "encoding/pem"
    "fmt"
    "golang.org/x/crypto/ssh"
    "net"
    "os"
    "os/exec"
    "path/filepath"
    "time"
)

//
// -----------------------------------------------------------------------------
// PRIVATE KEY LOADING
// -----------------------------------------------------------------------------

func loadPrivateKey(path string) (*rsa.PrivateKey, error) {
    data, err := os.ReadFile(path)
    if err != nil {
        return nil, fmt.Errorf("failed to read key: %w", err)
    }

    block, _ := pem.Decode(data)
    if block == nil {
        return nil, fmt.Errorf("no PEM block found in key file")
    }

    // Try PKCS#1 RSA
    if key, err := x509.ParsePKCS1PrivateKey(block.Bytes); err == nil {
        return key, nil
    }

    // Try PKCS#8
    pk, err := x509.ParsePKCS8PrivateKey(block.Bytes)
    if err == nil {
        if k, ok := pk.(*rsa.PrivateKey); ok {
            return k, nil
        }
    }

    return nil, fmt.Errorf("unsupported private key format")
}

//
// -----------------------------------------------------------------------------
// SIGNATURE
// -----------------------------------------------------------------------------

func signChallenge(priv *rsa.PrivateKey, challenge []byte) ([]byte, error) {
    hash := sha256.Sum256(challenge)
    return rsa.SignPKCS1v15(rand.Reader, priv, crypto.SHA256, hash[:])
}

//
// -----------------------------------------------------------------------------
// OPENSSH CONVERSION
// -----------------------------------------------------------------------------

// Export OpenSSH *private* key ("BEGIN OPENSSH PRIVATE KEY")
func exportOpenSSHPrivateKey(priv *rsa.PrivateKey) ([]byte, error) {
    openssh, err := ssh.MarshalPrivateKey(priv, "")
    if err != nil {
        return nil, fmt.Errorf("MarshalPrivateKey failed: %w", err)
    }
    return openssh.Bytes, nil
}

// Export OpenSSH public key ("ssh-rsa ...")
func exportOpenSSHPublicKey(priv *rsa.PrivateKey) ([]byte, error) {
    pub, err := ssh.NewPublicKey(&priv.PublicKey)
    if err != nil {
        return nil, err
    }
    return ssh.MarshalAuthorizedKey(pub), nil
}

// Create temp directory and store id_rsa + id_rsa.pub
func createTempSSHKeypair(priv *rsa.PrivateKey) (string, string, error) {
    tmpdir := fmt.Sprintf("/tmp/sshkey-%d", os.Getpid())
    os.MkdirAll(tmpdir, 0700)

    privBytes, err := exportOpenSSHPrivateKey(priv)
    if err != nil {
        return "", "", err
    }

    pubBytes, err := exportOpenSSHPublicKey(priv)
    if err != nil {
        return "", "", err
    }

    privPath := filepath.Join(tmpdir, "id_rsa")
    pubPath := filepath.Join(tmpdir, "id_rsa.pub")

    if err := os.WriteFile(privPath, privBytes, 0600); err != nil {
        return "", "", err
    }

    if err := os.WriteFile(pubPath, pubBytes, 0644); err != nil {
        return "", "", err
    }

    fmt.Println("[+] OpenSSH keypair written to", tmpdir)

    return privPath, pubPath, nil
}

//
// -----------------------------------------------------------------------------
// TCP HELPERS
// -----------------------------------------------------------------------------

func readLine(conn net.Conn) (string, error) {
    buf := make([]byte, 4096)
    n, err := conn.Read(buf)
    if err != nil {
        return "", err
    }
    return string(buf[:n]), nil
}

func writeLine(conn net.Conn, s []byte) error {
    _, err := conn.Write(s)
    return err
}

//
// -----------------------------------------------------------------------------
// LAUNCH SSH
// -----------------------------------------------------------------------------

func startSSH(host string, port int, userID, keyFile string) error {
    cmd := exec.Command("ssh",
        "-o", "StrictHostKeyChecking=no",
        "-i", keyFile,
        fmt.Sprintf("%s@%s", userID, host),
        "-p", fmt.Sprint(port),
    )

    cmd.Stdin = os.Stdin
    cmd.Stdout = os.Stdout
    cmd.Stderr = os.Stderr

    return cmd.Run()
}

//
// -----------------------------------------------------------------------------
// MAIN CLIENT
// -----------------------------------------------------------------------------

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
    addr := net.JoinHostPort(host, port)
    fmt.Println("[+] Connecting to manager:", addr)

    conn, err := net.DialTimeout("tcp", addr, 5*time.Second)
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
