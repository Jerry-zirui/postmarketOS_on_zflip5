#!/usr/bin/env python3
"""
Guided Touch Evdev Recorder for Z Flip 5 (SM-F7310)
Pairs with b5q_touch_ui.qml rendered fullscreen on KDE Plasma 6 Wayland.
- Records EVERY evdev event from /dev/input/event3 (Synaptics S3916A) with millisecond timestamps
- Tracks IRQ 189 count before/after each step
- Advances step on Volume Up (event2), retries current step on Volume Down (event1)
- Saves complete trace to /tmp/b5q_touch_evdev_capture.json
"""

import json
import os
import select
import struct
import subprocess
import sys
import time

EV_VOLDOWN = "/dev/input/event1"
EV_VOLUP = "/dev/input/event2"
EV_TOUCH = "/dev/input/event3"
UI_STATE = "/tmp/touch_ui_state.json"
OUT_JSON = "/tmp/b5q_touch_evdev_capture.json"

EV_FMT = "llHHI"
EV_SIZE = struct.calcsize(EV_FMT)

STEPS = [
    {"id": "1_tap_center",  "title": "步骤 1/7：单点轻触",     "desc": "单指轻点红色圆心 1 次后立刻抬手",        "draw": "tap_center"},
    {"id": "2_tap_corners", "title": "步骤 2/7：四角定位",     "desc": "依次轻点 1 → 2 → 3 → 4 四个蓝圈",       "draw": "tap_corners"},
    {"id": "3_long_press",  "title": "步骤 3/7：原地长按",     "desc": "单指按住黄色圆心静止 2 秒后再抬手",      "draw": "long_press"},
    {"id": "4_double_tap",  "title": "步骤 4/7：快速双击",     "desc": "在青色圆心快速连续点 2 下后抬手",        "draw": "double_tap"},
    {"id": "5_swipe_lines", "title": "步骤 5/7：横竖画线",     "desc": "先沿绿线 A 左→右划，再沿蓝线 B 上→下划", "draw": "swipe_lines"},
    {"id": "6_area_palm",   "title": "步骤 6/7：大面积按压",   "desc": "大拇指指腹平贴紫色方块按压 1 秒后抬手",  "draw": "area_palm"},
    {"id": "7_two_finger",  "title": "步骤 7/7：双指捏合",     "desc": "两指同时按住橙圈向中间捏合后同时抬手",   "draw": "two_finger"},
]

ABS_NAMES = {
    0x00: "ABS_X",
    0x01: "ABS_Y",
    0x2f: "ABS_MT_SLOT",
    0x30: "ABS_MT_TOUCH_MAJOR",
    0x31: "ABS_MT_TOUCH_MINOR",
    0x35: "ABS_MT_POSITION_X",
    0x36: "ABS_MT_POSITION_Y",
    0x37: "ABS_MT_TOOL_TYPE",
    0x39: "ABS_MT_TRACKING_ID",
    0x3a: "ABS_MT_PRESSURE",
}


def get_irq_count():
    try:
        with open("/proc/interrupts") as f:
            for line in f:
                if "synaptics-s3916" in line:
                    parts = line.split()
                    return sum(int(x) for x in parts[1:9] if x.isdigit())
    except Exception:
        pass
    return -1


def write_ui_state(state):
    tmp = UI_STATE + ".tmp"
    with open(tmp, "w") as f:
        json.dump(state, f)
    os.chmod(tmp, 0o644)
    os.replace(tmp, UI_STATE)


