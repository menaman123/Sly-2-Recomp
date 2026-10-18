#!/usr/bin/env python3
"""judge_audio.py -- judge the engine's audio stream against a PCSX2 capture of the same leg.

Inputs: OURS = a 48 kHz stereo WAV (NATIVEEE_AUDIO_DUMP; int16 or float), ORACLE = PCSX2's SPU2 output
(a 48 kHz stereo WAV, or the SDL "disk" audio driver's raw F32LE stereo file). The leg is a window of the
oracle (--oracle-window S,E seconds); the judge finds the same material in ours by cross-correlating the
RMS envelopes (50 ms windows, dB) over --ours-search (default: the whole file), then compares, over the
aligned window:

  envelope    the 50 ms RMS envelopes (dB, both channels averaged): the median level difference
              |ours - oracle| <= --max-level-db (3 dB) AND the 90th percentile of |ours - oracle| over the
              windows where both play <= --max-p90-db (8 dB; a stuck or missing voice fails this), AND
              either the Pearson correlation >= --min-corr (0.9) or the oracle's envelope is flat (its
              p90 - p10 < 10 dB, where a correlation measures noise)
  centroid    the spectral-centroid tracks (Hz, per 50 ms window, mono): the median ratio within +-25 %
              and the 75th percentile of |log2 ratio| <= 1 (the same timbre most of the time)
  gaps        no run of >= 50 ms where ours is silent (< -60 dBFS) while the oracle is not (> -45 dBFS),
              allowing +-1 window of alignment jitter
  balance     where the oracle is panned (|L-R| > 6 dB over a 50 ms window) ours leans the same way in
              >= --min-balance (0.9) of those windows
  
PASS/FAIL per criterion and for the leg (exit 0 / 1); --plot writes a PNG (envelopes, centroids, balance).
No game bytes leave this script: it reads the two streams and writes numbers and a plot.
"""
import argparse
import json
import sys
import wave

import numpy as np

SR = 48000
WIN = SR // 20  # 50 ms


