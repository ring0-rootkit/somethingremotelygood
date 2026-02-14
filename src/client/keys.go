package main

import (
	"bytes"
	"crypto/rsa"
	"crypto/x509"
	"encoding/pem"
	"fmt"
	"os"

	"golang.org/x/crypto/ssh"
)

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

// Export OpenSSH *private* key ("BEGIN OPENSSH PRIVATE KEY")
func exportOpenSSHPrivateKey(priv *rsa.PrivateKey) ([]byte, error) {
	openssh, err := ssh.MarshalPrivateKey(priv, "")
	if err != nil {
		return nil, fmt.Errorf("MarshalPrivateKey failed: %w", err)
	}
	buf := new(bytes.Buffer)

	if err := pem.Encode(buf, openssh); err != nil {
		panic(err)
	}
	return buf.Bytes(), nil
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

	privPath := tmpdir + "/id_rsa"
	pubPath := tmpdir + "/id_rsa.pub"

	if err := os.WriteFile(privPath, privBytes, 0600); err != nil {
		return "", "", err
	}

	if err := os.WriteFile(pubPath, pubBytes, 0644); err != nil {
		return "", "", err
	}

	fmt.Println("[+] OpenSSH keypair written to", tmpdir)

	return privPath, pubPath, nil
}
