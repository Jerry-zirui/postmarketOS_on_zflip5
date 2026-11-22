#!/usr/bin/env zsh
# ==============================================================================
# SM-F7310 (samsung-b5q) Stock Partition Emergency Restore Script
# Restores backed-up stock partitions from on-device /data/local/linux_port/backups
# or local Mac ./backups/backup_b5q/ via Root ADB.
# ==============================================================================
set -euo pipefail

PROJECT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
ADB="${PROJECT_DIR}/platform-tools/adb"

TARGET_PART="${1:-recovery}"

if [[ "$TARGET_PART" != "recovery" && "$TARGET_PART" != "boot" && "$TARGET_PART" != "vendor_boot" && "$TARGET_PART" != "dtbo" && "$TARGET_PART" != "all_boot" ]]; then
  echo "Usage: $0 [recovery|boot|vendor_boot|dtbo|all_boot]"
  echo "Default: recovery (since mainline Linux / UniLoader is flashed to recovery)"
  exit 1
fi

echo "[*] Checking ADB connection..."
"$ADB" wait-for-device

restore_one() {
  local part="$1"
  echo "[*] Verifying and restoring partition: ${part}..."
  "$ADB" shell "su -c '
    set -e
    cd /data/local/linux_port/backups
    grep \"  ${part}.img\$\" SHA256SUMS.txt | sha256sum -c -
    dd if=/data/local/linux_port/backups/${part}.img of=/dev/block/by-name/${part} bs=4096
    sync
  '"
  echo "[+] Successfully restored ${part}!"
}

if [[ "$TARGET_PART" == "all_boot" ]]; then
  for p in boot init_boot vendor_boot recovery dtbo vbmeta; do
    restore_one "$p"
  done
else
  restore_one "$TARGET_PART"
fi
