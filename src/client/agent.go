package main

import (
	"bytes"
	"encoding/binary"
	"errors"
	"fmt"
	"io"
	"net"
	"syscall"

	"golang.org/x/term"
)

const (
	sshAgentCRequestIdentities   uint8 = 11
	sshAgentCSignRequest         uint8 = 13
	sshAgentCAddIdentity         uint8 = 17
	sshAgentCRemoveIdentity      uint8 = 18
	sshAgentCRemoveAllIdentities uint8 = 19
	sshAgentCAddIdConstrained    uint8 = 25
	sshAgentCExtension           uint8 = 27

	sshAgentFailure          uint8 = 5
	sshAgentSuccess          uint8 = 6
	sshAgentIdentitiesAnswer uint8 = 12
	sshAgentSignResponse     uint8 = 14

	sshAgentLocked uint8 = 0x20
)

type Agent struct {
	conn net.Conn
}

type AgentKey struct {
	Blob    []byte
	Comment string
}

func NewAgent(socketPath string) (*Agent, error) {
	conn, err := net.Dial("unix", socketPath)
	if err != nil {
		return nil, fmt.Errorf("failed to connect to agent: %w", err)
	}
	return &Agent{conn: conn}, nil
}

func (a *Agent) Close() error {
	return a.conn.Close()
}

func (a *Agent) sendRequest(reqType uint8, payload []byte) ([]byte, error) {
	buf := new(bytes.Buffer)
	binary.Write(buf, binary.BigEndian, uint32(len(payload)+1))
	buf.WriteByte(reqType)
	buf.Write(payload)

	_, err := a.conn.Write(buf.Bytes())
	if err != nil {
		return nil, err
	}

	respLenBuf := make([]byte, 4)
	_, err = io.ReadFull(a.conn, respLenBuf)
	if err != nil {
		return nil, err
	}
	respLen := binary.BigEndian.Uint32(respLenBuf)

	resp := make([]byte, respLen)
	_, err = io.ReadFull(a.conn, resp)
	if err != nil {
		return nil, err
	}

	if len(resp) == 0 || resp[0] == sshAgentFailure {
		return nil, errors.New("agent request failed")
	}

	if resp[0] == sshAgentLocked {
		return nil, errors.New("agent locked")
	}

	return resp, nil
}

func (a *Agent) List() ([]AgentKey, error) {
	resp, err := a.sendRequest(sshAgentCRequestIdentities, nil)
	if err != nil {
		return nil, err
	}

	if len(resp) < 2 {
		return nil, errors.New("invalid response")
	}

	numKeys := binary.BigEndian.Uint32(resp[1:5])
	keys := make([]AgentKey, 0, numKeys)
	pos := 5

	for i := uint32(0); i < numKeys && pos < len(resp); i++ {
		if pos+4 > len(resp) {
			break
		}
		keyLen := binary.BigEndian.Uint32(resp[pos : pos+4])
		pos += 4

		if pos+int(keyLen) > len(resp) {
			break
		}
		keyBlob := resp[pos : pos+int(keyLen)]
		pos += int(keyLen)

		if pos+4 > len(resp) {
			break
		}
		commentLen := binary.BigEndian.Uint32(resp[pos : pos+4])
		pos += 4

		comment := ""
		if commentLen > 0 && pos+int(commentLen) <= len(resp) {
			comment = string(resp[pos : pos+int(commentLen)])
			pos += int(commentLen)
		}

		keys = append(keys, AgentKey{Blob: keyBlob, Comment: comment})
	}

	return keys, nil
}

func (a *Agent) Unlock(password []byte) error {
	payload := new(bytes.Buffer)
	// Extension name
	extName := []byte("esp32-unlock")
	binary.Write(payload, binary.BigEndian, uint32(len(extName)))
	payload.Write(extName)
	// Password as SSH string
	binary.Write(payload, binary.BigEndian, uint32(len(password)))
	payload.Write(password)

	_, err := a.sendRequest(sshAgentCExtension, payload.Bytes())
	return err
}

func (a *Agent) Sign(keyBlob []byte, data []byte, flags uint8) ([]byte, error) {
	payload := new(bytes.Buffer)
	binary.Write(payload, binary.BigEndian, uint32(len(keyBlob)))
	payload.Write(keyBlob)
	binary.Write(payload, binary.BigEndian, uint32(len(data)))
	payload.Write(data)
	payload.WriteByte(flags)

	resp, err := a.sendRequest(sshAgentCSignRequest, payload.Bytes())
	if err != nil {
		return nil, err
	}

	if len(resp) < 5 {
		return nil, errors.New("invalid sign response")
	}

	sigLen := binary.BigEndian.Uint32(resp[1:5])
	if len(resp) < 5+int(sigLen) {
		return nil, errors.New("invalid signature length")
	}

	return resp[5 : 5+sigLen], nil
}

