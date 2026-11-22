#!/bin/sh
# Deploys persistent Alpine Linux 3.21 aarch64 Phase 3 rootfs into /cache/pmos_root
# Runs on Android (su) using our Alpine build chroot's apk + adb reverse proxy (127.0.0.1:1082).
# Does NOT format /dev/sda29 (/cache) and leaves Android's /cache/recovery untouched!
set -eu

ALPINE_CHROOT=/data/local/linux_port/alpine_build
ROOT=/cache/pmos_root
MODS="$ALPINE_CHROOT/root/work/phase3_mods"

echo "=== [1/6] Preparing $ROOT on /dev/sda29 (/cache) ==="
df -h /cache
rm -rf "$ROOT"
mkdir -p "$ROOT"

# Bind-mount /cache/pmos_root inside our Alpine build chroot so apk can install cleanly
mkdir -p "$ALPINE_CHROOT/mnt/pmos_root"
mountpoint -q "$ALPINE_CHROOT/mnt/pmos_root" && umount "$ALPINE_CHROOT/mnt/pmos_root" || true
mount -o bind "$ROOT" "$ALPINE_CHROOT/mnt/pmos_root"

echo "=== [2/6] Bootstrapping Alpine 3.21 aarch64 + Mesa (Adreno 740) + Weston + WiFi/BT tools ==="
chroot "$ALPINE_CHROOT" /usr/bin/env -i \
    HOME=/root TERM=xterm \
    PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin \
    http_proxy=http://127.0.0.1:1082 https_proxy=http://127.0.0.1:1082 \
    /bin/sh -eu -c '
R=/mnt/pmos_root
mkdir -p "$R/etc/apk"
cp /etc/apk/repositories "$R/etc/apk/repositories"
cp -a /etc/apk/keys "$R/etc/apk/"
cp /etc/resolv.conf "$R/etc/resolv.conf" 2>/dev/null || printf "nameserver 8.8.8.8\n" > "$R/etc/resolv.conf"

apk --root "$R" --initdb --no-cache add \
    alpine-base busybox openrc kmod eudev \
    dropbear openssh-sftp-server iproute2 \
    wpa_supplicant iw wireless-tools bluez \
    i2c-tools evtest pciutils usbutils htop \
    mesa-dri-gallium mesa-egl mesa-gles mesa-vulkan-freedreno mesa-demos libdrm \
    seatd weston weston-backend-drm weston-shell-desktop weston-terminal font-dejavu
'

umount "$ALPINE_CHROOT/mnt/pmos_root"

echo "=== [3/6] Installing Phase 3A Kernel Modules ==="
if [ -d "$MODS/lib/modules" ]; then
    mkdir -p "$ROOT/lib/modules"
    cp -a "$MODS/lib/modules/"* "$ROOT/lib/modules/"
    # Run depmod inside rootfs so modprobe resolves dependencies cleanly
    KVER=$(ls "$ROOT/lib/modules" | head -n 1)
    chroot "$ALPINE_CHROOT" /usr/bin/env -i PATH=/sbin:/bin:/usr/sbin:/usr/bin \
        sh -c "mkdir -p /mnt/pmos_root"
    mount -o bind "$ROOT" "$ALPINE_CHROOT/mnt/pmos_root"
    chroot "$ALPINE_CHROOT" depmod -b /mnt/pmos_root "$KVER"
    umount "$ALPINE_CHROOT/mnt/pmos_root"
    echo "Installed kernel modules for $KVER"
else
    echo "WARNING: $MODS/lib/modules not found!"
fi

echo "=== [4/6] Installing Device Proprietary Firmware from /vendor/firmware ==="
FW="$ROOT/lib/firmware"
mkdir -p "$FW/qcom/sm8550/samsung/b5q" \
         "$FW/ath11k/WCN6855/hw2.0" \
         "$FW/ath11k/WCN6855/hw2.1" \
         "$FW/qca" \
         "$FW/tsp_synaptics"

# Adreno 740 GPU + GMU + ZAP shader
cp -f /vendor/firmware/a740_sqe.fw "$FW/qcom/a740_sqe.fw"
cp -f /vendor/firmware/gmu_gen70200.bin "$FW/qcom/gmu_gen70200.bin"
cp -f /vendor/firmware/a740_zap.mbn "$FW/qcom/sm8550/samsung/b5q/a740_zap.mbn"
cp -f /vendor/firmware/a740_zap.mbn "$FW/qcom/sm8550/a740_zap.mbn"

