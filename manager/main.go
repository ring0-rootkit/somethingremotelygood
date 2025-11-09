package main

import (
	"context"
	"crypto/tls"
	"fmt"
	"os/exec"

	"github.com/Telmate/proxmox-api-go/proxmox"
)

func runZFS(args ...string) (string, error) {
	cmd := exec.Command("zfs", args...)
	out, err := cmd.CombinedOutput()
	return string(out), err
}

func createEncryptedZvol(pool, volName, size string) error {
	zvol := fmt.Sprintf("%s/%s", pool, volName)
	args := []string{
		"create", "-V", size,
		"-o", "encryption=on",
		"-o", "keyformat=passphrase",
		"-o", "keylocation=prompt",
		zvol,
	}
	out, err := runZFS(args...)
	if err != nil {
		return fmt.Errorf("zfs create failed: %v, out: %s", err, out)
	}
	return nil
}

func destroyZFS(name string) error {
	out, err := runZFS("destroy", "-r", name)
	if err != nil {
		return fmt.Errorf("zfs destroy failed: %v, out: %s", err, out)
	}
	return nil
}

func createProxmoxVM(client *proxmox.Client, node proxmox.NodeName, vmid proxmox.GuestID, name, storage, disk string) error {
	ctx := context.Background()
	params := map[string]interface{}{
		"vmid":	vmid,
		"name":	name,
		"memory":  2048,
		"cores":   2,
		"storage": storage,
		"scsi0":   fmt.Sprintf("%s:%s", storage, disk),
	}

	status, err := client.CreateQemuVm(ctx, node, params)
	if err != nil {
		return fmt.Errorf("CreateQemuVm error: %v", err)
	}
	fmt.Println("Create VM task status:", status)
	return nil
}

func startVM(client *proxmox.Client, vmRef *proxmox.VmRef) error {
	ctx := context.Background()
	status, err := client.StartVm(ctx, vmRef)
	if err != nil {
		return fmt.Errorf("StartVm error: %v", err)
	}
	fmt.Println("Start VM task status:", status)
	return nil
}

func stopVM(client *proxmox.Client, vmRef *proxmox.VmRef) error {
	ctx := context.Background()
	status, err := client.StopVm(ctx, vmRef)
	if err != nil {
		return fmt.Errorf("StopVm error: %v", err)
	}
	fmt.Println("Stop VM task status:", status)
	return nil
}

func deleteVM(client *proxmox.Client, vmRef *proxmox.VmRef) error {
	ctx := context.Background()
	status, err := client.DeleteVm(ctx, vmRef)
	if err != nil {
		return fmt.Errorf("DeleteVm error: %v", err)
	}
	fmt.Println("Delete VM task status:", status)
	return nil
}

func main() {
	// Example: set up client with API token authentication

	apiURL := "https://localhost:8006/api2/json"
	user := ""                    // leave blank if using API token
	tlsConf := &tls.Config{InsecureSkipVerify: true} // skip verification (dev only)
	ticket := ""                  // leave blank for API token
	timeout := 30                 // seconds

	client, err := proxmox.NewClient(apiURL, nil, user, tlsConf, ticket, timeout)
	if err != nil {
	    panic(err)
	}

	client.SetAPIToken("user@pam!tokenid", "secret")
	
	nodeName := "node1"
	node := proxmox.NodeName(nodeName)
	vmid := proxmox.GuestID(101)

	vmRef := proxmox.NewVmRef(vmid)
	vmRef.SetNode(nodeName)

	if err := createEncryptedZvol("tank", "vm-101-disk0", "40G"); err != nil {
		fmt.Println("Error creating encrypted zvol:", err)
		return
	}
	if err := createProxmoxVM(client, node, vmid, "testvm-101", "tank", "vm-101-disk0"); err != nil {
		fmt.Println("Error creating VM:", err)
		return
	}
	if err := startVM(client, vmRef); err != nil {
		fmt.Println("Error starting VM:", err)
	}
}
