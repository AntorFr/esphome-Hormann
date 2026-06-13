#!/usr/bin/env python3
"""Decode the Hörmann UART (19200 8N1) from a raw Saleae export.
TX is the clean (idle-high) data line; RX is the idle-low complementary half.
Decode each channel into Hörmann frames, with timing and break durations.

Usage: python3 decode_uart.py [path.csv]   (default: UAP1-startup.csv next to this file)"""
import csv, os, sys

BAUD = 19200
BIT = 1.0 / BAUD                 # 52.083 µs
FRAME_GAP = 0.003                # 3 ms -> new frame


def load(path):
    rows = []
    with open(path) as f:
        r = csv.reader(f)
        next(r)                  # header
        for line in r:
            if len(line) < 4:
                continue
            rows.append((float(line[0]), int(line[1]), int(line[2]), int(line[3])))
    return rows


def decode_channel(rows, idx, invert=False):
    """Decode one channel (idx=1 TX, 2 RX). Idle=1, start bit = falling edge.
    invert=True for an idle-low line (inverted differential half): flip the level."""
    ts = [r[0] for r in rows]
    vs = [(1 - r[idx]) if invert else r[idx] for r in rows]
    n = len(ts)
    tend = ts[-1]

    def level_at(t):
        lo, hi, ans = 0, n - 1, vs[0]
        while lo <= hi:
            m = (lo + hi) // 2
            if ts[m] <= t:
                ans = vs[m]; lo = m + 1
            else:
                hi = m - 1
        return ans

    out, k, guard = [], 1, 0.0
    while k < n:
        if ts[k] < guard:
            k += 1; continue
        if vs[k - 1] == 1 and vs[k] == 0:          # falling edge = start bit
            st = ts[k]; val = 0; ok = True
            for b in range(8):
                tb = st + (1.5 + b) * BIT           # middle of data bit b (LSB first)
                if tb > tend:
                    ok = False; break
                val |= (level_at(tb) << b)
            if ok:
                out.append((st, val))
                guard = st + 9.5 * BIT
        k += 1
    return out


def group(bytestream, gap=FRAME_GAP):
    fr, cur, last = [], [], None
    for st, v in bytestream:
        if last is not None and st - last > gap:
            if cur:
                fr.append(cur)
            cur = []
        cur.append((st, v)); last = st
    if cur:
        fr.append(cur)
    return fr


def classify(hexs):
    """Short readable label based on the first useful byte (after the 0x00 break)."""
    b = hexs[1:] if hexs and hexs[0] == 0x00 else hexs
    if not b:
        return ""
    a = b[0]
    if a == 0x00:
        return "BCAST"
    if a == 0x80:
        cmd = b[2] if len(b) > 2 else None
        if cmd == 0x14:
            return "UAP1->scanresp"
        if cmd == 0x29:
            return "UAP1->statusresp"
        return "->master"
    if a == 0x0B or (0x01 <= a <= 0x1F):
        return "ANNOUNCE/ID?"
    if a == 0x28:
        cmd = b[2] if len(b) > 2 else None
        return "SCAN->us" if cmd == 0x01 else ("STATUS_REQ->us" if cmd == 0x02 else "->0x28")
    return f"addr=0x{a:02X}"


def show(title, bytestream):
    fr = group(bytestream)
    print(f"\n===== {title} : {len(fr)} frames =====")
    prev_end = None
    for f in fr:
        t0 = f[0][0]
        end = f[-1][0] + 10 * BIT
        hexs = [v for _, v in f]
        gap = f"{(t0 - prev_end) * 1000:7.1f}ms" if prev_end else "   --   "
        print(f"t={t0:9.5f}  +{gap}  [{len(f):2d}]  {':'.join('%02X' % v for v in hexs):<46} {classify(hexs)}")
        prev_end = end


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    path = sys.argv[1] if len(sys.argv) > 1 else os.path.join(here, "UAP1-startup.csv")
    rows = load(path)
    # TX = what the UAP1 TRANSMITS (announce + scanresp + statusresp). Clean, idle-high.
    show("TX (UAP1 transmits)", decode_channel(rows, 1))
    # RX = what the UAP1 RECEIVES (= the master transmits: scans, status_request, broadcasts).
    # idle-low -> inverted.
    show("RX (master transmits)", decode_channel(rows, 2, invert=True))


if __name__ == '__main__':
    main()
