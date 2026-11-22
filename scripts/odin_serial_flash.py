#!/usr/bin/env python3
"""
Clean, non-blocking macOS Serial Odin v3/v4 Flasher for Samsung SM-F7310.
Uses raw POSIX non-blocking I/O + select() on /dev/cu.usbmodem* to avoid
Node serialport / libusb kext stalls, with verbose packet tracing and strict timeouts.
"""
import fcntl
import os
import select
import struct
import sys
import termios
import time
from pathlib import Path

CMD_INIT = 100
CMD_PIT = 101
CMD_TRANSFER = 102
CMD_CLOSE = 103

CMD_SIZE = 1024
PIT_CHUNK = 500


def open_raw_serial(path: str) -> int:
    fd = os.open(path, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    attrs = termios.tcgetattr(fd)
    # Pure raw mode (cfmakeraw equivalent)
    attrs[0] = 0  # iflag
    attrs[1] = 0  # oflag
    attrs[2] = termios.CS8 | termios.CREAD | termios.CLOCAL  # cflag
    attrs[3] = 0  # lflag
    attrs[4] = termios.B115200
    attrs[5] = termios.B115200
    attrs[6][termios.VMIN] = 0
    attrs[6][termios.VTIME] = 0
    termios.tcsetattr(fd, termios.TCSANOW, attrs)
    # Assert DTR & RTS (macOS TIOCMBIS = 0x8004746c, TIOCM_DTR=2, TIOCM_RTS=4)
    try:
        fcntl.ioctl(fd, 0x8004746C, struct.pack("I", 0x006))
    except OSError:
        pass
    time.sleep(0.1)
    termios.tcflush(fd, termios.TCIOFLUSH)
    return fd


def write_all(fd: int, data: bytes, timeout: float = 10.0) -> None:
    view = memoryview(data)
    total = len(data)
    sent = 0
    deadline = time.monotonic() + timeout
    while sent < total:
        rem = deadline - time.monotonic()
        if rem <= 0:
            raise TimeoutError(f"write_all timed out after {sent}/{total} bytes")
        _, w, _ = select.select([], [fd], [], min(rem, 1.0))
        if not w:
            continue
        try:
            n = os.write(fd, view[sent : min(sent + 32768, total)])
            if n > 0:
                sent += n
        except BlockingIOError:
            continue


def read_exact(fd: int, size: int, timeout: float = 15.0) -> bytes:
    buf = bytearray()
    deadline = time.monotonic() + timeout
    while len(buf) < size:
        rem = deadline - time.monotonic()
        if rem <= 0:
            raise TimeoutError(f"read_exact timed out: got {len(buf)}/{size} bytes ({buf.hex()})")
        r, _, _ = select.select([fd], [], [], min(rem, 1.0))
        if not r:
            continue
        try:
            chunk = os.read(fd, size - len(buf))
            if not chunk:
                raise EOFError("Serial port closed by device")
            buf.extend(chunk)
        except BlockingIOError:
            continue
    return bytes(buf)


def pack_cmd(cmd: int, subcmd: int, *words: int) -> bytes:
    pkt = bytearray(CMD_SIZE)
    struct.pack_into("<II", pkt, 0, cmd, subcmd)
    for i, w in enumerate(words):
        struct.pack_into("<I", pkt, 8 + i * 4, w & 0xFFFFFFFF)
    return bytes(pkt)


def send_cmd(fd: int, cmd: int, subcmd: int, *words: int, timeout: float = 15.0) -> tuple[int, int]:
    write_all(fd, pack_cmd(cmd, subcmd, *words), timeout=timeout)
    resp = read_exact(fd, 8, timeout=timeout)
    return struct.unpack("<II", resp)


def parse_pit(pit_bytes: bytes) -> list[dict]:
    magic, count = struct.unpack_from("<II", pit_bytes, 0)
    if magic != 0x12349876:
        raise ValueError(f"Bad PIT magic: 0x{magic:08x}")
    entries = []
    off = 28
    for _ in range(count):
        if off + 132 > len(pit_bytes):
            break
        btype, dtype, ident, attrs, update_attrs, blk_sz, blk_cnt, foff, fsz = struct.unpack_from("<9I", pit_bytes, off)
        pname = pit_bytes[off + 36 : off + 68].split(b"\x00")[0].decode("ascii", errors="ignore")
        fname = pit_bytes[off + 68 : off + 100].split(b"\x00")[0].decode("ascii", errors="ignore")
        entries.append({
            "binaryType": btype,
            "deviceType": dtype,
            "identifier": ident,
            "partitionName": pname,
            "flashFilename": fname,
        })
        off += 132
    return entries


def main() -> None:
    port = sys.argv[1] if len(sys.argv) > 1 else "/dev/cu.usbmodem201"
    img_path = Path(sys.argv[2] if len(sys.argv) > 2 else "/Users/jerry/Desktop/workspace/zflip5/backups/backup_b5q/recovery.img")
    target_name = sys.argv[3] if len(sys.argv) > 3 else "recovery.img"
    part_size = int(sys.argv[4]) if len(sys.argv) > 4 else 131072  # 128 KB

    img_size = img_path.stat().st_size
    print(f"[*] Opening {port}...", flush=True)
    fd = open_raw_serial(port)
    try:
        print("[1] Handshake: sending ODIN...", flush=True)
        write_all(fd, b"ODIN", timeout=5.0)
        hs = read_exact(fd, 4, timeout=5.0)
        print(f"    Got: {hs!r}", flush=True)
        if hs != b"LOKE":
            raise RuntimeError(f"Unexpected handshake: {hs!r}")

        print("[2] Init session (requested variant=5)...", flush=True)
        rcmd, rval = send_cmd(fd, CMD_INIT, 0, 5)
        variant = (rval >> 16) & 0xFFFF
        print(f"    Init ack: cmd={rcmd} val=0x{rval:08x} (variant={variant})", flush=True)

        print(f"[3] Setting FilePartSize={part_size}...", flush=True)
        rcmd, rval = send_cmd(fd, CMD_INIT, 5, part_size)
        print(f"    FilePartSize ack: cmd={rcmd} val={rval}", flush=True)

        print(f"[4] Setting TotalBytes={img_size}...", flush=True)
        rcmd, rval = send_cmd(fd, CMD_INIT, 2, img_size & 0xFFFFFFFF, (img_size >> 32) & 0xFFFFFFFF)
        print(f"    TotalBytes ack: cmd={rcmd} val={rval}", flush=True)

        print("[5] Reading PIT...", flush=True)
        rcmd, pit_size = send_cmd(fd, CMD_PIT, 1)
        print(f"    PIT size: {pit_size} bytes", flush=True)
        pit_buf = bytearray()
        idx = 0
        while len(pit_buf) < pit_size:
            take = min(PIT_CHUNK, pit_size - len(pit_buf))
            write_all(fd, pack_cmd(CMD_PIT, 2, idx))
            pit_buf.extend(read_exact(fd, take))
            idx += 1
        rcmd, rval = send_cmd(fd, CMD_PIT, 3)
        entries = parse_pit(bytes(pit_buf))
        print(f"    PIT complete (ack={rcmd},{rval}), {len(entries)} entries", flush=True)

        entry = next((e for e in entries if e["flashFilename"] == target_name or e["partitionName"].upper() == "RECOVERY"), None)
        if not entry:
            raise RuntimeError(f"Partition {target_name} not found in PIT")
        print(f"[6] Matched PIT entry: {entry}", flush=True)

        # Max 30 parts per sequence (classic Odin v3/v4 limit!)
        max_parts_per_seq = 30
        max_seq_bytes = max_parts_per_seq * part_size

        t0 = time.monotonic()
        with img_path.open("rb") as f:
            sent_total = 0
            seq_idx = 0
            while sent_total < img_size:
                seq_idx += 1
                seq_actual = min(max_seq_bytes, img_size - sent_total)
                seq_padded = ((seq_actual + part_size - 1) // part_size) * part_size
                n_parts = seq_padded // part_size
                is_last = (sent_total + seq_actual) >= img_size

                rcmd, rval = send_cmd(fd, CMD_TRANSFER, 0)
                if rcmd != CMD_TRANSFER or rval != 0:
                    raise RuntimeError(f"Transfer Download cmd rejected: {rcmd}, {rval}")

                rcmd, rval = send_cmd(fd, CMD_TRANSFER, 2, seq_padded)
                if rcmd != CMD_TRANSFER or rval != 0:
                    raise RuntimeError(f"Transfer Start cmd rejected: {rcmd}, {rval}")

                if seq_idx == 1:
                    print(f"    Sequence 1 started cleanly (seq_padded={seq_padded}, n_parts={n_parts})!", flush=True)

                for p_i in range(n_parts):
                    raw = f.read(min(part_size, img_size - sent_total))
                    sent_total += len(raw)
                    if len(raw) < part_size:
                        raw = raw + b"\x00" * (part_size - len(raw))
                    write_all(fd, raw, timeout=10.0)
                    rcmd, rval = struct.unpack("<II", read_exact(fd, 8, timeout=15.0))
                    # Odin SendFilePartResponse returns (0, part_index)
                    if rcmd not in (0, CMD_TRANSFER) or rval != p_i:
                        raise RuntimeError(f"Part {p_i} ack mismatch: cmd={rcmd}, val={rval}")

                rcmd, rval = send_cmd(
                    fd,
                    CMD_TRANSFER,
                    3,
                    entry["binaryType"],
                    seq_actual,
                    0,
                    entry["deviceType"],
                    entry["identifier"],
                    1 if is_last else 0,
                    0,
                    0,
                    timeout=30.0,
                )
                if rcmd != CMD_TRANSFER or rval != 0:
                    raise RuntimeError(f"Sequence {seq_idx} complete rejected: cmd={rcmd}, val={rval}")

                elapsed = max(time.monotonic() - t0, 0.001)
                pct = sent_total * 100.0 / img_size
                mbps = sent_total / (1024 * 1024) / elapsed
                print(f"    [seq {seq_idx}] {sent_total}/{img_size} ({pct:.1f}%) @ {mbps:.1f} MB/s", flush=True)

        print("[7] Ending session & rebooting device...", flush=True)
        rcmd, rval = send_cmd(fd, CMD_CLOSE, 0)
        print(f"    EndSession ack: {rcmd}, {rval}", flush=True)
        rcmd, rval = send_cmd(fd, CMD_CLOSE, 1)
        print(f"    Reboot ack: {rcmd}, {rval}", flush=True)
        print("[+] ALL DONE! Phone is rebooting back to Android 14!", flush=True)
    finally:
        os.close(fd)


if __name__ == "__main__":
    main()
