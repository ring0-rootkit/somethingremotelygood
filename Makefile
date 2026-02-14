.PHONY: FORCE all clean manager client keygen clean-containers

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

clean-containers:
	-docker ps -aq | xargs -r docker stop
	-docker ps -aq | xargs -r docker rm -f

register-user:
	@if [ -z "$(USER)" ] || [ -z "$(CONTAINER)" ]; then \
		echo "Usage: make register-user USER=<username> CONTAINER=<container_id>"; \
		echo "Optional: PEM=path/to/key.pem SSH=path/to/key.pub KEY=path/to/container.key"; \
		exit 1; \
	fi
	@echo "Registering user $(USER) with container $(CONTAINER)..."
	@PEM_FILE="$${PEM:-keys/_public.pem}"; \
	SSH_FILE="$${SSH:-keys/_public_openssh.pub}"; \
	KEY_FILE="$${KEY:-keys/_symmetric.bin}"; \
	./build/manager add-user "$(USER)" "$$PEM_FILE" "$$SSH_FILE" && \
	./build/manager add-container "$(CONTAINER)" "$(USER)" "$$KEY_FILE"

FORCE:
