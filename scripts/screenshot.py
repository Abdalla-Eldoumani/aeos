#!/usr/bin/env python3
"""Headless GUI verification for AEOS via QEMU screendump + QMP input injection.

A bare-metal kernel under `qemu ... -display none` has no window, but QEMU still
maintains the virtio-gpu scanout surface, and the QMP `screendump` command writes
that surface to a PNG. QMP `input-send-event` injects absolute (tablet) pointer
moves and button clicks. Together these let you verify the RENDERED output -
the live clock, window open animations, click-to-highlight - without a physical
display, which `make test` and boot-health cannot do (they confirm the code
compiles, links, and reaches the WM loop, not that the pixels are right).

Examples (run from the repo root after `make`):

  # Desktop, two captures 2.6 s apart - proves the clock advances on its own
  # (the 30 FPS redraw). Compare the taskbar time in shot-1.png vs shot-2.png.
  python3 scripts/screenshot.py --name clock --shots 2 --interval 2.6

  # Open the Terminal icon (double-click its centre) and capture - the window
  # must show its content, not a blank rectangle.
  python3 scripts/screenshot.py --name term --double 44,44

  # Open Files (icon 1) then single-click the first entry - it must highlight
  # with no drag. --before captures prior to the --click for an A/B comparison.
  python3 scripts/screenshot.py --name files --double 124,44 --click 250,142 --before

Desktop icon centres are (ICON_MARGIN + col*ICON_SPACING + 24,
ICON_MARGIN + row*84 + 24) with 2 columns: Terminal (44,44), Files (124,44),
Settings (44,128), About (124,128), Calc (44,212), SysMon (124,212),
Notes (44,296), Tetris (124,296). Icons LAUNCH on double-click.

Requires python3 and qemu-system-aarch64 (>= 7.1 for QMP screendump PNG).
"""
import argparse, json, os, socket, subprocess, sys, time

FB_W, FB_H = 640, 480          # AEOS framebuffer; tablet abs axis is 0..32767
ABS_MAX = 32768


def parse_xy(s):
    x, y = s.split(",")
    return int(x), int(y)


def main():
    ap = argparse.ArgumentParser(description="Capture the AEOS GUI headlessly.")
    ap.add_argument("--kernel", default="kernel.elf", help="kernel ELF (default ./kernel.elf)")
    ap.add_argument("--out", default=".", help="output directory for the PNGs")
    ap.add_argument("--name", default="shot", help="output filename prefix")
    ap.add_argument("--shots", type=int, default=1, help="number of captures")
    ap.add_argument("--interval", type=float, default=2.6, help="seconds between captures")
    ap.add_argument("--double", help="X,Y framebuffer px to DOUBLE-click (open an app) first")
    ap.add_argument("--click", help="X,Y framebuffer px to single-click (e.g. select an entry)")
    ap.add_argument("--before", action="store_true", help="also capture before the --click")
    ap.add_argument("--boot-timeout", type=float, default=25.0)
    args = ap.parse_args()

    if not os.path.exists(args.kernel):
        sys.exit("kernel not found: %s (run `make` first)" % args.kernel)
    os.makedirs(args.out, exist_ok=True)
    sock, ser = "/tmp/aeos_qmp_%d.sock" % os.getpid(), "/tmp/aeos_ser_%d.log" % os.getpid()
    for f in (sock, ser):
        try: os.remove(f)
        except OSError: pass

    qemu = [
        "qemu-system-aarch64", "-M", "virt", "-cpu", "cortex-a57", "-m", "256M", "-smp", "4",
        "-device", "virtio-gpu-device", "-device", "virtio-keyboard-device",
        "-device", "virtio-tablet-device",
        "-netdev", "user,id=net0", "-device", "virtio-net-device,netdev=net0",
        "-display", "none", "-serial", "file:" + ser,
        "-semihosting-config", "enable=on,target=native", "-rtc", "base=localtime",
        "-qmp", "unix:%s,server,nowait" % sock, "-kernel", args.kernel,
    ]
    proc = subprocess.Popen(qemu)
    try:
        for _ in range(100):
            if os.path.exists(sock): break
            time.sleep(0.1)
        s = socket.socket(socket.AF_UNIX)
        for _ in range(100):
            try: s.connect(sock); break
            except OSError: time.sleep(0.1)
        fp = s.makefile("rw")

        def cmd(obj):
            s.sendall((json.dumps(obj) + "\n").encode())
            while True:
                line = fp.readline()
                if not line: return None
                r = json.loads(line)
                if "return" in r or "error" in r: return r

        fp.readline()                          # QMP greeting
        cmd({"execute": "qmp_capabilities"})

        deadline = time.time() + args.boot_timeout
        booted = False
        while time.time() < deadline:
            try:
                if "window manager main loop" in open(ser, "rb").read().decode("latin1"):
                    booted = True; break
            except OSError: pass
            time.sleep(0.3)
        if not booted:
            sys.exit("kernel never reached the WM loop (see %s)" % ser)
        time.sleep(1.5)

        def shot(name):
            r = cmd({"execute": "screendump",
                     "arguments": {"filename": os.path.join(args.out, name), "format": "png"}})
            print(name, r)

        def absmove(px, py):
            cmd({"execute": "input-send-event", "arguments": {"events": [
                {"type": "abs", "data": {"axis": "x", "value": px * ABS_MAX // FB_W}},
                {"type": "abs", "data": {"axis": "y", "value": py * ABS_MAX // FB_H}}]}})

        def btn(down):
            cmd({"execute": "input-send-event", "arguments": {"events": [
                {"type": "btn", "data": {"button": "left", "down": down}}]}})

        if args.double:
            px, py = parse_xy(args.double)
            absmove(px, py); time.sleep(0.4)
            for _ in range(2):                 # two clicks within 500 ms = launch
                btn(True); time.sleep(0.08); btn(False); time.sleep(0.12)
            time.sleep(2.0)                    # let the open animation finish

        if args.before:
            shot(args.name + "-before.png")

        if args.click:
            px, py = parse_xy(args.click)
            absmove(px, py); time.sleep(0.25)
            btn(True); time.sleep(0.08); btn(False); time.sleep(0.6)

        if args.shots <= 1:
            shot(args.name + ".png")
        else:
            for i in range(args.shots):
                shot("%s-%d.png" % (args.name, i + 1))
                if i + 1 < args.shots:
                    time.sleep(args.interval)

        cmd({"execute": "quit"})
        time.sleep(0.5)
    finally:
        proc.terminate()
        try: proc.wait(timeout=3)
        except Exception: proc.kill()


if __name__ == "__main__":
    main()
