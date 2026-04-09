{
  description = "somethingremotelygood";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = { self, nixpkgs, flake-utils }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgs = nixpkgs.legacyPackages.${system};
        
        # Build dependencies
        buildDeps = with pkgs; [
          clang
          gcc
          sqlcipher
          openssl
          lxc
          incus
          cryptsetup
          go
          python3
          python3Packages.python
        ];

        # Python dependencies for AI and ESP32 tools
        pythonDeps = with pkgs.python3Packages; [
          pandas
          numpy
          scikit-learn
          matplotlib
          seaborn
          fpdf
          pyserial
          pycryptodome
          requests
          pyyaml
          tabulate
          xlsxwriter
        ];

        # Development tools
        devTools = with pkgs; [
          gnumake
          pkg-config
          git
          openssh
          lxc
          cryptsetup
          utillinux
          iptables
          screen
          esptool
          platformio
          python3Packages.pip
          python3Packages.virtualenv
        ];

        # Additional utilities
        utils = with pkgs; [
          coreutils
          procps
          findutils
          gnused
          gawk
          which
        ];

      in
      {
        devShells.default = pkgs.mkShell {
          buildInputs = buildDeps ++ pythonDeps ++ devTools ++ utils;

          # Environment variables
          shellHook = ''
            echo "Setting up development environment for Clang/LXD project..."

            # Set up Go environment
            export GOPATH=$HOME/go
            export PATH=$GOPATH/bin:$PATH

            # Set up Python virtual environment
            if [ ! -d .venv ]; then
              echo "Creating Python virtual environment..."
              python3 -m venv .venv
            fi
            source .venv/bin/activate

            # Install additional Python packages if needed
            pip install --upgrade pip
            pip install pyserial pycryptodome fpdf pandas numpy scikit-learn matplotlib seaborn tabulate xlsxwriter

            # Set up LXD
            if ! command -v lxd &> /dev/null; then
              echo " LXD not found in PATH. You may need to install it separately."
              echo "   On NixOS, add 'virtualisation.lxd.enable = true;' to your configuration."
            else
              # Check if LXD is running
              if ! systemctl --user is-active --quiet lxd 2>/dev/null; then
                echo "Starting LXD socket..."
                systemctl --user start lxd 2>/dev/null || true
              fi
            fi

            # Compiler flags
            export CFLAGS="-I${pkgs.sqlcipher}/include -I${pkgs.openssl}/include"
            export LDFLAGS="-L${pkgs.sqlcipher}/lib -L${pkgs.openssl}/lib"
            export PKG_CONFIG_PATH="${pkgs.sqlcipher}/lib/pkgconfig:${pkgs.openssl}/lib/pkgconfig"
            export CFLAGS="-isystem ${pkgs.clang.cc}/resource-root/include"

            # Build instructions
            echo ""
            echo "Available commands:"
            echo "  make all          - Build manager and client"
            echo "  make manager      - Build only the manager"
            echo "  make client       - Build only the client"
            echo "  make keygen       - Generate encryption keys"
            echo "  make setup        - Install dependencies and configure LXD"
            echo "  make manager-run  - Run the manager server"
            echo "  make client-run   - Run the client"
            echo "  make clean        - Clean build artifacts"
            echo "  make esp32-upload - Upload firmware to ESP32"
            echo "  make esp32-monitor - Monitor ESP32 serial output"
            echo ""
            echo "To use LXD without sudo, log out and back in, or run:"
            echo "  newgrp lxd"
            echo ""
            echo "Development environment ready!"
          '';

          # Set up LXD socket path for non-root access
          LXD_SOCKET = "/var/snap/lxd/common/lxd/unix.socket";
        };

        # Package the application
        packages.default = pkgs.stdenv.mkDerivation {
          pname = "clang-lxd-manager";
          version = "1.0.0";

          src = ./.;

          nativeBuildInputs = with pkgs; [ 
            gcc 
            go 
            make 
            pkg-config 
          ];

          buildInputs = with pkgs; [
            sqlcipher
            openssl
            lxc
          ];

          buildPhase = ''
            make all
          '';

          installPhase = ''
            mkdir -p $out/bin
            cp build/manager build/client $out/bin/
            mkdir -p $out/share/doc
            cp README.md $out/share/doc/ 2>/dev/null || true
          '';
        };

        # Formatter
        formatter = pkgs.nixpkgs-fmt;
      }
    );
}
