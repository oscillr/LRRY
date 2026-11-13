#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 OSCILL/R (L/RRY). Part of L/RRY, a modified version of Felucca.
"""Sends `bench cal` to the unit's console (a LRRY_BENCH build) and prints the BENCH,CAL line: what the CPU does per loop iteration (standard library only).
  python3 tools/lrry_cal.py [PORT]"""
import glob
import os
import select
import sys
import termios
import time

port = sys.argv[1] if len(sys.argv) > 1 else sorted(glob.glob("/dev/cu.usbmodem*"))[0]
fd = os.open(port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
a = termios.tcgetattr(fd)
a[0], a[1], a[2], a[3] = 0, 0, termios.CS8 | termios.CREAD | termios.CLOCAL, 0
a[4] = a[5] = termios.B115200
termios.tcsetattr(fd, termios.TCSANOW, a)
os.write(fd, b"\rbench cal\r")
buf, end = b"", time.time() + 20
while time.time() < end and b"CAL," not in buf:
    if select.select([fd], [], [], 0.2)[0]:
        buf += os.read(fd, 4096)
time.sleep(0.3)
if select.select([fd], [], [], 0.1)[0]:
    buf += os.read(fd, 4096)
print([l for l in buf.decode("ascii", "replace").splitlines() if "CAL," in l] or buf[-200:])
os.close(fd)
