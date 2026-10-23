# Input -> photon latency budget (Sly 2, nativeee, round 5)

One button press, in the order the pipeline runs it. "field" = 16.67 ms at 60 Hz. Numbers are Paris
(`hub_ep1_fix`, 60 pictures/s, Metal windowed) on a quiet host unless noted; the census is run.meta
`latency=` + `present_times.csv` lanes (`input`, `pad`, `boundary`, `drained`, `consumed`, `output`,
`shown`), judged by `verify/judge_latency.py` (host part) and `lat_phase.py` (phases vs the pad lane).

## The stages

| # | stage | who | before (round 4) | after (L3' + L2 shipped) | nature |
|---|---|---|---|---|---|
| H1 | input edge -> pad sample | host: the pad is sampled once per boundary (BoundarySequencer slot 4, right after the 60 Hz wall-edge park) from a pump that just drained SDL | 0..1 field uniform, p50 ~0.5 | unchanged | floor: one sample per frame, like the console's own pad poll |
| G | pad sample -> the picture that carries it | **the game**: logic thread -> display-list thread -> flip | **4-5 fields** (menu S+5, character S+6; lockstep hash divergence, sec 1.1 of REPORT.md) | unchanged | **the console's own pipeline -- the floor; not the host's to cut** |
| H2 | boundary -> worker drained | host: VU1 worker (MTVU), run-ahead cap `engine.mtvu_depth`=2 | Paris 0.3 field (5.7 ms p50); hub_ep3/Cairo under host load 1-4 fields | unchanged (L1 hold: see below) | host; scene- and load-dependent |
| H3 | drained -> consumed -> output (picture posted) | host: the consumer's draws + the present capture. Round 4 DEFERRED the capture: picture N was read back at Present(N+1) | **~1 field + 2 ms** (output - boundary 22 ms p50) | **~7 ms** (L3' `video.capture_wait_ms=8`: the consumer waits <= 8 ms for THIS present's capture; 0.8-0.9 ms per frame in practice, 0 timeouts) | host; cut by one field |
| H4 | output -> shown | host: the presenter's 60 Hz admission train. Round 4 anchored the train to the LATEST arrival phase (never earlier): every typical picture waited ~15 ms | **15-29 ms p50** | **4-12 ms p50** (L2 `video.present_relax=on`: the train converges to 3 ms after the latest typical arrival, <= 4 ms a window; a late picture still slips and the train re-anchors later, so in a minute with irregular production the gain is smaller) | host; cut by ~10 ms |
| H5 | shown -> photon | the compositor / display (video.vsync off: the next refresh) | 0..1 display frame | unchanged | not measured |

Host budget `input -> shown` (H1 + H3 + H4 around the game's own G, the judge's gate: p50 <= 3 fields,
p99 <= 4), same-minute pairs on 2026-10-23 p.m. (REPORT.md sec 6): Paris Metal **3.1-3.8 / 4.1-4.3
fields p50/p99 before -> 1.6-2.0 / 2.6-3.8 after**; hub_ep3 Metal 2.8-4.0 / 3.3-6.8 -> 1.4-1.7 /
2.1-2.2; Cairo Metal 3.0 / 3.5 -> 1.3 / 1.7-2.6. `shown(N+1) - pad(N)` (the picture that can first
carry a pad sample, on the glass): Paris Metal 51-53 -> 17-26 ms, Paris GL 50-56 -> 15-17 ms,
hub_ep3 Metal 38-54 -> 15-21 ms, Cairo Metal 40-42 -> 10-14 ms, Cairo GL 50 -> 13.5 ms. Total to
the glass with the game's 4-5 fields: ~8-9 fields (140-150 ms) before -> ~6 fields (100 ms) after,
plus the display's pickup.

## What is shipped (defaults) and what is not

- `video.capture_wait_ms=8` (L3', prompt present capture; Metal `WaitCaptureBounded`, GL
  `WaitDeferredCaptureBounded`) and `video.present_relax=on` (L2, FramePacer relax): default ON
  after the gates (see REPORT.md sec 3): Paris 600 lockstep census byte-equal to the round-4 binary
  (gs_draws, xgkick kicks, syscall_count, intc, audio.ahash, mem.mhash), the <=294 cold-boot
  frame-hash oracle at 0, w_final.ppm non-black, judge_pacing GREEN on Paris Metal, 60 pictures/s.
  Both are fingerprint-excluded (WHEN a picture is captured / shown, never WHICH) and the
  deterministic profile pins them off.
- `engine.mtvu_depth_hold` (L1): redesigned this round as a hold on the WORKER's lag (the EE parks
  only while more than `hold` complete frames are queued ahead of the VU1 worker -- never while the
  worker could starve), replacing the presenter-depth hold that cost GL throughput (28 vs 49
  pictures/s). Default OFF: when the worker keeps up it is inert (1-9 parks per 900 frames, no
  latency change), and when the worker's lag is caused DOWNSTREAM (the GL consumer slow in hub_ep3:
  the worker waits for ring room in bursts) parking the EE empties the worker's queue between
  bursts -- 119 hold parks, drained p90 34 vs 9 ms, 17 % doubled frames vs 0.9 % (REPORT.md sec
  7). A hold that pays would have to see WHY the worker lags (park only while the worker is
  computing, never while it waits for the consumer's ring); not built this round.
- The prompt capture's gate: the wait is skipped when a COMPLETE frame is already queued behind the
  consumer (`DrawIrThreadQueue::PendingFrames() == 0`), so a consumer that is a frame behind keeps
  its CPU/GPU overlap (GL hub_ep3: 9.8 % -> 1.4 % doubled frames).
- The game's 4-5 fields (G) are the title's own double/triple-buffered logic -> render -> flip
  chain, proven by the deterministic lockstep divergence experiment (the same pictures byte-equal up
  to the frame tagged S+5/S+6 for a press at pad stamp S). A console pays them too; cutting them
  would mean running the game's threads differently from the title's own scheduling (run-ahead /
  frame-skip of the logic thread), which is out of scope for a faithful recompilation.

## Reading a run

`python3 -P verify/judge_latency.py judge pipe_<name>` -- stages in ms and fields, p50/p99/max,
matched/missing, the host gates, the total with the game's depth as an informational row
(`GAME_DEPTH_EXTRA_FIELDS` = 4). `python3 -P $WORK/wt/latency/lat_phase.py pipe_<name>` -- the
arrival phases vs the pad lane (output(N+1)-pad(N), shown-output, drained-boundary). A player's own
presses are stamped too: `play.sh`'s run.meta carries `latency=... edges=N`.
