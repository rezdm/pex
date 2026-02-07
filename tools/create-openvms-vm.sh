#!/usr/bin/env bash
set -euo pipefail

usage() {
    echo "Usage: $0 <vm-name> <vdi-file> <vbox-vms-folder>"
    echo
    echo "  vm-name         Name for the new VirtualBox VM"
    echo "  vdi-file         Path to the OpenVMS .vdi disk image"
    echo "  vbox-vms-folder  VirtualBox machine folder (e.g. ~/VirtualBox VMs)"
    exit 1
}

if [ $# -ne 3 ]; then
    usage
fi

VM_NAME="$1"
VDI_SRC="$2"
VBOX_DIR="$3"

VM_DIR="${VBOX_DIR}/${VM_NAME}"

if ! command -v VBoxManage &>/dev/null; then
    echo "Error: VBoxManage not found. Is VirtualBox installed?"
    exit 1
fi

if [ ! -f "$VDI_SRC" ]; then
    echo "Error: VDI file not found: ${VDI_SRC}"
    exit 1
fi

if VBoxManage showvminfo "$VM_NAME" &>/dev/null; then
    echo "Error: VM '${VM_NAME}' already exists."
    exit 1
fi

# Create VM directory and copy the VDI
mkdir -p "$VM_DIR"
VDI_DEST="${VM_DIR}/${VM_NAME}.vdi"
echo "Copying VDI to ${VDI_DEST} ..."
cp "$VDI_SRC" "$VDI_DEST"

# Assign a new UUID so VirtualBox doesn't conflict with the source
VBoxManage internalcommands sethduuid "$VDI_DEST"

# Create and register the VM
VBoxManage createvm --name "$VM_NAME" --ostype "Other_64" --basefolder "$VBOX_DIR" --register

# Configure hardware
# - ICH9 chipset: provides ACPI MCFG table required by OpenVMS x86
# - EFI firmware: OpenVMS x86 does not support legacy BIOS
# - HPET: required for proper process scheduling
# - 82540EM NIC: Intel PRO/1000 MT Desktop, the only NIC type OpenVMS x86 supports
VBoxManage modifyvm "$VM_NAME" \
    --memory 2048 \
    --cpus 2 \
    --vram 32 \
    --chipset ich9 \
    --firmware efi \
    --hpet on \
    --nic1 nat \
    --nictype1 82540EM \
    --audio-enabled off

# Serial port: OpenVMS x86 redirects the console (OPA0:) to COM1.
# Connect via: telnet localhost 3023
VBoxManage modifyvm "$VM_NAME" \
    --uart1 0x3F8 4 \
    --uart-mode1 tcpserver 3023

# NAT port forwarding for SSH access
# Connect via: ssh -p 2222 system@localhost
VBoxManage modifyvm "$VM_NAME" \
    --natpf1 "ssh,tcp,,2222,,22"

# Add SATA controller and attach the disk
VBoxManage storagectl "$VM_NAME" --name "SATA" --add sata --controller IntelAhci
VBoxManage storageattach "$VM_NAME" --storagectl "SATA" --port 0 --device 0 --type hdd --medium "$VDI_DEST"

echo
echo "VM '${VM_NAME}' created successfully."
echo
echo "Start VM:        VBoxManage startvm \"${VM_NAME}\""
echo "Serial console:  telnet localhost 3023"
echo "SSH (after boot): ssh -p 2222 system@localhost"
