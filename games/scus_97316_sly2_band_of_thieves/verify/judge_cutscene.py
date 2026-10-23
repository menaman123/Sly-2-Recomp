#!/usr/bin/env python3
"""judge_cutscene.py -- the binocucom cutscene judge (round 5, cutscene track): is the dialog timeline alive and paced
like the console?

    python3 -P judge_cutscene.py judge <frames dir> [<mixer.wav>]            # ours: --dump-frames every 10 + NATIVEEE_AUDIO_DUMP(_SOURCE=mixer)
    python3 -P judge_cutscene.py pcsx2 <frames dir> <audio.raw> <f0> <f1>  # the PCSX2 SaveFrame twin (513x448 field dumps, F32LE audio)

Measures, per dumped frame (ours: 512x447 PPM named p<key>.ppm, key = output ordinal ~ boundary; PCSX2: *_f<frame>_fr1_*.png,
the 512x224 field in the upper half):
  text    glyph pixels in the binocucom text box (x 80..300 of 512, the lower 52 of 447 rows; max(RGB) > thr). The game's
          typewriter is a RAMP per sentence group (0 -> full in ~200 frames), reset to 0 at the next group.
  motion  mean |diff| of consecutive dumps in the left portrait (x 15..95, y 300..420 of 447): the lip-sync animation.
  voice   300-3400 Hz band energy of the audio per 1/60 s (ours: the mixer dump is boundary-indexed, 800 frames per
          boundary; PCSX2: emulated time at 59.94 Hz); an onset = above -30 dBFS after >= 24 quiet frames.
Gates (frozen from the PCSX2 2.7.329 twin of the 'Satellite Sabotage' call, $WORK/wt/cutscene/pcsx2_a, 2026-10-23:
ramps at f4390 / 4690 / 5240 / 5750, ramp duration 150-230 frames, portrait changed pairs 34 %, text lag behind the voice
onset -8..62 frames; the frozen-Count engine: 0 ramps, 11 % changed pairs):
  T1 typewriter alive          >= 3 ramps (a ramp = text growing from <= 20 px to > 300 px over >= 50 frames) PCSX2 4
  T2 typewriter pace           median ramp duration (ramp start -> plateau) within 150..350 frames        PCSX2 285
  T3 lip-sync alive            portrait changed pairs (> 2.0 mean abs over 10 frames) >= 25 %            PCSX2 34 %, frozen 11 %
  T4 text follows the voice    median (ramp start - last voice onset) within -30..+150 frames             PCSX2 +9..+62; the
                               brief's 100 ms target is NOT resolvable with this detector (the ducked hub music and the
                               typewriter blips share the band): the gate is a 2.5 s sanity bound, the number is reported
Exit 0 = every gate PASS, 1 = a gate FAIL, 2 = usage.
"""
import glob
import os
import re
import sys
import wave

import numpy as np
from PIL import Image

RAMP_LO, RAMP_HI = 20, 300


def _key(path):
    m = re.search(r'(?:^p|_f)(\d+)', os.path.basename(path))
    return int(m.group(1)) if m else None


def load_frames(d, lo=0, hi=10**9):
    fs = [f for f in glob.glob(os.path.join(d, '*')) if _key(f) is not None and (f.endswith('.ppm') or f.endswith('.png'))]
    fs = sorted(fs, key=_key)
    return [(k, f) for k, f in ((_key(f), f) for f in fs) if lo <= k <= hi]


def text_and_motion(frames, pcsx2):
    thr = 90 if pcsx2 else 60
    text, motion, prev = {}, {}, None
    for k, f in frames:
        im = Image.open(f).convert('RGB')
        a = np.array(im)
        g = np.array(im.convert('L')).astype(float)
        if pcsx2:
            box = a[197:224, 80:300]
            por = g[150:210, 15:95]
        else:
            box = a[395:447, 80:300]
            por = g[300:420, 15:95]
        text[k] = int((box.max(axis=2) > thr).sum())
        if prev is not None:
            motion[k] = float(np.abs(por - prev).mean())
        prev = por
    return text, motion


