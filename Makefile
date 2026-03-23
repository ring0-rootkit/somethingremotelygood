.PHONY: FORCE all clean manager client keygen clean-containers cleanup-luks setup

all: manager client

MANAGER_SRC = src/manager/main.c \
              src/manager/database.c \
              src/manager/crypto.c \
              src/manager/utils.c \
              src/manager/lxd.c \
              src/manager/volume.c \
              src/manager/server.c \
              src/manager/cli.c

MANAGER_OBJ = $(MANAGER_SRC:.c=.o)

manager: $(MANAGER_OBJ)
	gcc -o build/manager $(MANAGER_OBJ) -lsqlcipher -lssl -lcrypto -lpthread  -Wall -Wextra -Werror

src/manager/%.o: src/manager/%.c
	gcc -c $< -o $@ -I src/manager -Wall -Wextra -Werror

client: src/client/*.go
	go build -o build/client ./src/client/

keygen: FORCE
	openssl genrsa -out keys/_private.pem 2048
	openssl rsa -in keys/_private.pem -pubout -keys/_public.pem
	openssl rand -out keys/_symmetric.bin 32
	ssh-keygen -f keys/_public.pem -i -m PKCS8 > keys/_public_openssh.pub

esp32-keygen: FORCE
	@read -sp "Enter password for key encryption (leave empty for no encryption): " ESP32_PASS; \
	echo; \
	if [ -n "$$ESP32_PASS" ]; then \
		python3 wrapper/esp32_keytool.py --port /dev/ttyACM0 --generate --password "$$ESP32_PASS"; \
	else \
		python3 wrapper/esp32_keytool.py --port /dev/ttyACM0 --generate; \
	fi

esp32-unlock: FORCE
	@read -sp "Enter password: " ESP32_PASS; \
	echo; \
	python3 wrapper/esp32_keytool.py --port /dev/ttyACM0 --unlock --password "$$ESP32_PASS"

esp32-lock: FORCE
	python3 wrapper/esp32_keytool.py --port /dev/ttyACM0 --lock

esp32-upload: FORCE
	esptool erase-flash
	@echo "Uploading ESP32 SSH Agent..."
	@if command -v pio >/dev/null 2>&1; then \
		cd esp32/ssh_agent && pio run --target upload; \
	else \
		echo "PlatformIO not found. Please install one of them."; \
		exit 1; \
	fi

esp32-monitor: FORCE
	@if command -v pio >/dev/null 2>&1; then \
		pio device monitor -b 115200; \
	elif command -v screen >/dev/null 2>&1; then \
		screen /dev/ttyACM0 115200; \
	else \
		echo "Cannot monitor. Install platformio or screen."; \
	fi

esp32-get-key: FORCE
	python3 wrapper/esp32_keytool.py --port /dev/ttyACM0 --get-key

esp32-sign: FORCE
	@if [ -z "$(CHALLENGE)" ]; then \
		echo "Usage: make esp32-sign CHALLENGE=<hex-challenge>"; \
		echo "Example: make esp32-sign CHALLENGE=0102030405060708090a0b0c0d0e0f10"; \
		exit 1; \
	fi
	python3 wrapper/esp32_keytool.py --port /dev/ttyACM0 --sign "$(CHALLENGE)"

agent-bridge: FORCE
	@echo "Starting ESP32 SSH Agent Bridge..."
	@echo "Run in another terminal, then use: SSH_AUTH_SOCK=/tmp/esp32-agent.sock ssh ..."
	python3 wrapper/esp32_agent_bridge.py /dev/ttyACM0

agent-test: FORCE
	SSH_AUTH_SOCK=/tmp/esp32-agent.sock ssh-add -l

clean:
	rm -f src/manager/*.o build/manager build/client

clean-containers:
	-lxc list --format csv -c n | xargs -r -I{} lxc stop {} --force 2>/dev/null || true
	-lxc list --format csv -c n | xargs -r -I{} lxc delete {} --force 2>/dev/null || true

cleanup-luks:
	@echo "Cleaning up stale LUKS devices..."
	-@for dev in $$(ls /dev/mapper/ | grep "^somethinigremotelygood_"); do \
		echo "Closing $$dev"; \
		umount "/dev/mapper/$$dev" 2>/dev/null || true; \
		cryptsetup close "$$dev" 2>/dev/null || true; \
	done
	-@for dir in homes/*_mnt; do \
		[ -d "$$dir" ] && rmdir "$$dir" 2>/dev/null || true; \
	done

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
	sudo DB_PASSWORD=123 ./build/manager add-user "$(USER)" "$$PEM_FILE" "$$SSH_FILE" && \
	sudo DB_PASSWORD=123 ./build/manager add-container "$(CONTAINER)" "$(USER)" "$$KEY_FILE"

esp32-register:
	@if [ -z "$(USER)" ] || [ -z "$(CONTAINER)" ]; then \
		echo "Usage: make esp32-register USER=<username> CONTAINER=<container_id>"; \
		echo "This will get the public key from ESP32 and register it with the manager"; \
		exit 1; \
	fi
	@echo "Getting public key from ESP32..."
	@python3 wrapper/esp32_keytool.py --port /dev/ttyACM0 --get-key > keys/esp32_pub.bin || \
		(echo "Failed to get key from ESP32. Is the bridge running? Stop it first."; exit 1)
	@if [ -s keys/esp32_pub.bin ]; then \
		echo "Registering with manager..."; \
		sudo DB_PASSWORD=123 ./build/manager add-user "$(USER)" "keys/esp32_pub.bin" "keys/esp32_pub.bin" && \
		sudo DB_PASSWORD=123 ./build/manager add-container "$(CONTAINER)" "$(USER)" "keys/_symmetric.bin"; \
	else \
		echo "Failed to get public key from ESP32"; \
		exit 1; \
	fi

client-run: FORCE
	./build/client localhost 5555 bob bob keys/_private.pem

client-run-agent: FORCE
	./build/client -agent /tmp/esp32-agent.sock localhost 5555 bob bob

FORCE:

setup: FORCE
	@echo "Installing dependencies..."
	@if command -v pacman >/dev/null 2>&1; then \
		sudo pacman -S --needed lxd cryptsetup gcc sqlcipher openssl go python; \
	elif command -v apt >/dev/null 2>&1; then \
		sudo apt install -y lxd cryptsetup gcc libsqlcipher-dev libssl-dev golang-go python3; \
	else \
		echo "Unsupported package manager. Install manually: lxd, cryptsetup, gcc, sqlcipher, openssl, go"; \
		exit 1; \
	fi
	@echo "Enabling LXD service..."
	sudo systemctl enable --now lxd 2>/dev/null || true
	@echo "Initializing LXD..."
	sudo lxd init --auto 2>/dev/null || echo "LXD already initialized"
	@echo "Adding user to lxd group..."
	sudo usermod -aG lxd $$(whoami) 2>/dev/null || true
	@echo "Preloading Alpine image..."
	lxc image copy images:alpine/3.20 local: --alias alpine 2>/dev/null || true
	@echo "Configuring firewall for LXD bridge..."
	-sudo iptables-legacy -C FORWARD -i lxdbr0 -j ACCEPT 2>/dev/null || sudo iptables-legacy -I FORWARD -i lxdbr0 -j ACCEPT
	-sudo iptables-legacy -C FORWARD -o lxdbr0 -m state --state RELATED,ESTABLISHED -j ACCEPT 2>/dev/null || sudo iptables-legacy -I FORWARD -o lxdbr0 -m state --state RELATED,ESTABLISHED -j ACCEPT
	-sudo iptables-legacy -t nat -C POSTROUTING -s $$(lxc network get lxdbr0 ipv4.address 2>/dev/null | cut -d/ -f1 | sed 's/\.[0-9]*$$/.0\/24/') ! -d $$(lxc network get lxdbr0 ipv4.address 2>/dev/null | cut -d/ -f1 | sed 's/\.[0-9]*$$/.0\/24/') -j MASQUERADE 2>/dev/null || sudo iptables-legacy -t nat -A POSTROUTING -s $$(lxc network get lxdbr0 ipv4.address 2>/dev/null | cut -d/ -f1 | sed 's/\.[0-9]*$$/.0\/24/') ! -d $$(lxc network get lxdbr0 ipv4.address 2>/dev/null | cut -d/ -f1 | sed 's/\.[0-9]*$$/.0\/24/') -j MASQUERADE
	@mkdir -p build keys homes
	@echo "Setup complete. Log out and back in for group changes to take effect."

manager-run: FORCE
	sudo DB_PASSWORD=123 ./build/manager serve 2>&1 | tee manager.log

# --- Admin CLI (via C manager) ---
list-anomalies-db: FORCE
	sudo DB_PASSWORD=123 ./build/manager list-anomalies

list-reports-db: FORCE
	sudo DB_PASSWORD=123 ./build/manager list-reports

review-anomaly: FORCE
	@if [ -z "$(REPORT_ID)" ] || [ -z "$(STATUS)" ]; then \
		echo "Usage: make review-anomaly REPORT_ID=<id> STATUS=<reviewed|escalated|dismissed>"; \
		exit 1; \
	fi
	sudo DB_PASSWORD=123 ./build/manager review-anomaly "$(REPORT_ID)" "$(STATUS)"

# --- AI Behavior Analysis ---
anomaly-detect: FORCE
	sudo DB_PASSWORD=123 python3 src/ai/anomaly_detect.py

anomaly-detect-user: FORCE
	@if [ -z "$(USER)" ]; then \
		echo "Usage: make anomaly-detect-user USER=<username>"; \
		exit 1; \
	fi
	sudo DB_PASSWORD=123 python3 src/ai/anomaly_detect.py --user "$(USER)"

analyze-anomaly: FORCE
	@if [ -z "$(REPORT_ID)" ]; then \
		echo "Usage: make analyze-anomaly REPORT_ID=<id>"; \
		exit 1; \
	fi
	sudo DB_PASSWORD=123 python3 src/ai/command_analysis.py --anomaly-id "$(REPORT_ID)"

list-anomalies: FORCE
	sudo DB_PASSWORD=123 python3 src/ai/anomaly_detect.py --list-pending

list-reports: FORCE
	sudo DB_PASSWORD=123 python3 src/ai/command_analysis.py --list

generate-baseline: FORCE
	@if [ -z "$(USER)" ]; then \
		echo "Usage: make generate-baseline USER=<username> [CONTAINER=<id>] [DAYS=30]"; \
		exit 1; \
	fi
	sudo DB_PASSWORD=123 python3 src/ai/generate_user_baseline.py "$(USER)" \
		$(if $(CONTAINER),--container "$(CONTAINER)") \
		$(if $(DAYS),--days "$(DAYS)")

clean-baseline: FORCE
	@if [ -z "$(USER)" ]; then \
		echo "Usage: make clean-baseline USER=<username> [CONTAINER=<id>]"; \
		exit 1; \
	fi
	sudo DB_PASSWORD=123 python3 src/ai/generate_user_baseline.py "$(USER)" \
		$(if $(CONTAINER),--container "$(CONTAINER)") --clean

generate-test-data: FORCE
	sudo DB_PASSWORD=123 python3 src/ai/generate_test_data.py $(ARGS)

generate-pdf: FORCE
	@if [ -z "$(JSON)" ]; then \
		echo "Usage: make generate-pdf JSON=reports/report_file.json"; \
		echo "Available reports:"; \
		ls -1 reports/*.json 2>/dev/null || echo "  (none)"; \
		exit 1; \
	fi
	python3 src/ai/generate_report_pdf.py "$(JSON)"

clean-test-data: FORCE
	sudo DB_PASSWORD=123 python3 src/ai/generate_test_data.py --clean
