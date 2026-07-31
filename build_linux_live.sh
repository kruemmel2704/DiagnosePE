#!/bin/bash
set -e

echo "=========================================================="
echo "    DiagnoseLinux Arch Live ISO Builder (archiso)         "
echo "=========================================================="

PROJECT_DIR="$(pwd)"
BUILD_DIR="/var/tmp/diagnose_live_build"
ISO_NAME="DiagnoseLinux.iso"
PROFILE_DIR="${BUILD_DIR}/archiso-profile"
OUT_DIR="${BUILD_DIR}/out"
WORK_DIR="${BUILD_DIR}/work"

# 1. Check if running on Arch Linux host
if ! command -v pacman &> /dev/null; then
    echo "[!] Dieser Build-Skript erfordert ein Arch Linux System oder WSL Arch Linux!"
    exit 1
fi

# Clean up build directory
echo "[+] Bereinige alten Build-Ordner..."
sudo rm -rf "${BUILD_DIR}"
mkdir -p "${BUILD_DIR}" "${OUT_DIR}" "${WORK_DIR}"

# Install host prerequisites if missing
echo "[+] Installiere Host-Voraussetzungen..."
sudo pacman -S --needed --noconfirm archiso cmake make gcc git sdl2 mesa mesa-utils openbox xterm smartmontools gparted gsmartcontrol lm_sensors memtester stress-ng pciutils usbutils dmidecode nvme-cli lshw htop ntfs-3g chntpw glmark2 pkgconf

# 2. Copy the official releng profile
echo "[+] Kopiere archiso releng Profil..."
cp -r /usr/share/archiso/configs/releng "${PROFILE_DIR}"

# Update file permissions in profiledef.sh to make our binaries executable
sed -i 's|\["/usr/local/bin/livecd-sound"\]="0:0:755"|\["/usr/local/bin/livecd-sound"\]="0:0:755"\n  \["/usr/local/bin/DiagnoseLinux"\]="0:0:755"\n  \["/usr/local/bin/windows_repair.sh"\]="0:0:755"|' "${PROFILE_DIR}/profiledef.sh"

# 3. Add packages to packages.x86_64
echo "[+] Füge DiagnoseLinux-Pakete zum Profil hinzu..."
cat << 'EOF' >> "${PROFILE_DIR}/packages.x86_64"
xorg-server
xorg-xinit
openbox
xterm
sdl2
mesa
mesa-utils
xf86-video-amdgpu
nvidia-open
nvidia-utils
xf86-video-fbdev
xf86-video-vesa
smartmontools
gparted
gsmartcontrol
lm_sensors
memtester
stress-ng
pciutils
usbutils
dmidecode
nvme-cli
lshw
htop
ntfs-3g
chntpw
cmake
make
gcc
glmark2
EOF

# 4. Compile DiagnoseLinux natively on the host
echo "[+] Kompiliere DiagnoseLinux-Anwendung..."
rm -rf "${PROJECT_DIR}/build"
mkdir -p "${PROJECT_DIR}/build"
cd "${PROJECT_DIR}/build"
cmake ..
make -j$(nproc)
cd "${PROJECT_DIR}"

# 5. Place application binaries and repair script into ISO structure
echo "[+] Kopiere Dateien in das Live-Dateisystem..."
mkdir -p "${PROFILE_DIR}/airootfs/usr/local/bin"
cp "${PROJECT_DIR}/build/DiagnoseLinux" "${PROFILE_DIR}/airootfs/usr/local/bin/DiagnoseLinux"
chmod +x "${PROFILE_DIR}/airootfs/usr/local/bin/DiagnoseLinux"

cp "${PROJECT_DIR}/windows_repair.sh" "${PROFILE_DIR}/airootfs/usr/local/bin/windows_repair.sh"
chmod +x "${PROFILE_DIR}/airootfs/usr/local/bin/windows_repair.sh"

# 6. Configure Autostart (kiosk mode)
echo "[+] Konfiguriere Autostart (Kiosk-Modus)..."

# Create Xinitrc configuration for root
mkdir -p "${PROFILE_DIR}/airootfs/root"
cat << 'EOF' > "${PROFILE_DIR}/airootfs/root/.xinitrc"
#!/bin/bash
# Start Openbox Window Manager in background
openbox &
# Launch the main diagnostic app in fullscreen/maximized mode
exec /usr/local/bin/DiagnoseLinux
EOF
chmod +x "${PROFILE_DIR}/airootfs/root/.xinitrc"

# Override automated_script.sh to start Xorg automatically on tty1 login
cat << 'EOF' > "${PROFILE_DIR}/airootfs/root/.automated_script.sh"
#!/bin/bash
# If we are on tty1, launch Xorg server running our kiosk environment
if [[ $(tty) == "/dev/tty1" ]]; then
    exec startx -- :0 -nolisten tcp vt1
fi
EOF
chmod +x "${PROFILE_DIR}/airootfs/root/.automated_script.sh"

# 7. Build the Live ISO using mkarchiso
echo "[+] Starte mkarchiso (dies kann einige Minuten dauern)..."
sudo mkarchiso -v -w "${WORK_DIR}" -o "${OUT_DIR}" "${PROFILE_DIR}"

# 8. Move the completed ISO back to the project folder
echo "[+] Bereite Ausgabe vor..."
rm -f "${PROJECT_DIR}/${ISO_NAME}"
cp "${OUT_DIR}"/*.iso "${PROJECT_DIR}/${ISO_NAME}"
sudo chmod 666 "${PROJECT_DIR}/${ISO_NAME}" 2>/dev/null || true

# Grant Hyper-V permissions to the generated ISO if running in WSL
if command -v icacls.exe &> /dev/null; then
    echo "[+] Gewähre Hyper-V Berechtigungen für das ISO..."
    icacls.exe "$(wslpath -w "${PROJECT_DIR}/${ISO_NAME}")" /grant 'Jeder:(R)' 2>/dev/null || true
    icacls.exe "$(wslpath -w "${PROJECT_DIR}/${ISO_NAME}")" /grant 'Everyone:(R)' 2>/dev/null || true
    icacls.exe "$(wslpath -w "${PROJECT_DIR}/${ISO_NAME}")" /grant '*S-1-5-83-0:(R)' 2>/dev/null || true
    icacls.exe "$(wslpath -w "${PROJECT_DIR}/${ISO_NAME}")" /grant 'NT VIRTUAL MACHINE\Virtual Machines:(R)' 2>/dev/null || true
fi

# Clean up build directory to free space
sudo rm -rf "${BUILD_DIR}"

echo "=========================================================="
echo " [SUCCESS] Arch Live ISO erfolgreich generiert:"
echo " ${PROJECT_DIR}/${ISO_NAME}"
echo " Bereit zum Flashen via Ventoy, Rufus oder dd!"
echo "=========================================================="
