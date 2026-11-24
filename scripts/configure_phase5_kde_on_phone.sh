#!/bin/sh
# Configures /mnt/linux (/dev/sda37, 328 GiB) for Phase 5:
# - System user 'messagebus' (for D-Bus) + desktop user 'jerry' (pw: 1234) with sudo/doas & subuid/subgid
# - 4 GiB swapfile on UFS 4.0
# - NetworkManager with preconfigured CandyTime_307_2.4G (fixed MAC) + unmanaged usb0
# - SDDM 0.21 Wayland autologin (proper PAM + pam_elogind session for KWin 6 / Polkit / NM / Flatpak)
# - Flathub remote for Flatpak + Podman/Distrobox rootless setup
# - /sbin/b5q-desktop-init launching KDE Plasma 6 (Plasma Mobile default, switchable to Plasma Desktop)
set -e

ROOT=/mnt/linux

echo "[1/7] Setting up hostname, timezone, and 4 GiB swapfile..."
echo "b5q-plasma" > "$ROOT/etc/hostname"
ln -sf /usr/share/zoneinfo/Asia/Shanghai "$ROOT/etc/localtime" 2>/dev/null || true
if [ ! -f "$ROOT/swapfile" ]; then
    dd if=/dev/zero of="$ROOT/swapfile" bs=1M count=4096 status=none
    chmod 600 "$ROOT/swapfile"
    mkswap "$ROOT/swapfile" >/dev/null
fi

echo "[2/7] Creating system 'messagebus' & desktop user 'jerry' (pw: 1234)..."
chroot "$ROOT" /bin/sh -c '
    if ! id messagebus >/dev/null 2>&1; then
        addgroup -S messagebus 2>/dev/null || true
        adduser -S -D -H -h /dev/null -s /sbin/nologin -G messagebus messagebus 2>/dev/null || true
    fi
    for helper in /usr/libexec/dbus-daemon-launch-helper /usr/lib/dbus-1/dbus-daemon-launch-helper; do
        if [ -f "$helper" ]; then
            chown root:messagebus "$helper" 2>/dev/null || true
            chmod 4750 "$helper" 2>/dev/null || true
        fi
    done

    for g in wheel video input audio seat plugdev netdev lp dialout render; do
        addgroup "$g" 2>/dev/null || true
    done
    if ! id jerry >/dev/null 2>&1; then
        adduser -D -s /bin/bash -G users jerry
    fi
    echo "jerry:1234" | chpasswd
    echo "root:1234" | chpasswd
    for g in wheel video input audio seat plugdev netdev render; do
        adduser jerry "$g" 2>/dev/null || true
    done
    mkdir -p /etc/sudoers.d /etc/doas.d
    echo "%wheel ALL=(ALL:ALL) NOPASSWD: ALL" > /etc/sudoers.d/wheel
    chmod 440 /etc/sudoers.d/wheel
    echo "permit nopass :wheel" > /etc/doas.d/wheel.conf
    grep -q "^jerry:" /etc/subuid 2>/dev/null || echo "jerry:100000:65536" >> /etc/subuid
    grep -q "^jerry:" /etc/subgid 2>/dev/null || echo "jerry:100000:65536" >> /etc/subgid
    mkdir -p /home/jerry/.ssh
    cp -f /root/.ssh/authorized_keys /home/jerry/.ssh/authorized_keys 2>/dev/null || true
    chown -R jerry:users /home/jerry/.ssh
    chmod 700 /home/jerry/.ssh
    chmod 600 /home/jerry/.ssh/authorized_keys 2>/dev/null || true
'

echo "[3/7] Configuring NetworkManager (keep usb0 for USB SSH, autoconnect CandyTime_307_2.4G)..."
mkdir -p "$ROOT/etc/NetworkManager/conf.d" "$ROOT/etc/NetworkManager/system-connections"
cat <<'EOF' > "$ROOT/etc/NetworkManager/conf.d/10-b5q.conf"
[keyfile]
unmanaged-devices=interface-name:usb0

[device]
wifi.scan-rand-mac-address=no

