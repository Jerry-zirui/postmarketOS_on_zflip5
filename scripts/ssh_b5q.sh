#!/usr/bin/env zsh
# Auto-discovers the Z Flip 5 Mainline Linux USB CDC-NCM interface on macOS
# and opens a passwordless root SSH connection over IPv6 link-local (zero DHCP/IPv4 setup needed).
set -euo pipefail

IFACE=$(networksetup -listallhardwareports | awk '/Z Flip 5 .*Linux/{getline; print $2; exit}')
if [[ -z "${IFACE:-}" ]]; then
  echo "[!] Z Flip 5 Mainline Linux USB Ethernet interface not found. Try replugging the USB cable." >&2
  exit 1
fi

SELF_LL=$(ifconfig "$IFACE" | awk '/inet6 fe80::/{print $2}' | cut -d% -f1 | head -n 1)
PEER=$(ping6 -c 2 "ff02::1%${IFACE}" 2>/dev/null \
  | awk '/bytes from fe80::/{print $4}' \
  | sed 's/,$//' \
  | grep -v "${SELF_LL}" \
  | head -n 1)

if [[ -z "${PEER:-}" ]]; then
  echo "[!] Interface ${IFACE} is up, but Z Flip 5 did not respond to multicast ping6." >&2
  exit 1
fi

exec ssh -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null "root@${PEER}" "$@"
