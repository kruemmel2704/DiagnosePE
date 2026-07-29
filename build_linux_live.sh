#!/bin/bash
set -e

echo "=========================================================="
echo "    DiagnoseLinux Live Kernel & ISO Image Builder        "
echo "=========================================================="

PROJECT_DIR="$(pwd)"
BUILD_DIR="/tmp/diagnose_live_build"
ISO_NAME="DiagnoseLinux.iso"
ROOTFS_DIR="${BUILD_DIR}/rootfs"
ISO_DIR="${BUILD_DIR}/iso"

# Safely unmount leftover mounts from previous runs if any
sudo umount -l "${ROOTFS_DIR}/dev" 2>/dev/null || true
sudo umount -l "${ROOTFS_DIR}/proc" 2>/dev/null || true
sudo umount -l "${ROOTFS_DIR}/sys" 2>/dev/null || true
sudo rm -rf "${BUILD_DIR}"

# 1. Install prerequisites if running on Debian/Ubuntu host
if command -v apt-get &> /dev/null; then
    echo "[+] Installing host build prerequisites..."
    sudo apt-get update
    sudo apt-get install -y debootstrap xorriso squashfs-tools grub-pc-bin grub-efi-amd64-bin mtools pkg-config
fi

# 2. Bootstrap Debian Bookworm Live Base System in native Linux ext4 (/tmp)
echo "[+] Bootstrapping Debian 12 (Bookworm) Live System in ${ROOTFS_DIR}..."
sudo mkdir -p "${ROOTFS_DIR}"
sudo debootstrap --arch=amd64 bookworm "${ROOTFS_DIR}" http://deb.debian.org/debian/

# Configure Debian Non-Free-Firmware Repositories inside rootfs
cat << 'EOF' | sudo tee "${ROOTFS_DIR}/etc/apt/sources.list"
deb http://deb.debian.org/debian bookworm main contrib non-free non-free-firmware
deb http://deb.debian.org/debian-security bookworm-security main contrib non-free non-free-firmware
deb http://deb.debian.org/debian bookworm-updates main contrib non-free non-free-firmware
EOF

# Clean host build artifacts and copy clean source code into rootfs for target compilation
echo "[+] Copying source files to chroot for native target compilation..."
sudo rm -rf "${PROJECT_DIR}/build"
sudo mkdir -p "${ROOTFS_DIR}/tmp/app_src"
sudo cp -r "${PROJECT_DIR}/"* "${ROOTFS_DIR}/tmp/app_src/"

# 3. Configure Chroot & System Packages
echo "[+] Installing Kernel, Libraries & Compiling DiagnoseLinux inside target rootfs..."
sudo mount --bind /dev "${ROOTFS_DIR}/dev"
sudo mount --bind /proc "${ROOTFS_DIR}/proc"
sudo mount --bind /sys "${ROOTFS_DIR}/sys"

cat << 'EOF' | sudo chroot "${ROOTFS_DIR}" /bin/bash
export DEBIAN_FRONTEND=noninteractive
apt-get update
apt-get install -y \
    linux-image-amd64 \
    live-boot \
    initramfs-tools \
    systemd-sysv \
    xorg \
    xinit \
    matchbox-window-manager \
    x11-xserver-utils \
    libsdl2-2.0-0 \
    libsdl2-dev \
    libgl1-mesa-dev \
    mesa-utils \
    xserver-xorg-video-all \
    cmake \
    build-essential \
    g++

apt-get install -y --no-install-recommends \
    firmware-linux-free \
    firmware-realtek \
    firmware-iwlwifi \
    sudo \
    smartmontools \
    gsmartcontrol \
    lm-sensors \
    memtester \
    stress-ng \
    gparted \
    pciutils \
    usbutils \
    dmidecode \
    nvme-cli \
    lshw \
    htop \
    xterm

# Compile DiagnoseLinux natively inside Debian 12 rootfs
echo "[+] Compiling C++ binary inside Debian 12 chroot..."
cd /tmp/app_src
rm -rf build
mkdir -p build && cd build
cmake ..
make -j$(nproc)
cp DiagnoseLinux /usr/local/bin/DiagnoseLinux
chmod +x /usr/local/bin/DiagnoseLinux
cd /
rm -rf /tmp/app_src

