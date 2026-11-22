#!/usr/bin/env python3
"""
Synaptics S3916A (TCM v1) 10-finger Multi-Touch Driver -> Linux uinput
Hardware: Samsung Galaxy Z Flip5 (SM-F7310 b5q), I2C bus /dev/i2c-2 @ 0x20

Uses single-transaction 64-byte I2C reads (required by QCOM GENI I2C GPI DMA
on SM8550) and sets DC_NO_SLEEP=1 so the touch IC stays active.
"""

import fcntl
import os
import signal
import struct
import sys
import time

I2C_SLAVE = 0x0703

# Linux input/uinput constants (aarch64)
EV_SYN = 0x00
EV_KEY = 0x01
EV_ABS = 0x03

SYN_REPORT = 0x00
BTN_TOUCH = 0x14A

ABS_X = 0x00
ABS_Y = 0x01
ABS_MT_SLOT = 0x2F
ABS_MT_TOUCH_MAJOR = 0x30
ABS_MT_POSITION_X = 0x35
ABS_MT_POSITION_Y = 0x36
ABS_MT_TRACKING_ID = 0x39
ABS_MT_PRESSURE = 0x3A

INPUT_PROP_DIRECT = 0x01

UI_SET_EVBIT = 0x40045564
UI_SET_KEYBIT = 0x40045565
UI_SET_ABSBIT = 0x40045567
UI_SET_PROPBIT = 0x4004556E
UI_DEV_SETUP = 0x405C5503
UI_ABS_SETUP = 0x401C5504
UI_DEV_CREATE = 0x5501
UI_DEV_DESTROY = 0x5502

BUS_I2C = 0x18

running = True


def handle_sig(signum, frame):
    global running
    running = False


def create_uinput_touch():
    ufd = os.open("/dev/uinput", os.O_WRONLY | os.O_NONBLOCK)

    fcntl.ioctl(ufd, UI_SET_EVBIT, EV_KEY)
    fcntl.ioctl(ufd, UI_SET_EVBIT, EV_ABS)
    fcntl.ioctl(ufd, UI_SET_EVBIT, EV_SYN)
    fcntl.ioctl(ufd, UI_SET_KEYBIT, BTN_TOUCH)
    fcntl.ioctl(ufd, UI_SET_PROPBIT, INPUT_PROP_DIRECT)

    abs_axes = [
        (ABS_X, 0, 4095, 0, 0, 0),
        (ABS_Y, 0, 4095, 0, 0, 0),
        (ABS_MT_SLOT, 0, 9, 0, 0, 0),
        (ABS_MT_TRACKING_ID, 0, 65535, 0, 0, 0),
        (ABS_MT_POSITION_X, 0, 4095, 0, 0, 0),
        (ABS_MT_POSITION_Y, 0, 4095, 0, 0, 0),
        (ABS_MT_TOUCH_MAJOR, 0, 255, 0, 0, 0),
        (ABS_MT_PRESSURE, 0, 255, 0, 0, 0),
    ]
    for code, mn, mx, fuzz, flat, res in abs_axes:
        fcntl.ioctl(ufd, UI_SET_ABSBIT, code)
        setup = struct.pack("<HHiiiiii", code, 0, 0, mn, mx, fuzz, flat, res)
        fcntl.ioctl(ufd, UI_ABS_SETUP, setup)

    name = b"Synaptics S3916A Touchscreen (b5q)".ljust(80, b"\x00")
    dev_setup = struct.pack("<HHHH", BUS_I2C, 0x06CB, 0x3916, 0x0001) + name + struct.pack("<I", 0)
    fcntl.ioctl(ufd, UI_DEV_SETUP, dev_setup)
    fcntl.ioctl(ufd, UI_DEV_CREATE)
    return ufd


def emit(ufd, ev_type, code, value):
    os.write(ufd, struct.pack("<qqHHi", 0, 0, ev_type, code, value))


def tcm_cmd(fd, cmd, payload=b""):
    try:
        os.write(fd, struct.pack("<BH", cmd, len(payload)) + payload)
    except OSError as e:
        return None
    for _ in range(25):
        time.sleep(0.015)
        try:
            buf = os.read(fd, 64)
        except OSError:
            continue
        if len(buf) >= 4 and buf[0] == 0xA5:
            st = buf[1]
            if st in (0x00, 0x03, 0xFF):
                continue
            if st < 0x10:
                return st
    return None


