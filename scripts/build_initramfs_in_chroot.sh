#!/bin/bash
# Builds mainline Linux Phase 2 M2 initramfs.cpio.gz with:
# - Full Alpine busybox (with udhcpd, telnetd, mount, losetup, dd, etc.) + musl libc
# - Dropbear SSH server with Mac's ~/.ssh/id_ed25519.pub pre-authorized for passwordless root SSH
# - macOS-native USB CDC-NCM Ethernet (172.16.42.1/24 + DHCP server -> Mac gets 172.16.42.2)
# - Auto-clear misc (/dev/sda10) boot-recovery flag so normal reboot returns cleanly to Android 14
set -e
export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin

WORK=/root/work/initramfs_root
OUT=/root/work/initramfs.cpio.gz

rm -rf "$WORK"
mkdir -p "$WORK/bin" "$WORK/sbin" "$WORK/lib" "$WORK/usr/bin" "$WORK/usr/sbin" "$WORK/usr/lib" \
         "$WORK/etc/dropbear" "$WORK/root/.ssh" "$WORK/proc" "$WORK/sys" "$WORK/dev" "$WORK/tmp" "$WORK/run" "$WORK/mnt" "$WORK/var/lib/misc"

copy_with_libs() {
    local bin="$1"
    local dst="$2"
    cp -L "$bin" "$WORK$dst"
    chmod 755 "$WORK$dst"
    for lib in $(ldd "$bin" 2>/dev/null | awk '{for(i=1;i<=NF;i++) if($i ~ /^\//) print $i}'); do
        if [ -f "$lib" ]; then
            mkdir -p "$WORK$(dirname "$lib")"
            cp -Ln "$lib" "$WORK$lib" 2>/dev/null || true
        fi
    done
}

# Copy full Alpine busybox (includes udhcpd, telnetd, etc.) and dropbear SSH
copy_with_libs /bin/busybox /bin/busybox
copy_with_libs /usr/sbin/dropbear /usr/sbin/dropbear
[ -x /usr/bin/dropbearkey ] && copy_with_libs /usr/bin/dropbearkey /usr/bin/dropbearkey || true
[ -x /sbin/blkid ] && copy_with_libs /sbin/blkid /sbin/blkid || true
[ -x /bin/lsblk ] && copy_with_libs /bin/lsblk /bin/lsblk || true

# Install all busybox applet symlinks
for app in $("$WORK/bin/busybox" --list 2>/dev/null || /bin/busybox --list); do
    if [ ! -e "$WORK/bin/$app" ] && [ ! -e "$WORK/sbin/$app" ] && [ ! -e "$WORK/usr/bin/$app" ] && [ ! -e "$WORK/usr/sbin/$app" ]; then
        ln -sf /bin/busybox "$WORK/bin/$app"
    fi
done

# Pre-generate Dropbear Ed25519 host key so boot is instant
if [ -x /usr/bin/dropbearkey ]; then
    /usr/bin/dropbearkey -t ed25519 -f "$WORK/etc/dropbear/dropbear_ed25519_host_key" >/dev/null
    chmod 600 "$WORK/etc/dropbear/dropbear_ed25519_host_key"
fi

# Pre-authorize Mac's SSH public key for passwordless root SSH
cat <<'EOF' > "$WORK/root/.ssh/authorized_keys"
ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIGbB4xNII5axJJChOkbS+JBosBVWldUlFbO8s/9RQttl jerryou_cn@outlook.com
EOF
chmod 700 "$WORK/root/.ssh"
chmod 600 "$WORK/root/.ssh/authorized_keys"

cat <<'EOF' > "$WORK/etc/passwd"
root::0:0:root:/root:/bin/sh
EOF
cat <<'EOF' > "$WORK/etc/group"
root:x:0:
EOF
cat <<'EOF' > "$WORK/etc/shells"
/bin/sh
/bin/ash
EOF

# udhcpd config: hand 172.16.42.2 to Mac automatically over USB CDC-NCM
cat <<'EOF' > "$WORK/etc/udhcpd.conf"
start 172.16.42.2
end 172.16.42.10
interface usb0
lease_file /var/lib/misc/udhcpd.leases
opt subnet 255.255.255.0
opt router 172.16.42.1
EOF
touch "$WORK/var/lib/misc/udhcpd.leases"