[connection]
wifi.powersave=2
EOF

cat <<'EOF' > "$ROOT/etc/NetworkManager/system-connections/CandyTime_307_2.4G.nmconnection"
[connection]
id=CandyTime_307_2.4G
uuid=6f2d8c9a-5b3e-4f10-a123-7310b5024001
type=wifi
autoconnect=true
autoconnect-priority=10

[wifi]
mode=infrastructure
ssid=CandyTime_307_2.4G
cloned-mac-address=02:b5:73:10:00:01

[wifi-security]
key-mgmt=wpa-psk
psk=QJBH888888

[ipv4]
method=auto

[ipv6]
method=auto
EOF
chmod 600 "$ROOT/etc/NetworkManager/system-connections/CandyTime_307_2.4G.nmconnection"

echo "[4/7] Configuring SDDM Wayland autologin (PAM + elogind session)..."
mkdir -p "$ROOT/etc/sddm.conf.d" "$ROOT/var/lib/sddm"
cat <<'EOF' > "$ROOT/etc/sddm.conf.d/10-b5q-autologin.conf"
[General]
DisplayServer=wayland
GreeterEnvironment=QT_WAYLAND_SHELL_INTEGRATION=layer-shell,KWIN_DRM_DEVICES=/dev/dri/card0,KWIN_COMPOSE=Q
InputMethod=maliit-keyboard

[Wayland]
CompositorCommand=kwin_wayland --drm --no-lockscreen --no-global-shortcuts --locale1

[Autologin]
User=jerry
Session=plasma-mobile
Relogin=true
EOF
chroot "$ROOT" chown -R sddm:video /var/lib/sddm

echo "[5/7] Configuring KDE Plasma 6 Wayland env, Maliit keyboard, fonts, and Flathub..."
mkdir -p "$ROOT/home/jerry/.config" "$ROOT/etc/profile.d"
cat <<'EOF' > "$ROOT/etc/profile.d/b5q-wayland.sh"
export XDG_SESSION_TYPE=wayland
export QT_QPA_PLATFORM=wayland
export GDK_BACKEND=wayland
export MOZ_ENABLE_WAYLAND=1
export SDL_VIDEODRIVER=wayland
export CLUTTER_BACKEND=wayland
export KWIN_DRM_DEVICES=/dev/dri/card0
export KWIN_COMPOSE=Q
export LANG=zh_CN.UTF-8
export LC_CTYPE=C.UTF-8
EOF

cat <<'EOF' > "$ROOT/home/jerry/.config/kwinrc"
[Wayland]
InputMethod[$e]=/usr/share/applications/com.github.maliit.keyboard.desktop
VirtualKeyboardEnabled=true

[Plugins]
blurEnabled=false
contrastEnabled=false
slideEnabled=false

[Compositing]
AnimationSpeed=3

[Effect-overview]
BorderActivate=9
EOF

cat <<'EOF' > "$ROOT/home/jerry/.config/kdeglobals"
[General]
font=Noto Sans CJK SC,10,-1,5,400,0,0,0,0,0,0,0,0,0,0,1
fixed=DejaVu Sans Mono,10,-1,5,400,0,0,0,0,0,0,0,0,0,0,1
EOF

cat <<'EOF' > "$ROOT/home/jerry/.config/kscreenlockerrc"
[Daemon]
Autolock=false
LockOnResume=false
EOF

cat <<'EOF' > "$ROOT/home/jerry/.config/powerdevilrc"
[AC][Display]
DimDisplayWhenIdle=false
TurnOffDisplayWhenIdle=false
TurnOffDisplayIdleTimeoutSec=0
TurnOffDisplayIdleTimeoutWhenLockedSec=0

[AC][SuspendAndShutdown]
AutoSuspendAction=0

[Battery][Display]
DimDisplayWhenIdle=false
TurnOffDisplayWhenIdle=false
TurnOffDisplayIdleTimeoutSec=0
TurnOffDisplayIdleTimeoutWhenLockedSec=0

[Battery][SuspendAndShutdown]
AutoSuspendAction=0