# Create diagnose user
useradd -m -s /bin/bash diagnose
echo "diagnose:diagnose" | chpasswd
usermod -aG sudo,video,input,render diagnose
echo "diagnose ALL=(ALL) NOPASSWD: ALL" >> /etc/sudoers
EOF

# 4. Configure Autostart Kiosk Mode
echo "[+] Configuring DiagnoseLinux Kiosk Shell..."

sudo mkdir -p "${ROOTFS_DIR}/home/diagnose"
cat << 'EOF' | sudo tee "${ROOTFS_DIR}/home/diagnose/.xinitrc"
#!/bin/bash
xset s off 2>/dev/null || true
xset -dpms 2>/dev/null || true
xset s noblank 2>/dev/null || true
matchbox-window-manager &
exec /usr/local/bin/DiagnoseLinux
EOF
sudo chmod +x "${ROOTFS_DIR}/home/diagnose/.xinitrc"

# Configure systemd tty1 autologin
sudo mkdir -p "${ROOTFS_DIR}/etc/systemd/system/getty@tty1.service.d"
cat << 'EOF' | sudo tee "${ROOTFS_DIR}/etc/systemd/system/getty@tty1.service.d/override.conf"
[Service]
ExecStart=
ExecStart=-/sbin/agetty --autologin diagnose --noclear %I $TERM
EOF

# Configure bashrc to launch startx on tty1 login
cat << 'EOF' | sudo tee -a "${ROOTFS_DIR}/home/diagnose/.bashrc"
if [ -z "$DISPLAY" ] && [ "$(tty)" = "/dev/tty1" ]; then
    exec startx -- :0
fi
EOF

sudo chroot "${ROOTFS_DIR}" chown -R diagnose:diagnose /home/diagnose

# Unmount chroot mounts
sudo umount "${ROOTFS_DIR}/dev"
sudo umount "${ROOTFS_DIR}/proc"
sudo umount "${ROOTFS_DIR}/sys"

# 5. Build Live Squashfs Filesystem
echo "[+] Creating compressed SquashFS live image..."
sudo mkdir -p "${ISO_DIR}/live"
sudo mksquashfs "${ROOTFS_DIR}" "${ISO_DIR}/live/filesystem.squashfs" -comp xz

# Copy Kernel and Initrd
KERNEL_FILE=$(sudo find "${ROOTFS_DIR}" -name "vmlinuz*" | head -n 1)
INITRD_FILE=$(sudo find "${ROOTFS_DIR}" -name "initrd*" | head -n 1)

echo "[+] Copying Kernel: ${KERNEL_FILE}"
echo "[+] Copying Initrd: ${INITRD_FILE}"

sudo cp "${KERNEL_FILE}" "${ISO_DIR}/live/vmlinuz"
sudo cp "${INITRD_FILE}" "${ISO_DIR}/live/initrd.img"

# 6. Configure GRUB Bootloader for UEFI & BIOS Boot
echo "[+] Configuring GRUB Bootloader..."
sudo mkdir -p "${ISO_DIR}/boot/grub"
cat << 'EOF' | sudo tee "${ISO_DIR}/boot/grub/grub.cfg"
set default=0
set timeout=5

menuentry "DiagnoseLinux Hardware Diagnostic Live OS (UEFI/BIOS)" {
    linux /live/vmlinuz boot=live quiet splash
    initrd /live/initrd.img
}

menuentry "DiagnoseLinux Safe Graphics Mode" {
    linux /live/vmlinuz boot=live nomodeset quiet
    initrd /live/initrd.img
}
EOF

# 7. Generate Hybrid Bootable ISO Image
echo "[+] Packaging bootable ISO: ${PROJECT_DIR}/${ISO_NAME}..."
sudo rm -f "${PROJECT_DIR}/${ISO_NAME}"
sudo grub-mkrescue -o "${PROJECT_DIR}/${ISO_NAME}" "${ISO_DIR}"
sudo chmod 666 "${PROJECT_DIR}/${ISO_NAME}" 2>/dev/null || true

# Clean up temp build directory
sudo rm -rf "${BUILD_DIR}"

echo "=========================================================="
echo " [SUCCESS] Linux Live ISO generated: ${PROJECT_DIR}/${ISO_NAME}"
echo " Ready for USB flashing via Ventoy, Rufus, or dd!"
echo "=========================================================="