cat <<'EOF' > "$WORK/init"
#!/bin/sh
export PATH=/bin:/sbin:/usr/bin:/usr/sbin
export HOME=/root

mount -t proc proc /proc
mount -t sysfs sysfs /sys
mount -t devtmpfs devtmpfs /dev 2>/dev/null || true
mkdir -p /dev/pts /dev/shm /sys/kernel/config
mount -t devpts devpts /dev/pts 2>/dev/null || true
mount -t configfs configfs /sys/kernel/config 2>/dev/null || true

exec </dev/tty0 >/dev/tty0 2>&1

clear
echo "=================================================="
echo "  Mainline Linux 7.3.5 on Z Flip 5 (SM-F7310 b5q) "
echo "  Phase 2 M2: UFS 4.0 + USB CDC-NCM + Dropbear SSH"
echo "=================================================="
echo ""
echo "[Kernel] $(uname -a)"
echo "[Memory] $(grep MemTotal /proc/meminfo)"
echo ""

# Wait up to 4s for UFS 4.0 (/dev/sda) and USB UDC to settle
for i in 1 2 3 4; do
    [ -n "$(ls /sys/class/udc 2>/dev/null)" ] && [ -b /dev/sda10 ] && break
    sleep 1
done

# Auto-clear boot-recovery flag in misc (/dev/sda10) so normal reboot returns to Android 14
if [ -b /dev/sda10 ]; then
    MISC_HDR=$(dd if=/dev/sda10 bs=13 count=1 2>/dev/null || true)
    if [ "$MISC_HDR" = "boot-recovery" ]; then
        dd if=/dev/zero of=/dev/sda10 bs=4096 count=1 2>/dev/null && sync
        echo "[MISC] Cleared boot-recovery flag on /dev/sda10 -> reboot returns to Android 14!"
    else
        echo "[MISC] /dev/sda10 clean (no boot-recovery flag)"
    fi
fi

echo "[UFS 4.0 Block Devices]"
ls /dev/sd* 2>/dev/null | head -n 16 || echo "(no /dev/sd* found)"
echo ""

# Set up macOS-native USB CDC-NCM Ethernet gadget
G=/sys/kernel/config/usb_gadget/g1
if [ -d /sys/kernel/config/usb_gadget ]; then
    mkdir -p "$G"
    echo 0x18d1 > "$G/idVendor"
    echo 0xd001 > "$G/idProduct"
    echo 0x0200 > "$G/bcdUSB"
    mkdir -p "$G/strings/0x409"
    echo "b5q-mainline" > "$G/strings/0x409/serialnumber"
    echo "Samsung-Mainline" > "$G/strings/0x409/manufacturer"
    echo "Z Flip 5 Mainline Linux (b5q)" > "$G/strings/0x409/product"
    mkdir -p "$G/configs/c.1/strings/0x409"
    echo "NCM" > "$G/configs/c.1/strings/0x409/configuration"
    echo 500 > "$G/configs/c.1/MaxPower"

    mkdir -p "$G/functions/ncm.usb0"
    ln -sf "$G/functions/ncm.usb0" "$G/configs/c.1/"

    UDC=$(ls /sys/class/udc 2>/dev/null | head -n 1)
    if [ -n "$UDC" ]; then
        echo "$UDC" > "$G/UDC"
        sleep 1
        ifconfig lo 127.0.0.1 up
        ifconfig usb0 172.16.42.1 netmask 255.255.255.0 up
        udhcpd /etc/udhcpd.conf &
        dropbear -R -B -p 22 &
        telnetd -l /bin/sh -b 0.0.0.0 &
        echo "[NET] USB CDC-NCM UP: 172.16.42.1 (DHCP + SSH :22 + Telnet :23)"
        echo "[SSH] Run on Mac: ssh root@172.16.42.1"
    else
        echo "[USB] ERROR: No UDC found!"
    fi
fi

echo ""
echo "[Ready] Mainline Linux 7.3.5 + UFS 4.0 + SSH live!"
while true; do
    /bin/sh
    sleep 1
done
EOF
chmod 755 "$WORK/init"

cd "$WORK"
find . | cpio -H newc -o 2>/dev/null | gzip -9 > "$OUT"
ls -lh "$OUT"
