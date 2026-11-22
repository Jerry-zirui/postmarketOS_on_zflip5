#!/usr/bin/env python3
"""
Standalone Android Boot Image v2 packer & unpacker for Samsung SM-F7310 (samsung-b5q).
Matches the exact Header v2 format used by SM-F7310's standalone 104.5 MB `recovery` partition:
- Page Size: 4096
- Kernel Addr: 0x00008000
- Ramdisk Addr: 0x02000000
- Tags Addr: 0x01e00000
- DTB Addr: 0x01f00000
- Board Name: SRPWC09A007
"""
import argparse
import hashlib
import struct
from pathlib import Path

BOOT_MAGIC = b"ANDROID!"
PAGE_SIZE = 4096


def align_up(x: int, align: int = PAGE_SIZE) -> int:
    return (x + align - 1) & ~(align - 1)


def pad_to_page(data: bytes, page_size: int = PAGE_SIZE) -> bytes:
    rem = len(data) % page_size
    return data if rem == 0 else data + b"\x00" * (page_size - rem)


def pack_v2(
    kernel_path: Path,
    ramdisk_path: Path,
    dtb_path: Path,
    output_path: Path,
    cmdline: str = "console=tty0 printk.devkmsg=on",
    board: str = "SRPWC09A007",
    kernel_addr: int = 0x00008000,
    ramdisk_addr: int = 0x02000000,
    second_addr: int = 0x00000000,
    tags_addr: int = 0x01E00000,
    dtb_addr: int = 0x01F00000,
    os_version: int = 0x1A000193,
) -> None:
    kernel_data = kernel_path.read_bytes()
    ramdisk_data = ramdisk_path.read_bytes() if ramdisk_path else b""
    dtb_data = dtb_path.read_bytes()

    cmdline_bytes = cmdline.encode("utf-8")
    main_cmdline = cmdline_bytes[:511].ljust(512, b"\x00")
    extra_cmdline = cmdline_bytes[511:1534].ljust(1024, b"\x00")
    board_bytes = board.encode("utf-8")[:15].ljust(16, b"\x00")

    sha = hashlib.sha1()
    sha.update(kernel_data)
    sha.update(struct.pack("<I", len(kernel_data)))
    sha.update(ramdisk_data)
    sha.update(struct.pack("<I", len(ramdisk_data)))
    sha.update(b"")
    sha.update(struct.pack("<I", 0))
    sha.update(b"")
    sha.update(struct.pack("<I", 0))
    sha.update(dtb_data)
    sha.update(struct.pack("<I", len(dtb_data)))
    img_id = sha.digest().ljust(32, b"\x00")

    header_size = 1660
    hdr = struct.pack(
        "<8s10I16s512s32s1024sIQIIQ",
        BOOT_MAGIC,
        len(kernel_data),
        kernel_addr,
        len(ramdisk_data),
        ramdisk_addr,
        0,  # second_size
        second_addr,
        tags_addr,
        PAGE_SIZE,
        2,  # header_version = 2
        os_version,
        board_bytes,
        main_cmdline,
        img_id,
        extra_cmdline,
        0,  # recovery_dtbo_size
        0,  # recovery_dtbo_offset
        header_size,
        len(dtb_data),
        dtb_addr,
    )

    with output_path.open("wb") as f:
        f.write(pad_to_page(hdr))
        f.write(pad_to_page(kernel_data))
        if ramdisk_data:
            f.write(pad_to_page(ramdisk_data))
        f.write(pad_to_page(dtb_data))

    print(f"[+] Packed Boot Image v2 -> {output_path} ({output_path.stat().st_size} bytes)")


def main() -> None:
    parser = argparse.ArgumentParser(description="Pack Android Boot Image Header v2 for SM-F7310")
    parser.add_argument("--kernel", required=True, type=Path, help="Path to ARM64 kernel Image")
    parser.add_argument("--ramdisk", required=False, type=Path, help="Path to initramfs.cpio.gz")
    parser.add_argument("--dtb", required=True, type=Path, help="Path to compiled .dtb")
    parser.add_argument("--cmdline", default="console=tty0 printk.devkmsg=on", help="Kernel command line")
    parser.add_argument("--output", "-o", required=True, type=Path, help="Output boot/recovery image path")
    args = parser.parse_args()
    pack_v2(args.kernel, args.ramdisk, args.dtb, args.output, args.cmdline)


if __name__ == "__main__":
    main()