# WCN6855 / QCA6490 Wi-Fi 6E (populate both hw2.0 and hw2.1 paths + board.bin fallback)
for d in "$FW/ath11k/WCN6855/hw2.0" "$FW/ath11k/WCN6855/hw2.1"; do
    cp -f /vendor/firmware/qca6490/amss20.bin "$d/amss.bin"
    cp -f /vendor/firmware/qca6490/m3.bin "$d/m3.bin"
    cp -f /vendor/firmware/qca6490/regdb.bin "$d/regdb.bin"
    cp -f /vendor/firmware/qca6490/bdwlan.elf "$d/board.bin"
    cp -f /vendor/firmware/qca6490/bdwlan.elf "$d/bdwlan.elf"
done

# WCN6855 Bluetooth 5.3
cp -f /vendor/firmware/hpbtfw21.tlv "$FW/qca/hpbtfw21.tlv"
cp -f /vendor/firmware/hpnv21* "$FW/qca/"

# Main Screen Synaptics S3916A Touchscreen
cp -f /vendor/firmware/tsp_synaptics/s3916a_b5.bin "$FW/tsp_synaptics/s3916a_b5.bin"

# Regulatory database (if available in Alpine chroot or skip gracefully)
[ -f /vendor/firmware/qca6490/regdb.bin ] && cp -f /vendor/firmware/qca6490/regdb.bin "$FW/regulatory.db" 2>/dev/null || true

echo "=== [5/6] Configuring SSH, Networking, Seatd, Weston, and /sbin/b5q-init ==="
echo "b5q-alpine" > "$ROOT/etc/hostname"
printf "127.0.0.1\tlocalhost b5q-alpine\n::1\t\tlocalhost b5q-alpine\n" > "$ROOT/etc/hosts"
printf "nameserver 8.8.8.8\nnameserver 1.1.1.1\n" > "$ROOT/etc/resolv.conf"

# Pre-authorize Mac's SSH public key for passwordless root SSH + pre-generate host key
mkdir -p "$ROOT/root/.ssh" "$ROOT/etc/dropbear" "$ROOT/var/lib/misc" "$ROOT/run/udev"
cat <<'EOF' > "$ROOT/root/.ssh/authorized_keys"
ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIGbB4xNII5axJJChOkbS+JBosBVWldUlFbO8s/9RQttl jerryou_cn@outlook.com
EOF
chmod 700 "$ROOT/root/.ssh"
chmod 600 "$ROOT/root/.ssh/authorized_keys"
mount -o bind /dev "$ROOT/dev"
mount -t proc proc "$ROOT/proc"
chroot "$ROOT" /usr/bin/env -i PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin \
    http_proxy=http://127.0.0.1:1082 https_proxy=http://127.0.0.1:1082 \
    /bin/sh -eu -c '
    apk fix --no-cache >/dev/null 2>&1 || true
    fc-cache -f 2>/dev/null || true
    /usr/bin/dropbearkey -t ed25519 -f /etc/dropbear/dropbear_ed25519_host_key >/dev/null
    chmod 600 /etc/dropbear/dropbear_ed25519_host_key
'
umount "$ROOT/proc" 2>/dev/null || true
umount "$ROOT/dev" 2>/dev/null || true

# udhcpd config for USB CDC-NCM
cat <<'EOF' > "$ROOT/etc/udhcpd.conf"
start 172.16.42.2
end 172.16.42.10
interface usb0
lease_file /var/lib/misc/udhcpd.leases
opt subnet 255.255.255.0
opt router 172.16.42.1
EOF
touch "$ROOT/var/lib/misc/udhcpd.leases"

# Weston config for 1080x2640 SimpleDRM + Adreno 740 headless render
mkdir -p "$ROOT/root/.config"
cat <<'EOF' > "$ROOT/root/.config/weston.ini"
[core]
idle-time=0

[shell]
background-color=0xff102030
panel-position=top
locking=false
EOF

# Create clean PID 1 init script (/sbin/b5q-init)
cat <<'EOF' > "$ROOT/sbin/b5q-init"
#!/bin/sh
export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
export HOME=/root
export TERM=linux