[LowBattery][Display]
DimDisplayWhenIdle=false
TurnOffDisplayWhenIdle=false
TurnOffDisplayIdleTimeoutSec=0
TurnOffDisplayIdleTimeoutWhenLockedSec=0

[LowBattery][SuspendAndShutdown]
AutoSuspendAction=0
EOF
chmod 600 "$ROOT/home/jerry/.config/powerdevilrc"

mkdir -p "$ROOT/home/jerry/.config/autostart"
for _desk in org.kde.discover.notifier baloo_file; do
    printf '[Desktop Entry]\nHidden=true\n' > "$ROOT/home/jerry/.config/autostart/${_desk}.desktop"
done
printf '[Basic Settings]\nIndexing-Enabled=false\n' > "$ROOT/home/jerry/.config/baloofilerc"

chroot "$ROOT" chown -R jerry:users /home/jerry/.config

# Configure Flatpak Flathub repository (system-wide)
chroot "$ROOT" /bin/sh -c '
    flatpak remote-add --if-not-exists flathub https://dl.flathub.org/repo/flathub.flatpakrepo 2>/dev/null || true
'

echo "[6/7] Installing helper commands b5q-plasma-mode & b5q-reboot..."
echo "mobile" > "$ROOT/etc/b5q-session"
cat <<'EOF' > "$ROOT/usr/local/bin/b5q-plasma-mode"
#!/bin/sh
MODE="${1:-status}"
CONF="/etc/sddm.conf.d/10-b5q-autologin.conf"

kill_user_procs() {
    SIG="$1"
    for p in $(ps -o pid,user | awk '$2=="jerry" {print $1}'); do
        kill -"$SIG" "$p" 2>/dev/null || true
    done
}

restart_session() {
    # 1. Freeze sddm so it neither exits (triggering PID 1's 2s respawn)
    #    nor spawns an intermediate autologin session while we tear down.
    killall -STOP sddm 2>/dev/null || true
    # 2. Gracefully stop sddm-helper and all user session processes (incl. kwin_wayland_wr / dbus / portals).
    killall -TERM sddm-helper 2>/dev/null || true
    kill_user_procs TERM
    for i in 1 2 3 4 5; do
        pidof kwin_wayland sddm-helper >/dev/null 2>&1 || break
        sleep 1
    done
    killall -KILL sddm-helper 2>/dev/null || true
    kill_user_procs KILL
    loginctl terminate-user jerry 2>/dev/null || true
    rm -f /run/user/1000/wayland-0 /run/user/1000/wayland-0.lock 2>/dev/null || true
    sleep 1
    # 3. Terminate frozen sddm; PID 1 supervisor cleanly spawns one fresh sddm after 2s.
    killall -KILL sddm 2>/dev/null || true
}

case "$MODE" in
    mobile)
        echo "mobile" > /etc/b5q-session
        sed -i 's/^Session=.*/Session=plasma-mobile/' "$CONF"
        echo "[b5q] Switched KDE Plasma 6 session mode to: plasma-mobile"
        restart_session
        ;;
    desktop)
        echo "desktop" > /etc/b5q-session
        sed -i 's/^Session=.*/Session=plasma/' "$CONF"
        echo "[b5q] Switched KDE Plasma 6 session mode to: plasma (desktop)"
        restart_session
        ;;
    restart)
        echo "[b5q] Restarting SDDM + KDE Plasma 6 session..."
        restart_session
        ;;
    *)
        echo "Current mode: $(cat /etc/b5q-session 2>/dev/null || echo mobile)"
        echo "Usage: b5q-plasma-mode [mobile|desktop|restart]"
        ;;
esac
EOF
chmod 755 "$ROOT/usr/local/bin/b5q-plasma-mode"