def main():
    # Initialize UI state before launching QML window
    write_ui_state({
        "step_idx": 0,
        "total_steps": len(STEPS),
        "title": STEPS[0]["title"],
        "desc": STEPS[0]["desc"],
        "draw": STEPS[0]["draw"],
        "ev_count": 0,
        "down_count": 0,
        "up_count": 0,
        "active_touch": False,
        "norm_x": -1.0,
        "norm_y": -1.0,
        "last_line": "正在启动引导界面...",
        "finished": False,
        "quit": False,
    })

    # Launch fullscreen Qt6 QML UI as user jerry on active Wayland session
    qml_cmd = [
        "su", "-", "jerry", "-c",
        "WAYLAND_DISPLAY=wayland-0 XDG_RUNTIME_DIR=/run/user/1000 QT_QPA_PLATFORM=wayland "
        "QML_XHR_ALLOW_FILE_READ=1 /usr/lib/qt6/bin/qmlscene /usr/local/share/b5q_touch_ui.qml"
    ]
    ui_proc = subprocess.Popen(qml_cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(1.2)

    fd_down = os.open(EV_VOLDOWN, os.O_RDONLY | os.O_NONBLOCK)
    fd_up = os.open(EV_VOLUP, os.O_RDONLY | os.O_NONBLOCK)
    fd_ts = os.open(EV_TOUCH, os.O_RDONLY | os.O_NONBLOCK)

    # Drain stale events
    for fd in (fd_down, fd_up, fd_ts):
        try:
            while os.read(fd, 4096):
                pass
        except BlockingIOError:
            pass

    results = {"device": "SM-F7310 Synaptics S3916A (evdev)", "steps": []}

    try:
        step_idx = 0
        while step_idx < len(STEPS):
            step = STEPS[step_idx]
            t0 = time.monotonic()
            irq_start = get_irq_count()
            frames = []
            cur_frame = []
            down_count = 0
            up_count = 0
            active_slots = set()
            cur_slot = 0
            last_x, last_y = -1, -1
            last_line = "等待触摸目标..."

            def push_ui():
                write_ui_state({
                    "step_idx": step_idx,
                    "total_steps": len(STEPS),
                    "title": step["title"],
                    "desc": step["desc"],
                    "draw": step["draw"],
                    "ev_count": len(frames),
                    "down_count": down_count,
                    "up_count": up_count,
                    "active_touch": len(active_slots) > 0,
                    "norm_x": (last_x / 4095.0) if last_x >= 0 else -1.0,
                    "norm_y": (last_y / 4095.0) if last_y >= 0 else -1.0,
                    "last_line": last_line,
                    "finished": False,
                    "quit": False,
                })

            push_ui()
            print(f"\n>>> [{step['id']}] {step['title']} | {step['desc']}", flush=True)
            last_ui = time.monotonic()
            dirty = False

            while True:
                rlist, _, _ = select.select([fd_ts, fd_down, fd_up], [], [], 0.05)

                if fd_ts in rlist:
                    try:
                        data = os.read(fd_ts, EV_SIZE * 64)
                    except BlockingIOError:
                        data = b""
                    for off in range(0, len(data) - EV_SIZE + 1, EV_SIZE):
                        sec, usec, etype, ecode, eval_ = struct.unpack(EV_FMT, data[off:off + EV_SIZE])
                        # Signed 32-bit value for tracking ID (-1)
                        sval = struct.unpack("i", struct.pack("I", eval_))[0]
                        dt_ms = round((time.monotonic() - t0) * 1000.0, 2)

                        if etype == 0 and ecode == 0:  # SYN_REPORT
                            if cur_frame:
                                frames.append({"t_ms": dt_ms, "events": cur_frame})
                                cur_frame = []
                                dirty = True
                        elif etype == 3:  # EV_ABS
                            name = ABS_NAMES.get(ecode, f"ABS_{ecode:#x}")
                            cur_frame.append([name, sval])
                            if ecode == 0x2f:
                                cur_slot = sval
                            elif ecode == 0x39:
                                if sval >= 0:
                                    active_slots.add(cur_slot)
                                    down_count += 1
                                    last_line = f"DOWN slot={cur_slot} id={sval} ({last_x},{last_y})"
                                else:
                                    active_slots.discard(cur_slot)
                                    up_count += 1
                                    last_line = f"UP   slot={cur_slot} (active={len(active_slots)})"
                            elif ecode == 0x35:
                                last_x = sval
                                last_line = f"MOVE slot={cur_slot} x={last_x} y={last_y} active={sorted(active_slots)}"
                            elif ecode == 0x36:
                                last_y = sval
                                last_line = f"MOVE slot={cur_slot} x={last_x} y={last_y} active={sorted(active_slots)}"
                        elif etype == 1:  # EV_KEY
                            kname = "BTN_TOUCH" if ecode == 330 else f"KEY_{ecode}"
                            cur_frame.append([kname, sval])

                now = time.monotonic()
                if dirty and (now - last_ui) >= 0.08:
                    push_ui()
                    last_ui = now
                    dirty = False

                btn = None
                for fd in (fd_down, fd_up):
                    if fd in rlist:
                        try:
                            bdata = os.read(fd, EV_SIZE * 16)
                        except BlockingIOError:
                            continue
                        for off in range(0, len(bdata) - EV_SIZE + 1, EV_SIZE):
                            _, _, etype, ecode, eval_ = struct.unpack(EV_FMT, bdata[off:off + EV_SIZE])
                            if etype == 1 and eval_ == 1:
                                if ecode == 115:
                                    btn = "next"
                                elif ecode == 114:
                                    btn = "retry"

                if btn == "retry":
                    print(f"  [RETRY] Cleared {len(frames)} frames for {step['id']}", flush=True)
                    frames = []
                    cur_frame = []
                    down_count = 0
                    up_count = 0
                    active_slots.clear()
                    t0 = time.monotonic()
                    irq_start = get_irq_count()
                    last_line = "已清空，请重做本步..."
                    push_ui()
                    time.sleep(0.25)
                elif btn == "next":
                    irq_end = get_irq_count()
                    print(
                        f"  [DONE] {step['id']}: frames={len(frames)} down={down_count} up={up_count} "
                        f"active_end={sorted(active_slots)} irqs={irq_end - irq_start}",
                        flush=True,
                    )
                    results["steps"].append({
                        "id": step["id"],
                        "title": step["title"],
                        "frame_count": len(frames),
                        "down_count": down_count,
                        "up_count": up_count,
                        "active_slots_at_end": sorted(active_slots),
                        "irq_delta": irq_end - irq_start,
                        "frames": frames,
                    })
                    step_idx += 1
                    time.sleep(0.25)
                    break

        with open(OUT_JSON, "w") as f:
            json.dump(results, f, indent=2)

        write_ui_state({
            "step_idx": len(STEPS),
            "total_steps": len(STEPS),
            "title": "全部 7 步采集完成！",
            "desc": "正在保存日志并退出...",
            "draw": "done",
            "ev_count": sum(s["frame_count"] for s in results["steps"]),
            "down_count": sum(s["down_count"] for s in results["steps"]),
            "up_count": sum(s["up_count"] for s in results["steps"]),
            "active_touch": False,
            "norm_x": -1.0,
            "norm_y": -1.0,
            "last_line": f"已保存至 {OUT_JSON}",
            "finished": True,
            "quit": False,
        })
        print(f"\n[+] ALL 7 STEPS COMPLETED! Saved evdev capture to {OUT_JSON}", flush=True)
        time.sleep(1.5)
        write_ui_state({
            "step_idx": len(STEPS), "total_steps": len(STEPS), "title": "", "desc": "", "draw": "done",
            "ev_count": 0, "down_count": 0, "up_count": 0, "active_touch": False,
            "norm_x": -1.0, "norm_y": -1.0, "last_line": "", "finished": True, "quit": True,
        })
    finally:
        for fd in (fd_down, fd_up, fd_ts):
            os.close(fd)
        try:
            ui_proc.terminate()
            ui_proc.wait(timeout=2)
        except Exception:
            ui_proc.kill()


if __name__ == "__main__":
    main()