# Mount virtual filesystems
mountpoint -q /proc || mount -t proc proc /proc
mountpoint -q /sys || mount -t sysfs sysfs /sys
mountpoint -q /dev || mount -t devtmpfs devtmpfs /dev
mkdir -p /dev/pts /dev/shm /run /tmp /sys/kernel/config /sys/fs/cgroup
mountpoint -q /dev/pts || mount -t devpts devpts /dev/pts
mountpoint -q /dev/shm || mount -t tmpfs tmpfs /dev/shm
mountpoint -q /run || mount -t tmpfs tmpfs /run
mountpoint -q /tmp || mount -t tmpfs tmpfs /tmp
mountpoint -q /sys/kernel/config || mount -t configfs configfs /sys/kernel/config 2>/dev/null || true

hostname -F /etc/hostname 2>/dev/null || hostname b5q-alpine

exec </dev/tty0 >/dev/tty0 2>&1

# Bring up loopback + USB CDC-NCM immediately so SSH is reachable in <1s
ip link set lo up 2>/dev/null || ifconfig lo 127.0.0.1 up
ip addr add 172.16.42.1/24 dev usb0 2>/dev/null || ifconfig usb0 172.16.42.1 netmask 255.255.255.0 up 2>/dev/null || true
ip link set usb0 up 2>/dev/null || true
udhcpd /etc/udhcpd.conf 2>/dev/null &
dropbear -R -B -p 22 2>/dev/null &

# Start udevd & seatd for DRM/input/Wayland
/sbin/udevd --daemon 2>/dev/null || true
udevadm trigger --action=add 2>/dev/null || true
seatd -g video -u root 2>/dev/null &

echo ""
echo "=== [Alpine 3.21 Persistent Rootfs on /dev/sda29] ==="
echo "[NET]  USB CDC-NCM UP -> 172.16.42.1 (Dropbear SSH :22)"

# Load Phase 3A kernel modules with /lib/firmware already mounted
echo "[MODS] Loading Adreno 740 GPU (msm)..."
modprobe msm 2>&1 || echo "  (msm load warning)"

echo "[MODS] Loading Wi-Fi 6E power sequencing + ath11k_pci..."
modprobe pci-pwrctrl-pwrseq 2>&1 || modprobe pci_pwrctrl_pwrseq 2>&1 || true
sleep 1
modprobe ath11k_pci 2>&1 || echo "  (ath11k_pci load warning)"

echo "[MODS] Loading Bluetooth 5.3 (hci_uart + btqca)..."
modprobe btqca 2>&1 || true
modprobe hci_uart 2>&1 || echo "  (hci_uart load warning)"

sleep 2

echo ""
echo "================ HARDWARE STATUS ================"
echo "[CPU]  $(nproc) cores | $(uname -r)"
echo "[RAM]  $(free -m | awk '/Mem:/ {print $3 "MB / " $2 "MB"}')"
echo "[DISK] $(df -h / | awk 'NR==2 {print $3 " / " $2 " (" $4 " free)"}')"
echo "[DRM]  $(ls /dev/dri 2>/dev/null | tr '\n' ' ')"
echo "[PCI]  $(lspci 2>/dev/null | head -n 3 || echo 'none')"
echo "[WIFI] $(iw dev 2>/dev/null | awk '/Interface/ {print $2}' | tr '\n' ' ')"
echo "[BT]   $(ls /sys/class/bluetooth 2>/dev/null | tr '\n' ' ')"
for ps in /sys/class/power_supply/*; do
    [ -d "$ps" ] || continue
    cap=$(cat "$ps/capacity" 2>/dev/null || echo "?")
    st=$(cat "$ps/status" 2>/dev/null || echo "?")
    volt=$(cat "$ps/voltage_now" 2>/dev/null || echo "?")
    echo "[PWR]  $(basename "$ps"): status=$st cap=${cap}% uV=$volt"
done
echo "================================================="
echo "[READY] SSH in from Mac with ./scripts/ssh_b5q.sh"
echo ""

while true; do
    /bin/sh
    sleep 1
done
EOF
chmod 755 "$ROOT/sbin/b5q-init"

echo "=== [6/6] Rootfs Summary ==="
du -sh "$ROOT"
df -h /cache
echo "=== PHASE 3A PERSISTENT ROOTFS DEPLOYED ==="