cat <<'EOF' > "$ROOT/usr/local/bin/b5q-reboot"
#!/bin/sh
# Usage: b5q-reboot [linux|android|download]
TARGET="${1:-linux}"
sync
case "$TARGET" in
    linux|recovery)
        exec python3 -c 'import ctypes; l=ctypes.CDLL(None,use_errno=True); r=l.syscall(ctypes.c_long(142),ctypes.c_uint(0xfee1dead),ctypes.c_uint(0x28121969),ctypes.c_uint(0xa1b2c3d4),ctypes.c_char_p(b"recovery")); print(r, ctypes.get_errno())'
        ;;
    download|bootloader)
        exec python3 -c 'import ctypes; l=ctypes.CDLL(None,use_errno=True); r=l.syscall(ctypes.c_long(142),ctypes.c_uint(0xfee1dead),ctypes.c_uint(0x28121969),ctypes.c_uint(0xa1b2c3d4),ctypes.c_char_p(b"bootloader")); print(r, ctypes.get_errno())'
        ;;
    android|normal)
        exec /sbin/reboot -f
        ;;
    *)
        echo "Usage: b5q-reboot [linux|android|download]"
        exit 1
        ;;
esac
EOF
chmod 755 "$ROOT/usr/local/bin/b5q-reboot"
ln -sf /usr/local/bin/b5q-plasma-mode "$ROOT/usr/bin/b5q-plasma-mode"
ln -sf /usr/local/bin/b5q-reboot "$ROOT/usr/bin/b5q-reboot"

cat <<'EOF' > "$ROOT/usr/local/bin/b5q-power-sync"
#!/bin/sh
# Trigger deferred interconnect & RPMh power domain sync_state after drivers load,
# releasing ABL boot-time INT_MAX DDR/bus votes so LPDDR5X and CX/MX scale dynamically.
for f in \
    /sys/devices/platform/interconnect-*/state_synced \
    /sys/devices/platform/soc@0/*.interconnect/state_synced \
    /sys/devices/platform/soc@0/17a00000.rsc/17a00000.rsc:power-controller/state_synced
do
    [ -e "$f" ] || continue
    if [ "$(cat "$f" 2>/dev/null)" = "0" ]; then
        printf "1" > "$f" 2>/dev/null && echo "synced: $f" || echo "skip/err: $f"
    fi
done
EOF
chmod 755 "$ROOT/usr/local/bin/b5q-power-sync"

mkdir -p "$ROOT/usr/local/src"
cat <<'EOF' > "$ROOT/usr/local/src/b5q-cover-off.c"
#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <linux/gpio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define TLMM_DEV_NODE "f100000.pinctrl"
#define COVER_RESET_OFFSET 42

/* Hold the cover OLED (B5_S6E3FC5, TLMM GPIO 42) in hardware reset (LOW). */
static int find_tlmm_dev(char *out_path, size_t out_sz) {
	DIR *d = opendir("/sys/bus/gpio/devices");
	struct dirent *ent;

	if (!d)
		return -1;

	while ((ent = readdir(d)) != NULL) {
		char sys_path[PATH_MAX];
		char *real_path;

		if (strncmp(ent->d_name, "gpiochip", 8) != 0)
			continue;
		snprintf(sys_path, sizeof(sys_path), "/sys/bus/gpio/devices/%s", ent->d_name);
		real_path = realpath(sys_path, NULL);
		if (!real_path)
			continue;
		if (strstr(real_path, TLMM_DEV_NODE)) {
			free(real_path);
			snprintf(out_path, out_sz, "/dev/%s", ent->d_name);
			closedir(d);
			return 0;
		}
		free(real_path);
	}
	closedir(d);
	errno = ENODEV;
	return -1;
}

