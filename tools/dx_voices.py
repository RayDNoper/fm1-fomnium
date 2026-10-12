#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Jambox FM voices from a DX7 32-voice bank (.syx): the patches that are two carrier-and-modulator
pairs on operators 3..6 (a DX9's algorithm 5 as a DX7 holds it), as lines for FMV in dsp/omni.c.

  tools/dx_voices.py BANK.syx 3,4,5 [NAME]

An approximation: Jambox's voice has one envelope for the sound and one for both modulators, no detune,
no touch, and the ratios of FM_RATIO. Operator 4 -> 3 becomes Jambox's 2 -> 1, and 6 -> 5 (with the
feedback) its 4 -> 3. A patch whose second carrier is silent plays the first pair on both."""
import math
import sys

RAT = [0.5, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 14, 16]


def tfull(r):                       # a full (96 dB) fall at envelope rate r, seconds (after Dexed's rates)
    return 0.008 * 2 ** ((99 - r) / 6.25)


def tpar(t):                        # seconds as fm_time()'s 0..127 (1 ms .. 10 s)
    return max(0, min(127, round(127 * math.log2(max(t, 0.001) / 0.001) / 13.287712)))


def nearest(x):
    return min(range(len(RAT)), key=lambda i: abs(math.log(RAT[i] / x)))


def falls(o):                       # an operator's envelope: (the time of a 40 dB fall, its sustain 0..1)
    l1, l2, l3 = o["L"][:3]
    r = o["R"][1] if l1 - l2 > 8 else o["R"][2]
    return tfull(r) * 40 / 96, 10 ** (-0.0375 * max(0, max(l1, l2) - l3))


def index(m):                       # a modulator's peak as Jambox's index (127: 4 pi, a DX7's 99)
    pk = max(m["L"][:3]) - (8 if m["R"][0] < 35 else 0)      # a slow rise: Jambox's falls only, so less
    return max(0, min(127, round(127 * 2 ** ((m["ol"] - (99 - pk) - 99) / 8))))


def main():
    d = open(sys.argv[1], "rb").read()
    if len(d) != 4104 or d[:6] != bytes([0xF0, 0x43, 0, 9, 0x20, 0]):
        raise SystemExit("not a DX7 32-voice bank")
    name = sys.argv[3] if len(sys.argv) > 3 else "DX"
    for v in (int(x) for x in sys.argv[2].split(",")):
        p = d[6 + 128 * (v - 1): 6 + 128 * v]
        ops = {}
        for k in range(6):          # operator 6 first
            o = p[17 * k: 17 * k + 17]
            c = o[15] >> 1
            ops[6 - k] = {"R": list(o[0:4]), "L": list(o[4:8]), "ol": o[14],
                          "ratio": (0.5 if c == 0 else c) * (1 + o[16] / 100)}
        fb = p[111] & 7
        c1, m1, c2, m2 = ops[3], ops[4], ops[5], ops[6]
        if c2["ol"] < 60:           # one pair only
            c2, m2, fb = c1, m1, 0
        r1, r3 = nearest(c1["ratio"]), nearest(c2["ratio"])
        r2 = nearest(m1["ratio"] / c1["ratio"] * RAT[r1])
        r4 = nearest(m2["ratio"] / c2["ratio"] * RAT[r3])
        md, cd = [falls(m1), falls(m2)], [falls(c1), falls(c2)]
        moddec = 127 if min(s for _, s in md) > 0.6 else tpar(sum(t for t, _ in md) / 2)
        dec, sus = tpar(max(t for t, _ in cd)), round(127 * max(s for _, s in cd))
        ta = min(tfull(c1["R"][0]), tfull(c2["R"][0])) / 8
        att = 0 if ta < 0.006 else tpar(ta)
        fbv = round(min(127, 63.5 * 2 ** (fb - 7) * 2 ** ((m2["ol"] - 99) / 8))) if fb else 0
        lv2 = round(127 * min(1.0, 10 ** (-0.0375 * (c1["ol"] - c2["ol"]))))
        i1, i2 = index(m1), index(m2)
        tm = 0.001 * 2 ** (moddec * 13.287712 / 127)
        if md[0][0] < tm / 8:       # a modulator gone long before the other: a click there, not this long
            i1 = round(i1 / 4)
        if md[1][0] < tm / 8:
            i2 = round(i2 / 4)
        print(f'    {{"{name} {v}", {r2}, {r3}, {r4}, {i1}, {moddec}, {fbv}, {dec}, {att}, {sus}, {r1}, {i2}, {lv2}}},')


main()