def main():
    signal.signal(signal.SIGINT, handle_sig)
    signal.signal(signal.SIGTERM, handle_sig)

    i2c_dev = sys.argv[1] if len(sys.argv) > 1 else "/dev/i2c-2"
    debug = "--debug" in sys.argv

    fd = os.open(i2c_dev, os.O_RDWR)
    fcntl.ioctl(fd, I2C_SLAVE, 0x20)

    # Drain startup packet
    for _ in range(3):
        try:
            os.read(fd, 64)
        except OSError:
            pass

    # Disable sleep (DC_NO_SLEEP=1) and enable standard TCM v1 touch report (0x11)
    st_ns = tcm_cmd(fd, 0x24, struct.pack("<BH", 0x01, 1))
    st_dis_c1 = tcm_cmd(fd, 0x06, bytes([0xC1]))
    st_en_11 = tcm_cmd(fd, 0x05, bytes([0x11]))
    print(
        f"[b5q-touchd] S3916A init on {i2c_dev}@0x20: NO_SLEEP=0x{st_ns or 0:02x} "
        f"DIS_C1=0x{st_dis_c1 or 0:02x} EN_11=0x{st_en_11 or 0:02x}",
        flush=True,
    )

    ufd = create_uinput_touch()
    print("[b5q-touchd] uinput multi-touch device ready (0..4095 x 0..4095, 10 slots)", flush=True)

    active_slots = {}
    next_tid = 1
    log_count = 0

    try:
        while running:
            try:
                buf = os.read(fd, 64)
            except OSError:
                time.sleep(0.01)
                continue

            if len(buf) < 4 or buf[0] != 0xA5:
                time.sleep(0.005)
                continue

            code = buf[1]
            plen = struct.unpack("<H", buf[2:4])[0]

            if code in (0x00, 0x01, 0x03, 0xFF) or plen == 0:
                time.sleep(0.006)
                continue

            if debug and log_count < 40:
                print(
                    f"[pkt] code=0x{code:02x} plen={plen} data={buf[4:4+min(plen,48)].hex(' ')}",
                    flush=True,
                )
                log_count += 1

            # If payload > 60 bytes, read remaining chunk safely
            payload = buf[4 : 4 + min(plen, 60)]
            if plen > 60 and plen < 512:
                try:
                    more = os.read(fd, (plen - 60) + 4)
                    if more[:2] == b"\xa5\x03":
                        payload += more[2 : 2 + (plen - 60)]
                except OSError:
                    pass

            if code != 0x11 or len(payload) < 1:
                continue

            # Parse 11-byte finger objects starting at payload[1:]
            seen = {}
            off = 1
            while off + 11 <= len(payload):
                b = payload[off : off + 11]
                off += 11
                slot = b[0] & 0x0F
                cls = (b[0] >> 4) & 0x0F
                if slot >= 10:
                    continue
                if cls in (1, 2, 3, 5):
                    x = b[1] | ((b[2] & 0x0F) << 8)
                    y = (b[2] >> 4) | (b[3] << 4)
                    z = b[4]
                    w = max(b[5], b[6])
                    seen[slot] = (x, y, z, w)

            if debug and log_count < 40 and (seen or active_slots):
                print(f"[touch] seen={seen}", flush=True)

            changed = False
            for slot in list(active_slots.keys()):
                if slot not in seen:
                    emit(ufd, EV_ABS, ABS_MT_SLOT, slot)
                    emit(ufd, EV_ABS, ABS_MT_TRACKING_ID, -1)
                    del active_slots[slot]
                    changed = True

            primary = None
            for slot, (x, y, z, w) in seen.items():
                emit(ufd, EV_ABS, ABS_MT_SLOT, slot)
                if slot not in active_slots:
                    active_slots[slot] = next_tid
                    next_tid = (next_tid + 1) & 0xFFFF or 1
                    emit(ufd, EV_ABS, ABS_MT_TRACKING_ID, active_slots[slot])
                emit(ufd, EV_ABS, ABS_MT_POSITION_X, x)
                emit(ufd, EV_ABS, ABS_MT_POSITION_Y, y)
                emit(ufd, EV_ABS, ABS_MT_TOUCH_MAJOR, w)
                emit(ufd, EV_ABS, ABS_MT_PRESSURE, z)
                if primary is None:
                    primary = (x, y)
                changed = True

            if changed:
                if primary is not None:
                    emit(ufd, EV_KEY, BTN_TOUCH, 1)
                    emit(ufd, EV_ABS, ABS_X, primary[0])
                    emit(ufd, EV_ABS, ABS_Y, primary[1])
                else:
                    emit(ufd, EV_KEY, BTN_TOUCH, 0)
                emit(ufd, EV_SYN, SYN_REPORT, 0)

            time.sleep(0.005)
    finally:
        try:
            fcntl.ioctl(ufd, UI_DEV_DESTROY)
        except OSError:
            pass
        os.close(ufd)
        os.close(fd)


if __name__ == "__main__":
    main()