int main(int argc, char **argv) {
	int hold = (argc > 1 && strcmp(argv[1], "--hold") == 0);
	char dev_path[512];
	int chip_fd;
	struct gpio_v2_line_request req;
	struct gpio_v2_line_values vals;

	if (find_tlmm_dev(dev_path, sizeof(dev_path)) < 0) {
		perror("find_tlmm_dev");
		return 1;
	}

	chip_fd = open(dev_path, O_RDWR | O_CLOEXEC);
	if (chip_fd < 0) {
		perror("open gpiochip");
		return 1;
	}

	memset(&req, 0, sizeof(req));
	req.offsets[0] = COVER_RESET_OFFSET;
	req.num_lines = 1;
	snprintf(req.consumer, sizeof(req.consumer), "b5q-cover-off");
	req.config.flags = GPIO_V2_LINE_FLAG_OUTPUT;
	req.config.num_attrs = 1;
	req.config.attrs[0].attr.id = GPIO_V2_LINE_ATTR_ID_OUTPUT_VALUES;
	req.config.attrs[0].attr.values = 0;
	req.config.attrs[0].mask = 1;

	if (ioctl(chip_fd, GPIO_V2_GET_LINE_IOCTL, &req) < 0) {
		perror("GPIO_V2_GET_LINE_IOCTL");
		close(chip_fd);
		return 1;
	}

	memset(&vals, 0, sizeof(vals));
	vals.mask = 1;
	vals.bits = 0;
	if (ioctl(req.fd, GPIO_V2_LINE_SET_VALUES_IOCTL, &vals) < 0) {
		perror("GPIO_V2_LINE_SET_VALUES_IOCTL");
		close(req.fd);
		close(chip_fd);
		return 1;
	}

	if (hold)
		pause();

	close(req.fd);
	close(chip_fd);
	return 0;
}
EOF
chroot "$ROOT" /bin/sh -c '
    if command -v gcc >/dev/null 2>&1; then
        gcc -O2 -Wall -Wextra -o /usr/local/bin/b5q-cover-off /usr/local/src/b5q-cover-off.c
        chmod 755 /usr/local/bin/b5q-cover-off
    fi
'

echo "[7/7] Installing verified /sbin/b5q-desktop-init..."
cat <<'EOF' > "$ROOT/sbin/b5q-desktop-init"
#!/bin/sh
export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
export HOME=/root
export TERM=linux

mountpoint -q /proc || mount -t proc proc /proc
mountpoint -q /sys || mount -t sysfs sysfs /sys
mountpoint -q /dev || mount -t devtmpfs devtmpfs /dev
mkdir -p /dev/pts /dev/shm /run /tmp /sys/kernel/config /sys/fs/cgroup
mountpoint -q /dev/pts || mount -t devpts devpts /dev/pts
mountpoint -q /dev/shm || mount -t tmpfs tmpfs /dev/shm
mountpoint -q /run || mount -t tmpfs tmpfs /run
mountpoint -q /tmp || mount -t tmpfs tmpfs /tmp
mkdir -p /tmp/.X11-unix && chmod 1777 /tmp/.X11-unix
mount --make-rshared / 2>/dev/null || true
mountpoint -q /sys/kernel/config || mount -t configfs configfs /sys/kernel/config 2>/dev/null || true
mountpoint -q /sys/fs/cgroup || mount -t cgroup2 none /sys/fs/cgroup 2>/dev/null || true

hostname -F /etc/hostname 2>/dev/null || hostname b5q-plasma

# Activate 4 GiB swapfile
[ -f /swapfile ] && swapon /swapfile 2>/dev/null || true

# Machine ID for D-Bus / elogind / NetworkManager / Flatpak
[ -s /etc/machine-id ] || /usr/bin/dbus-uuidgen --ensure=/etc/machine-id 2>/dev/null || true

# Show early boot progress on screen
exec </dev/tty0 >/dev/tty0 2>&1

# USB CDC-NCM + Dropbear SSH UP in <1s
ip link set lo up 2>/dev/null || true
ip addr add 172.16.42.1/24 dev usb0 2>/dev/null || true
ip link set usb0 up 2>/dev/null || true
/usr/sbin/dropbear -R -B -p 22 2>/dev/null &

# Start udevd
/sbin/udevd --daemon 2>/dev/null || true
/sbin/udevadm trigger --action=add 2>/dev/null || true
/sbin/udevadm settle --timeout=3 2>/dev/null || true

# Hold cover display (B5_S6E3FC5, TLMM GPIO 42) in hardware reset so it stays off
/usr/local/bin/b5q-cover-off 2>/dev/null || true

