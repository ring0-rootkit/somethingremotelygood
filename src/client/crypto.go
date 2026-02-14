package main

import (
	"crypto"
	"crypto/rand"
	"crypto/rsa"
	"crypto/sha256"
)

func signChallenge(priv *rsa.PrivateKey, challenge []byte) ([]byte, error) {
	hash := sha256.Sum256(challenge)
	return rsa.SignPKCS1v15(rand.Reader, priv, crypto.SHA256, hash[:])
}
