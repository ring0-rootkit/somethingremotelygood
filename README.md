# Overview

## Core idea: 
Host runs a single hypervisor/container host. Each worker gets a container stored in encrypted format that is only decrypted when the worker presents their USB token. Admins provision containers and manage keys via an Database. Worker access is via SSH or RDP using keys/certs stored on their USB token (YubiKey or smartcard PIV).

<img width="1194" height="1408" alt="image" src="https://github.com/user-attachments/assets/510df374-7715-4ee2-93c2-6123fe8e4af5" />


## Components:
### Hypervisor / manager:
Docker with per container encryption?
> NOTE: I'll use docker first then might try implementing my own hypervisor using libvirt?

### Unlocking:
Store keys in encrypted SQLite database. Password for decrypting database is stored as ENV variable on host.

### User authentication to the VM:
I'll start with SSH only, (and possibly RDP support if there’s enough time)

### Short-lived access & privileged operations:
Tokens with userid and privelege id are stored in SQLite databse. When user tries to log in database is decrypted and user token is compared to the one stored in database (by userid), if they match docker will be decrypted using password stored in database and new token will be generated and sent to user.

## Requirements:
- Runs on a single server
- Admins create containers
- Workers access only their container
- All containers encrypted with separate keys
- Workers connect via USB key

# Structure

## Actors:
- _User_: Ability to access their VM with USB key.
- _Admin_: Ability to create/delete VMs.
- _Superuser_: Ability to monitor containers, access to master server, access to master key for SQLite.

# Implementation

## USB key code:
 - detect USB
 - send hello
 - receive user_id
 - Kernel sends random challenge
 - USB device signs challenge with private key
 - Kernel verifies signature with stored public key

## Steps
 - [] write helper scripts for docker
 - [] write 'manager' code to communicate with client (TCP)
 - [] integrate 'manager' with sqlite
 - [] setup sqlite with encryption
 - [] write client code for communication
 - [] implement usb token