echo ""
echo "=== [Z Flip 5 Mainline Linux 7.3.5 | 334 GiB /dev/sda37] ==="
echo "[NET]  USB CDC-NCM 172.16.42.1 | Dropbear SSH :22"

echo "[MODS] Loading Adreno 740 GPU (msm)..."
modprobe msm 2>&1 || true

echo "[MODS] Loading Wi-Fi 6E (ath11k_pci)..."
modprobe ath11k_pci 2>&1 || true

echo "[MODS] Loading Bluetooth 5.3 (hci_uart + btqca)..."
modprobe btqca 2>&1 || true
modprobe hci_uart 2>&1 || true

echo "[MODS] Loading Synaptics S3916A Touchscreen..."
modprobe synaptics_s3916_sec 2>&1 || true
if ! grep -qi "Synaptics S3916A" /proc/bus/input/devices 2>/dev/null; then
    sleep 1
    rmmod synaptics_s3916_sec 2>/dev/null || true
    modprobe synaptics_s3916_sec 2>&1 || true
fi

# Re-trigger udev for newly loaded DRM / input / net devices so elogind/KWin sees them
/sbin/udevadm trigger --action=add --subsystem-match=drm --subsystem-match=input 2>/dev/null || true
/sbin/udevadm settle --timeout=2 2>/dev/null || true

# Start D-Bus (system), elogind, Polkit, UPower, BlueZ, NetworkManager
mkdir -p /run/dbus /run/elogind /run/NetworkManager /var/lib/NetworkManager /var/lib/bluetooth /var/lib/sddm
chown messagebus:messagebus /run/dbus 2>/dev/null || true
chown sddm:video /var/lib/sddm 2>/dev/null || true

echo "[SYS]  Starting D-Bus, elogind, Polkit, UPower, BlueZ, NetworkManager..."
/usr/bin/dbus-daemon --system --nofork --nopidfile >/tmp/dbus.log 2>&1 &
for i in 1 2 3 4 5; do
    [ -S /run/dbus/system_bus_socket ] && break
    sleep 1
done

/usr/libexec/elogind/elogind >/tmp/elogind.log 2>&1 &
sleep 1
/usr/lib/polkit-1/polkitd >/tmp/polkit.log 2>&1 &
/usr/libexec/upowerd >/tmp/upower.log 2>&1 &
/usr/lib/bluetooth/bluetoothd >/tmp/bluetoothd.log 2>&1 &

# Let NetworkManager manage wlan0 (wpa_supplicant via D-Bus)
/usr/sbin/wpa_supplicant -u -s -B >/tmp/wpa_dbus.log 2>&1 || true
/usr/sbin/NetworkManager --no-daemon >/tmp/nm.log 2>&1 &

# Background NTP sync once Wi-Fi comes up
(
    for i in $(seq 1 30); do
        if ip route | grep -q "^default "; then
            /usr/sbin/ntpd -n -q -p ntp.aliyun.com >/dev/null 2>&1 || true
            break
        fi
        sleep 2
    done
) &

echo "[GUI]  Handing off tty1 to SDDM -> KDE Plasma 6 ($(cat /etc/b5q-session 2>/dev/null || echo mobile))..."
echo "[READY] USB SSH: ./scripts/ssh_b5q.sh | User: jerry (pw: 1234)"

# Release tty0/tty1 so SDDM + KWin 6 own VT1 cleanly
( sleep 8; /usr/local/bin/b5q-power-sync >/tmp/b5q-power-sync.log 2>&1 ) &

exec </dev/null >/tmp/b5q-init.log 2>&1

# Supervise SDDM in PID 1 loop (supports instant restart/mode switch via b5q-plasma-mode)
while true; do
    /usr/bin/sddm >/tmp/sddm.log 2>&1
    sleep 2
done
EOF
chmod 755 "$ROOT/sbin/b5q-desktop-init"
sh -n "$ROOT/sbin/b5q-desktop-init" && echo "B5Q_DESKTOP_INIT_SYNTAX_OK"
sync
echo "[+] Phase 5 KDE Plasma 6 configuration complete on /dev/sda37!"