type ESP32Agent struct {
	socketPath string
	agent      *Agent
	publicKey  []byte
}

func NewESP32Agent(socketPath string) (*ESP32Agent, error) {
	agent, err := NewAgent(socketPath)
	if err != nil {
		return nil, err
	}

	keys, err := agent.List()
	if err != nil {
		agent.Close()
		return nil, err
	}

	if len(keys) == 0 {
		agent.Close()
		return nil, errors.New("no keys found on agent")
	}

	return &ESP32Agent{
		socketPath: socketPath,
		agent:      agent,
		publicKey:  keys[0].Blob,
	}, nil
}

func (e *ESP32Agent) Close() error {
	if e.agent != nil {
		return e.agent.Close()
	}
	return nil
}

func (e *ESP32Agent) SignChallenge(challenge []byte) ([]byte, error) {
	flags := uint8(0)

	sig, err := e.agent.Sign(e.publicKey, challenge, flags)
	if err != nil && err.Error() == "agent locked" {
		fmt.Print("[*] ESP32 agent is locked. Enter password: ")
		password, readErr := term.ReadPassword(int(syscall.Stdin))
		fmt.Println()
		if readErr != nil {
			return nil, fmt.Errorf("failed to read password: %w", readErr)
		}

		if unlockErr := e.agent.Unlock(password); unlockErr != nil {
			return nil, fmt.Errorf("unlock failed (wrong password?): %w", unlockErr)
		}
		fmt.Println("[+] Agent unlocked")

		sig, err = e.agent.Sign(e.publicKey, challenge, flags)
	}
	if err != nil {
		return nil, err
	}

	return sig, nil
}

func (e *ESP32Agent) GetPublicKey() []byte {
	return e.publicKey
}

func ConnectWithESP32Agent(host, port, userID, containerID, agentSocket string) error {
	agent, err := NewESP32Agent(agentSocket)
	if err != nil {
		return fmt.Errorf("failed to connect to ESP32 agent: %w", err)
	}
	defer agent.Close()

	fmt.Println("[+] Connected to ESP32 SSH Agent")

	conn, err := connectToManager(host, port)
	if err != nil {
		return fmt.Errorf("connection failed: %w", err)
	}
	defer conn.Close()

	writeLine(conn, []byte(userID))

	challenge, err := readLine(conn)
	if err != nil {
		return fmt.Errorf("failed to read challenge: %w", err)
	}
	fmt.Println("[+] Challenge received")

	sig, err := agent.SignChallenge([]byte(challenge+userID+containerID))
	if err != nil {
		return fmt.Errorf("failed to sign with ESP32 agent: %w", err)
	}

	writeLine(conn, sig)

	reply, err := readLine(conn)
	if err != nil {
		return fmt.Errorf("failed to read reply: %w", err)
	}

	if reply != "REQ CID" {
		return fmt.Errorf("access denied: %s", reply)
	}

	writeLine(conn, []byte(containerID))

	reply, err = readLine(conn)
	if err != nil {
		return fmt.Errorf("failed to read reply: %w", err)
	}
	if reply != "OK" {
		return fmt.Errorf("received error: %s", reply)
	}

	sshInfo, err := readLine(conn)
	if err != nil {
		return fmt.Errorf("failed to read SSH info: %w", err)
	}

	var sshPort int
	_, _ = fmt.Sscanf(sshInfo, "%d", &sshPort)

	fmt.Printf("[+] Container unlocked: SSH at %s:%d\n", host, sshPort)
	fmt.Println("[+] Authentication successful")

	conn.Close()

	startSSHWithAgent(host, sshPort, userID, agentSocket)

	return nil
}


func RunESP32AgentMode(serialPort string, baudrate int) error {
	fmt.Println("[!] ESP32 Agent mode requires the Python bridge to be running")
	fmt.Printf("[!] Run: python wrapper/esp32_agent_bridge.py %s\n", serialPort)
	fmt.Println("[!] Then set SSH_AUTH_SOCK=/tmp/esp32-agent.sock and try again")
	return nil
}
