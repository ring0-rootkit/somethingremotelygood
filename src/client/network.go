package main

import (
	"net"
	"time"
)

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

func connectToManager(host, port string) (net.Conn, error) {
	addr := net.JoinHostPort(host, port)
	return net.DialTimeout("tcp", addr, 5*time.Second)
}
