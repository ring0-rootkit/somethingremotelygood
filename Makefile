.PHONY: FORCE all clean manager client keygen

all: manager client

MANAGER_SRC = src/manager/main.c \
              src/manager/database.c \
              src/manager/crypto.c \
              src/manager/utils.c \
              src/manager/docker.c \
              src/manager/server.c \
              src/manager/cli.c

MANAGER_OBJ = $(MANAGER_SRC:.c=.o)

manager: $(MANAGER_OBJ)
	gcc -o build/manager $(MANAGER_OBJ) -lsqlite3 -lssl -lcrypto

src/manager/%.o: src/manager/%.c
	gcc -c $< -o $@ -I src/manager

client: src/client/*.go
	go build -o build/client ./src/client/

keygen: FORCE
	openssl genrsa -out keys/_private.pem 2048
	openssl rsa -in keys/_private.pem -pubout -out keys/_public.pem
	openssl rand -out keys/_symmetric.bin 32
	ssh-keygen -f keys/_public.pem -i -m PKCS8 > keys/_public_openssh.pub

clean:
	rm -f src/manager/*.o build/manager build/client

FORCE:
