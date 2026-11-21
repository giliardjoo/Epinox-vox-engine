#!/usr/bin/env python3
"""Measures a recorded voice and writes an EPINOX VOX ENGINE voice-profile pack (voicepack.json).

Usage: python3 analyze_voice.py recording.wav --id my-voice --name "My Voice" [--out folder]

The recording follows the test script: sustained A E I O U at a low, a middle and a high comfortable note (three groups of five,
about 3 s each, with short pauses). The script finds the steady vowels, measures their pitch and formants (linear prediction),
and writes the profile the plugin reads. Everything it measured is printed, so a bad recording is easy to spot.
"""
import argparse, json, os, sys
import numpy as np
from scipy.io import wavfile
from scipy.linalg import solve_toeplitz
from scipy.signal import lfilter, resample_poly

VOWELS = "aeiou"
REF = {"a": (730, 1090, 2440), "e": (530, 1840, 2480), "i": (270, 2290, 3010), "o": (570, 840, 2410), "u": (300, 870, 2240)}


def load(path):
    sr, x = wavfile.read(path)
    if x.dtype.kind == "i":
        x = x / float(np.iinfo(x.dtype).max)
    elif x.dtype.kind == "u":
        x = (x.astype(np.float64) - 128) / 128
    x = x.astype(np.float64)
    if x.ndim > 1:
        x = x.mean(axis=1)
    return sr, x


def f0_track(x, sr, hop):
    """Pitch by normalised autocorrelation; returns (f0, strength) per hop (0 where unvoiced)."""
    win = int(0.04 * sr)
    lo, hi = int(sr / 500), int(sr / 70)
    f0, st = [], []
    for s in range(0, len(x) - win - hi, hop):
        seg = x[s:s + win + hi]
        a, b = seg[:win], seg
        if np.sqrt(np.mean(a * a)) < 1e-3:
            f0.append(0.0); st.append(0.0); continue
        ac = np.correlate(b, a, mode="valid")[: hi + 1]
        norm = np.sqrt(np.sum(a * a) * np.array([np.sum(b[k:k + win] ** 2) for k in range(hi + 1)])) + 1e-12
        r = ac / norm
        k = lo + int(np.argmax(r[lo:hi + 1]))
        f0.append(sr / k if r[k] > 0.8 else 0.0); st.append(float(r[k]))
    return np.array(f0), np.array(st)


def lpc_formants(seg, sr):
    y = lfilter([1, -0.97], [1], seg) * np.hamming(len(seg))
    order = 2 + int(sr / 1000)
    r = np.correlate(y, y, mode="full")[len(y) - 1: len(y) + order]
    r[0] += 1e-9 + 1e-6 * r[0]
    a = np.concatenate(([1.0], solve_toeplitz(r[:order], -r[1:order + 1])))
    roots = [z for z in np.roots(a) if z.imag > 0.01]
    cands = sorted((np.angle(z) * sr / (2 * np.pi), -np.log(abs(z)) * sr / np.pi) for z in roots)
    f = [fr for fr, bw in cands if 150 < fr < 4500 and bw < 500]
    return f


def stable_regions(f0, hop, sr, min_s=0.5):
    """Runs of voiced frames with a steady pitch. Short dropouts (3 frames) and single-frame glitches are tolerated."""
    f0 = np.array(f0, dtype=float)   # a copy: octave slips are folded back in place
    regs, n, i = [], len(f0), 0
    while i < n:
        if f0[i] <= 0:
            i += 1; continue
        j, last, gap = i, f0[i], 0
        k = i + 1
        while k < n:
            if f0[k] > 0:
                octaves = round(12 * np.log2(f0[k] / last) / 12)
                if octaves != 0 and abs(12 * np.log2(f0[k] / last) - 12 * octaves) < 1.5:
                    f0[k] = f0[k] / 2 ** octaves   # an octave slip of the pitch tracker
            if f0[k] > 0 and abs(12 * np.log2(f0[k] / last)) < 1.5:
                j, last, gap = k, f0[k], 0
            elif f0[k] > 0:
                break
            else:
                gap += 1
                if gap > 3:
                    break
            k += 1
        if (j - i + 1) * hop / sr >= min_s:
            regs.append((i, j))
        i = max(j + 1, i + 1)
    return regs


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("wav"); ap.add_argument("--id", required=True); ap.add_argument("--name", required=True)
    ap.add_argument("--license", default="Recordings made and owned by the voice's owner, who allows commercial use inside EPINOX VOX ENGINE.")
    ap.add_argument("--out", default=None)
    args = ap.parse_args()

    sr, x = load(args.wav)
    if sr > 24000:
        g = np.gcd(sr, 16000); x = resample_poly(x, 16000 // g, sr // g); sr = 16000
    x = x / (np.max(np.abs(x)) + 1e-9)
    hop = int(0.01 * sr)
    f0, st = f0_track(x, sr, hop)
    regs = stable_regions(f0, hop, sr)
    print(f"{len(regs)} steady sounds found (the script has 15 vowels: 5 vowels at 3 pitches)")
    if len(regs) < 15:
        print("Fewer than 15 steady vowels found: re-record with longer, steadier notes and a pause between them.", file=sys.stderr)
    if len(regs) < 10:
        sys.exit(1)

    regs = regs[:15] if len(regs) >= 15 else regs
    items = []
    for (a, b) in regs:
        mid = (a + b) // 2
        w = int(0.06 * sr); c = mid * hop + int(0.02 * sr)
        seg = x[max(0, c - w // 2): c + w // 2]
        forms = lpc_formants(seg, sr)
        items.append({"f0": float(np.median(f0[a:b + 1])), "forms": forms, "strength": float(np.mean(st[a:b + 1]))})

    groups = [items[i:i + 5] for i in range(0, len(items) - 4, 5)]
    mid_group = groups[len(groups) // 2] if groups else items[:5]
    vowels, problems, estimated = {}, [], []
    for v, it in zip(VOWELS, mid_group):
        f = it["forms"]
        ref = REF[v]
        pick = []
        for r in ref[:3]:
            if not f: break
            best = min(f, key=lambda z: abs(z - r * 1.15))
            if best not in pick:
                pick.append(best)
        pick = sorted(pick)
        if len(pick) == 2:
            # The third formant is weak in some voices and recordings: estimate it from the first two.
            ratio = float(np.clip((pick[0] / ref[0] + pick[1] / ref[1]) / 2, 0.9, 1.3))
            pick.append(max(pick[1] + 300, ref[2] * ratio))
            estimated.append(v)
        if len(pick) < 3 or not (pick[0] < pick[1] < pick[2]):
            problems.append(v); pick = [ref[0], ref[1], ref[2]]
        vowels[v] = [round(p) for p in pick]
    if problems:
        print("Could not measure these vowels well, so the average values were used:", ", ".join(problems))

    if estimated:
        print("The third formant was estimated for:", ", ".join(estimated))
    centre = float(np.median([69 + 12 * np.log2(it["f0"] / 440) for it in mid_group]))
    strength = float(np.mean([it["strength"] for it in items]))
    breath = float(np.clip((1 - strength) * 1.2, 0.02, 0.3))
    pack = {"id": args.id, "name": args.name, "type": "profile", "license": args.license,
            "voice": {"centreMidi": round(centre, 1), "breath": round(breath, 3), "vibratoHz": 5.4, "vibratoCents": 18,
                      "brightness": 1.0, "attackMs": 14, "vowels": vowels}}
    out = args.out or args.id
    os.makedirs(out, exist_ok=True)
    with open(os.path.join(out, "voicepack.json"), "w", encoding="utf-8") as fh:
        json.dump(pack, fh, indent=2, ensure_ascii=False)
    print(json.dumps(pack["voice"], indent=2))
    print("Wrote", os.path.join(out, "voicepack.json"))


if __name__ == "__main__":
    main()
