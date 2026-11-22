#!/bin/bash
# Builds a minimal standalone mainline Linux debug initramfs.cpio.gz
# with SimpleFB console banner + macOS-native USB CDC-NCM Ethernet (172.16.42.1) + telnetd.
set -e
export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin

WORK=/root/work/initramfs_root
OUT=/root/work/initramfs.cpio.gz

rm -rf "$WORK"
mkdir -p "$WORK/bin" "$WORK/sbin" "$WORK/etc" "$WORK/proc" "$WORK/sys" "$WORK/dev" "$WORK/tmp" "$WORK/run" "$WORK/usr/bin" "$WORK/usr/sbin" "$WORK/mnt"

# Copy static busybox and create symlinks
cp /bin/busybox.static "$WORK/bin/busybox"
chmod 755 "$WORK/bin/busybox"
for cmd in sh ash ls cat echo mount umount mkdir rm cp mv ln sleep ip ifconfig udhcpd telnetd dmesg uname ps top df free lsmod modprobe mdev grep sed awk head tail vi clear reset poweroff reboot dd sync; do
    ln -sf /bin/busybox "$WORK/bin/$cmd"
done
ln -sf /bin/busybox "$WORK/sbin/init"

# Minimal /etc/passwd and /etc/group for root login
cat <<'EOF' > "$WORK/etc/passwd"
root::0:0:root:/root:/bin/sh
EOF
cat <<'EOF' > "$WORK/etc/group"
root:x:0:
EOF

# udhcpd config so Mac automatically receives 172.16.42.2 over USB CDC-NCM
cat <<'EOF' > "$WORK/etc/udhcpd.conf"
start 172.16.42.2
end 172.16.42.10
interface usb0
opt subnet 255.255.255.0
opt router 172.16.42.1
EOF

# /init script executed as PID 1 by mainline Linux kernel
cat <<'EOF' > "$WORK/init"
#!/bin/sh
export PATH=/bin:/sbin:/usr/bin:/usr/sbin

mount -t proc proc /proc
mount -t sysfs sysfs /sys
mount -t devtmpfs devtmpfs /dev 2>/dev/null || true
mkdir -p /dev/pts /sys/kernel/config
mount -t devpts devpts /dev/pts 2>/dev/null || true
mount -t configfs configfs /sys/kernel/config 2>/dev/null || true

# Redirect output to primary display console (tty0)
exec </dev/tty0 >/dev/tty0 2>&1

clear
echo "=================================================="
echo "  postmarketOS / Mainline Linux on Z Flip 5 (b5q) "
echo "  SM-F7310 Slab-Modded - Phase 2 Early Bring-up   "
echo "=================================================="
echo ""
echo "[Kernel] $(uname -a)"
echo "[Cmdline] $(cat /proc/cmdline)"
echo ""
echo "[Memory]"
head -n 3 /proc/meminfo
echo ""
echo "[Block Devices]"
ls /dev/sd* 2>/dev/null || echo "(no UFS /dev/sd* yet)"
echo ""

# Configure USB Gadget CDC-NCM (native macOS USB Ethernet - zero drivers!)
echo "[USB] Setting up CDC-NCM USB Ethernet gadget (172.16.42.1)..."
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

    # Prefer CDC-NCM (macOS native driver), fallback to ECM
    if mkdir -p "$G/functions/ncm.usb0" 2>/dev/null; then
        ln -sf "$G/functions/ncm.usb0" "$G/configs/c.1/"
        echo "[USB] Created ncm.usb0 function"
    elif mkdir -p "$G/functions/ecm.usb0" 2>/dev/null; then
        ln -sf "$G/functions/ecm.usb0" "$G/configs/c.1/"
        echo "[USB] Created ecm.usb0 function"
    fi

    UDC=$(ls /sys/class/udc 2>/dev/null | head -n 1)
    if [ -n "$UDC" ]; then
        echo "[USB] Binding UDC: $UDC"
        echo "$UDC" > "$G/UDC"
        sleep 1
        ifconfig usb0 172.16.42.1 netmask 255.255.255.0 up
        udhcpd /etc/udhcpd.conf &
        telnetd -l /bin/sh -b 0.0.0.0 &
        echo "[NET] USB Ethernet UP -> telnet 172.16.42.1 (Mac gets 172.16.42.2 via DHCP)"
    else
        echo "[USB] WARNING: No UDC found in /sys/class/udc!"
    fi
else
    echo "[USB] WARNING: configfs usb_gadget not available!"
fi

echo ""
echo "[dmesg tail]"
dmesg | tail -n 15
echo ""
echo "[Ready] Interactive shell running. Hold VolDown+Power 7s to reboot to Android."

while true; do
    /bin/sh
    sleep 1
done
EOF
chmod 755 "$WORK/init"

cd "$WORK"
find . | cpio -H newc -o 2>/dev/null | gzip -9 > "$OUT"
ls -lh "$OUT"
