#!/usr/bin/env python3
"""Read the initiator's CSV (BEGIN..END blocks) and estimate distance two ways.

usage: python3 range.py capture.txt [--offset METERS]
"""
import sys, numpy as np

C = 299_792_458.0

def sweeps(path):
    rows, out = [], []
    for line in open(path):
        line = line.strip()
        if line == "BEGIN": rows = []
        elif line == "END" and rows: out.append(np.array(rows, float))
        elif line and line[0].isdigit(): rows.append([float(x) for x in line.split(",")])
    return out

def combine(s):
    f = s[:, 0] * 1e6
    a = s[:, 1] + 1j * s[:, 2]          # initiator's measurement of the reflector's tone
    b = s[:, 3] + 1j * s[:, 4]          # reflector's measurement of the initiator's tone
    return f, a * b                      # both local-oscillator phases cancel: 2x propagation

def slope(f, h):
    phi = np.unwrap(np.angle(h))
    k = np.polyfit(f, phi, 1)[0]
    return -k * C / (4 * np.pi)          # sign: CHECK on first capture

def ifft(f, h, pad=16):
    # resample onto the uniform 1 MHz grid; missing channels left at 0
    grid = np.arange(f.min(), f.max() + 1e6, 1e6)
    H = np.zeros(len(grid), complex)
    H[np.round((f - grid[0]) / 1e6).astype(int)] = h
    n = pad * len(grid)
    cir = np.abs(np.fft.ifft(H, n))   # sign/conjugation: CHECK on first capture
    tau = np.arange(n) / (n * 1e6)       # delay axis of the round trip
    d = tau * C / 2
    # first local maximum above half the strongest peak = direct path
    pk = np.where((cir[1:-1] > cir[:-2]) & (cir[1:-1] >= cir[2:]) & (cir[1:-1] > 0.5 * cir.max()))[0] + 1
    return d[pk[0]]                      # unambiguous up to 150 m

if __name__ == "__main__":
    off = float(sys.argv[sys.argv.index("--offset") + 1]) if "--offset" in sys.argv else 0.0
    for s in sweeps(sys.argv[1]):
        f, h = combine(s)
        print(f"slope {slope(f, h) - off:7.2f} m   ifft {ifft(f, h) - off:7.2f} m   "
              f"|h| min/med {np.abs(h).min():.0f}/{np.median(np.abs(h)):.0f}")
