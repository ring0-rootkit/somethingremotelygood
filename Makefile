.PHONY: FORCE
FORCE:

manager: manager.c
	gcc -o build/manager manager.c -lsqlite3 -lssl -lcrypto
client: client.go
	go build -o build/client client.go
keygen: FORCE
	openssl genrsa -out keys/_private.pem 2048
	openssl rsa -in keys/_private.pem -pubout -out keys/_public.pem
	openssl rand -out keys/_symmetric.bin 32


