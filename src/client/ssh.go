package main

import (
	"fmt"
	"os"
	"os/exec"
)

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

func startSSHWithAgent(host string, port int, userID, agentSocket string) error {
	cmd := exec.Command("ssh",
		"-o", "StrictHostKeyChecking=no",
		fmt.Sprintf("%s@%s", userID, host),
		"-p", fmt.Sprint(port),
	)

	cmd.Env = append(os.Environ(), "SSH_AUTH_SOCK="+agentSocket)
	cmd.Stdin = os.Stdin
	cmd.Stdout = os.Stdout
	cmd.Stderr = os.Stderr

	return cmd.Run()
}