def load(path):
    if path.endswith(".raw"):
        x = np.fromfile(path, dtype="<f4")
        return x[: len(x) // 2 * 2].reshape(-1, 2).astype(np.float32)
    w = wave.open(path)
    n, ch, sw, sr = w.getnframes(), w.getnchannels(), w.getsampwidth(), w.getframerate()
    raw = w.readframes(n)
    if sw == 2:
        x = np.frombuffer(raw, dtype="<i2").astype(np.float32) / 32768.0
    elif sw == 4:
        x = np.frombuffer(raw, dtype="<f4").astype(np.float32)
    else:
        sys.exit(f"{path}: unsupported sample width {sw}")
    x = x.reshape(-1, ch)
    if ch == 1:
        x = np.repeat(x, 2, axis=1)
    if sr != SR:
        sys.exit(f"{path}: sample rate {sr} != {SR}")
    return x


def envelope_db(x):
    n = len(x) // WIN
    seg = x[: n * WIN].reshape(n, WIN, 2)
    rms = np.sqrt((seg ** 2).mean(axis=1))  # (n, 2)
    return 20 * np.log10(np.maximum(rms, 1e-6))


def centroid_hz(x):
    n = len(x) // WIN
    mono = x[: n * WIN].mean(axis=1).reshape(n, WIN)
    win = np.hanning(WIN)
    spec = np.abs(np.fft.rfft(mono * win, axis=1))
    freqs = np.fft.rfftfreq(WIN, 1.0 / SR)
    power = spec ** 2
    tot = power.sum(axis=1)
    c = (power * freqs).sum(axis=1) / np.maximum(tot, 1e-12)
    c[tot < 1e-9] = np.nan
    return c


def features(x):
    """Per-window feature rows: level (dB), balance (dB), log2 centroid -- each standardised later."""
    env = envelope_db(x)
    cen = centroid_hz(x)[: len(env)]
    cen = np.log2(np.maximum(np.nan_to_num(cen, nan=1.0), 1.0))
    return np.stack([env.mean(axis=1), env[:, 0] - env[:, 1], cen], axis=1)


def align(ours_feat, oracle_feat, search_lo, search_hi):
    """Lag (in windows) at which the oracle's feature tracks best match ours inside [search_lo, search_hi):
    the sum of the per-feature Pearson correlations (level, balance, centroid), so a flat ambient bed still
    aligns on its timbre and stereo image."""
    n = len(oracle_feat)
    o = oracle_feat - oracle_feat.mean(axis=0)
    o_norm = np.sqrt((o * o).sum(axis=0))
    best, best_lag = -1e9, None
    for lag in range(search_lo, max(search_lo + 1, search_hi - n + 1)):
        seg = ours_feat[lag : lag + n]
        if len(seg) < n:
            break
        s = seg - seg.mean(axis=0)
        denom = np.sqrt((s * s).sum(axis=0)) * o_norm
        c = float(((s * o).sum(axis=0) / np.where(denom > 0, denom, 1.0)).sum())
        if c > best:
            best, best_lag = c, lag
    return best_lag, best


def judge(ours, oracle, args):
    o0, o1 = int(args.oracle_window[0] * SR), int(args.oracle_window[1] * SR)
    oracle_leg = oracle[o0:o1]
    env_or = envelope_db(oracle_leg)
    feat_ours_all = features(ours)
    s_lo = int(args.ours_search[0] * SR) // WIN if args.ours_search else 0
    s_hi = int(args.ours_search[1] * SR) // WIN if args.ours_search else len(feat_ours_all)
    lag, corr_raw = align(feat_ours_all, features(oracle_leg), s_lo, s_hi)
    if lag is None:
        return {"leg": args.leg, "pass": False, "error": "no alignment (ours shorter than the oracle window)"}
    n = len(env_or)
    ours_leg = ours[lag * WIN : lag * WIN + n * WIN]
    env_ou = envelope_db(ours_leg)
    n = min(len(env_ou), len(env_or))
    env_ou, env_or = env_ou[:n], env_or[:n]
    sum_ou, sum_or = env_ou.sum(axis=1) / 2, env_or.sum(axis=1) / 2
    active = (sum_ou > -60) | (sum_or > -60)
    env_corr = float(np.corrcoef(sum_ou[active], sum_or[active])[0, 1]) if active.sum() > 4 else 0.0
    both = (sum_ou > -60) & (sum_or > -60)
    diff = (sum_ou - sum_or)[both]
    level_diff = float(np.median(diff)) if both.any() else 0.0
    p90_diff = float(np.percentile(np.abs(diff), 90)) if both.any() else 0.0
    or_range = float(np.percentile(sum_or[sum_or > -60], 90) - np.percentile(sum_or[sum_or > -60], 10)) if (sum_or > -60).any() else 0.0
    # centroid
    c_ou, c_or = centroid_hz(ours_leg)[:n], centroid_hz(oracle_leg)[:n]
    ok = both & ~np.isnan(c_ou) & ~np.isnan(c_or)
    cen_corr = float(np.corrcoef(c_ou[ok], c_or[ok])[0, 1]) if ok.sum() > 4 else 0.0
    cen_ratio = float(np.median(c_ou[ok] / np.maximum(c_or[ok], 1.0))) if ok.any() else 0.0
    cen_p75_log2 = float(np.percentile(np.abs(np.log2(np.maximum(c_ou[ok], 1.0) / np.maximum(c_or[ok], 1.0))), 75)) if ok.any() else 0.0
    # gaps: ours silent while oracle plays, with +-1 window jitter
    ours_sil = sum_ou < -60
    or_play = sum_or > -45
    or_play_near = or_play.copy()
    or_play_near[1:] &= or_play[:-1]
    or_play_near[:-1] &= or_play[1:]
    gap_windows = ours_sil & or_play_near
    gap_windows[1:] &= ours_sil[:-1]  # a run of >= 2 windows = >= 100 ms (one window of jitter allowed)
    gaps = int(gap_windows.sum())
    # balance
    bal_or = env_or[:, 0] - env_or[:, 1]
    bal_ou = env_ou[:, 0] - env_ou[:, 1]
    panned = (np.abs(bal_or) > 6) & both
    agree = float((np.sign(bal_ou[panned]) == np.sign(bal_or[panned])).mean()) if panned.any() else 1.0
    res = {
        "leg": args.leg,
        "oracle_window_s": args.oracle_window,
        "ours_offset_s": lag * WIN / SR,
        "windows": int(n),
        "envelope_corr": round(env_corr, 4),
        "level_diff_db": round(level_diff, 2),
        "level_p90_absdiff_db": round(p90_diff, 2),
        "oracle_env_range_db": round(or_range, 1),
        "centroid_corr": round(cen_corr, 4),
        "centroid_ratio": round(cen_ratio, 3),
        "centroid_p75_log2": round(cen_p75_log2, 3),
        "gap_windows": gaps,
        "panned_windows": int(panned.sum()),
        "balance_agree": round(agree, 3),
    }
    checks = {
        "envelope": abs(level_diff) <= args.max_level_db and p90_diff <= args.max_p90_db and (env_corr >= args.min_corr or or_range < 10.0),
        "centroid": 0.75 <= cen_ratio <= 1.25 and cen_p75_log2 <= 1.0,
        "gaps": gaps == 0,
        "balance": agree >= args.min_balance,
    }
    res["checks"] = checks
    res["pass"] = all(checks.values())
    if args.plot:
        plot(args, env_ou, env_or, c_ou, c_or, bal_ou, bal_or, res)
    return res


def plot(args, env_ou, env_or, c_ou, c_or, bal_ou, bal_or, res):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    t = np.arange(len(env_ou)) * WIN / SR
    fig, ax = plt.subplots(3, 1, figsize=(14, 9), sharex=True)
    ax[0].plot(t, env_or.sum(axis=1) / 2, label="PCSX2", color="k", lw=0.8)
    ax[0].plot(t, env_ou.sum(axis=1) / 2, label="ours", color="tab:red", lw=0.8, alpha=0.8)
    ax[0].set_ylabel("RMS dBFS (50 ms)")
    ax[0].set_ylim(-80, 0)
    ax[0].legend(loc="lower right")
    ax[0].set_title(f"{args.leg}: envelope corr {res['envelope_corr']}, level diff {res['level_diff_db']} dB (p90 |diff| {res['level_p90_absdiff_db']}), gaps {res['gap_windows']} -> {'PASS' if res['pass'] else 'FAIL'}")
    ax[1].plot(t, c_or, color="k", lw=0.8, label="PCSX2")
    ax[1].plot(t, c_ou, color="tab:red", lw=0.8, alpha=0.8, label="ours")
    ax[1].set_ylabel("spectral centroid Hz")
    ax[1].set_title(f"centroid corr {res['centroid_corr']}, ratio {res['centroid_ratio']}")
    ax[2].plot(t, bal_or, color="k", lw=0.8, label="PCSX2")
    ax[2].plot(t, bal_ou, color="tab:red", lw=0.8, alpha=0.8, label="ours")
    ax[2].set_ylabel("L-R dB")
    ax[2].set_ylim(-30, 30)
    ax[2].set_title(f"balance agreement {res['balance_agree']} over {res['panned_windows']} panned windows")
    ax[2].set_xlabel("s (leg time)")
    fig.tight_layout()
    fig.savefig(args.plot, dpi=90)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--ours", required=True)
    ap.add_argument("--oracle", required=True)
    ap.add_argument("--leg", default="leg")
    ap.add_argument("--oracle-window", type=float, nargs=2, required=True, metavar=("S", "E"))
    ap.add_argument("--ours-search", type=float, nargs=2, metavar=("S", "E"))
    ap.add_argument("--min-corr", type=float, default=0.9)
    ap.add_argument("--max-level-db", type=float, default=3.0)
    ap.add_argument("--max-p90-db", type=float, default=8.0)
    ap.add_argument("--min-balance", type=float, default=0.9)
    ap.add_argument("--plot")
    ap.add_argument("--json")
    args = ap.parse_args()
    res = judge(load(args.ours), load(args.oracle), args)
    if args.json:
        with open(args.json, "w") as f:
            json.dump(res, f, indent=1)
    for k, v in res.items():
        if k != "checks":
            print(f"{k}: {v}")
    for k, v in res.get("checks", {}).items():
        print(f"  {k}: {'PASS' if v else 'FAIL'}")
    print(f"{res['leg']}: {'PASS' if res['pass'] else 'FAIL'}")
    sys.exit(0 if res["pass"] else 1)


if __name__ == "__main__":
    main()