def ramps(text):
    ks = sorted(text)
    out = []
    for i in range(1, len(ks)):
        if text[ks[i - 1]] <= RAMP_LO and text[ks[i]] > RAMP_LO:
            start = ks[i]
            peak, plateau = text[start], None
            for j in range(i, len(ks)):
                v = text[ks[j]]
                if v <= RAMP_LO:
                    break
                if v >= peak:
                    peak, plateau = v, ks[j]
            # a typewriter ramp GROWS for >= 50 frames; a header / notice pops in one frame (excluded)
            if peak > RAMP_HI and plateau is not None and plateau - start >= 50:
                out.append((start, plateau - start))
    return out


def band_db(x, fs, fpf):
    from scipy.signal import butter, sosfilt
    sos = butter(4, [300, 3400], btype='band', fs=fs, output='sos')
    y = sosfilt(sos, x)
    nb = int(len(y) // fpf)
    return np.array([20 * np.log10(np.sqrt((y[int(k * fpf):int((k + 1) * fpf)] ** 2).mean()) + 1e-9) for k in range(nb)])


def onsets(e, thr=-30.0, quiet_min=24):
    quiet, out = 0, []
    for k in range(len(e)):
        if e[k] > thr:
            if quiet >= quiet_min:
                out.append(k)
            quiet = 0
        else:
            quiet += 1
    return out


def load_audio(path, pcsx2):
    if pcsx2:
        x = np.fromfile(path, dtype='<f4').reshape(-1, 2).mean(axis=1)
        return band_db(x, 48000, 48000 / 59.94)
    w = wave.open(path)
    x = np.frombuffer(w.readframes(w.getnframes()), dtype=np.int16).reshape(-1, 2).mean(axis=1) / 32768.0
    return band_db(x, 48000, 800)


def judge(frames_dir, audio, pcsx2, lo=0, hi=10**9):
    frames = load_frames(frames_dir, lo, hi)
    if len(frames) < 10:
        print(f'judge_cutscene: {len(frames)} frames in {frames_dir} -- nothing to judge')
        return 2
    text, motion = text_and_motion(frames, pcsx2)
    rs = ramps(text)
    # the lip-sync window = the dialog: from 100 frames before the first ramp to 100 after the last plateau
    if rs:
        w0, w1 = rs[0][0] - 100, rs[-1][0] + rs[-1][1] + 100
        mot = [v for k, v in motion.items() if w0 <= k <= w1]
    else:
        mot = list(motion.values())
    motion = mot
    changed = sum(1 for v in motion if v > 2.0)
    frac = 100.0 * changed / max(1, len(motion))
    lags = []
    if audio:
        e = load_audio(audio, pcsx2)
        ons = onsets(e)
        for start, _ in rs:
            prev = [o for o in ons if o <= start + 10]
            if prev:
                lags.append(start - prev[-1])
    dur = float(np.median([d for _, d in rs])) if rs else 0.0
    lag = float(np.median(lags)) if lags else None
    rows = [
        ('T1 typewriter alive: ramps >= 3', f'{len(rs)} ({", ".join(str(s) for s, _ in rs)})', len(rs) >= 3),
        ('T2 typewriter pace: median ramp 150..350 frames', f'{dur:.0f}', 150 <= dur <= 350),
        ('T3 lip-sync alive: portrait changed pairs >= 25 %', f'{frac:.0f} % ({changed}/{len(motion)}, mean {np.mean(motion) if motion else 0:.2f})', frac >= 25),
    ]
    if audio:
        rows.append(('T4 text follows the voice: median lag -30..+150 frames', 'n/a' if lag is None else f'{lag:.0f} ({", ".join(str(l) for l in lags)})',
                     lag is not None and -30 <= lag <= 150))
    print(f'judge_cutscene {"pcsx2" if pcsx2 else "ours"} frames={len(frames)} [{frames[0][0]}..{frames[-1][0]}] text max={max(text.values())}')
    print('| gate | value | verdict |\n|---|---|---|')
    ok = True
    for name, val, good in rows:
        print(f'| {name} | {val} | {"PASS" if good else "FAIL"} |')
        ok = ok and good
    print('PASS' if ok else 'FAIL')
    return 0 if ok else 1


def main(argv):
    if len(argv) >= 3 and argv[1] == 'judge':
        return judge(argv[2], argv[3] if len(argv) > 3 else None, False)
    if len(argv) >= 6 and argv[1] == 'pcsx2':
        return judge(argv[2], argv[3], True, int(argv[4]), int(argv[5]))
    print(__doc__)
    return 2


if __name__ == '__main__':
    sys.exit(main(sys.argv))
