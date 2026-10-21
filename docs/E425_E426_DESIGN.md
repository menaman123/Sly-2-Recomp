# E425 (early FINISH at EE staging) + E426 (deferred boundary) -- design note for a dedicated stage

Written by the eeperf implementation round (2026-10-20) from `$WORK/diag_ee/REPORT.md` sections 1b, 2 (lever 1), 2h and 3.
NOT implemented in this round (HIGH risk: a guest-observable schedule change). Everything below is a plan, not a result.
Line references are to the eeperf worktree at engine `fix/eeperf` (base 85b7a92) and the games copy (base 791b884).

## 1. The problem it solves (from the diag, confirmed by this round's runs)
The frame is a SERIES: kick (tid 3 walks and stages the ONE VIF1 chain, 0.6 ms) -> W (the MTVU worker replays it, 8-14 ms;
the EE idles in MtvuSync::WaitProgress) -> the drain thread's pass parses the closing PATH2 DIRECT FINISH -> Intc::RaiseDeferred ->
collected at the holder's pre-dispatch -> ISR iSignalSema -> tid 3 PATH3 x2 + FINISH x2, SignalSema(frame-done) -> L (tid 1 logic
tick, 5.5-6.5 ms) -> SyncV -> B (boundary 0.7-1.3 ms) -> next kick. W and L never overlap because the game's logic waits for
frame-done, which waits for FINISH, which waits for the worker. frame = W + L + B ~ 15-18 ms -> 49-53 Hz.
Lever 1 changes the SHAPE: raise FINISH when the EE has STAGED the chain (E425) and let the boundary run without the terminal
sync / VSync fence (E426): frame = max(W, walk + L + B). With this round's W (8-10 ms avg) that is 60 fps whenever W <= 16.7 ms.
Estimated gain (diag): -6..-7 ms/frame average, -9 heavy.

## 2. Mechanism
E425 -- the early raise.
- Today: the FINISH observer (games/.../hle/leg_interrupts.cpp:41-97, installed on GsPrivRegs::Finish, engine/gs/state/gs_priv_regs.cpp:180-183)
  is fed by the GIF PARSER when it reaches the A+D FINISH register write -- on the drain thread under MTVU (Intc::RaiseDeferred,
  collected at leg_interrupts.cpp:152 `intc.CollectDeferred()`).
- Proposed: a tag walk for A+D FINISH (0x61) inside PATH2 DIRECT payloads at the EE's staging point
  (MtvuWorker::SubmitVif1Staged / FlushVif1Staged, engine/vu/tiers/mtvu_worker.cpp:146-186) and for the EE's own PATH3 chains at
  WalkGifChain's arbiter submit. Each FINISH found increments a `pre_raised` count owned by the arbiter (it must travel with the
  packets: quiesce / savestate see it), sets CSR.FINISH and raises the INTC edge ONCE at the staging point. The parser's later
  GsPrivRegs::Finish() for that packet CONSUMES one pre-raised count and must neither set the CSR bit again (harmless) nor raise a
  second INTC edge (would double the ISR count: O4's gs_irq 1800 / intc GS edges 1800 are the pin).
- SIGNAL: Paris has none (gs_irq events = p3_finishes); a SIGNAL-using scene either gets the same pre-raise (SIGLBLID write at
  staging) or stays parser-raised. Census row per event kind before deciding.
E426 -- the deferred boundary.
- MtvuStack::FrameSync (engine/app/host/mtvu_stack.cpp:68-93) already has the `defer_boundaries_` path; the title turns it off at
  games/.../hle/leg_mtvu.cpp:30 `SetDeferBoundaries(false)`.
- The VSync `ordered_submit` (leg_mtvu.cpp:47-54: submitOrdered + drain + arbiter.DrainFence()) must drop its DrainFence (two drain
  passes = 0.37 ms/frame on the critical path); the Verify slot's grab then reads the previous picture (LastFrameHeaderCopy already,
  in wall-locked play): the fhash lane shifts a frame. It is not an oracle in play (pipeline_notes 1e(c)).
- The GIF register window (MtvuStack::GifGate::Read/Write -> FenceDrain, mtvu_stack.cpp:150-166; census gif_fences = 1200 = ONE per
  frame) waits for the drain thread to EMPTY -> re-serialises the frame once per frame and would cancel the whole gain. It must
  become a COMPOSED view: find which register / bit the game reads (a reverted trace at GifGate::Read, 1 access per frame) and answer
  it from the EE's own queue state (P3Q / APATH), as kVif1StatBusyView does for VIF1_STAT (engine/vu/tiers/mtvu_sync.cpp:66-71).

## 3. Hazard census (what the EE could observe between the early FINISH and the GS consuming the chain) -- diag 2h
| observable | Paris census | consequence | closure |
|---|---|---|---|
| VIF1 regs / VU1 mem from the EE | syncs=0:0:0 in every run (also this round's) | none for this title; MtvuSync::Sync still drains first | keep Sync |
| GIF registers (GIF_STAT / TAG window) | gif_fences=1200 = 1/frame | the fence re-serialises the frame | composed STAT view (sec 2) |
| GS CSR reads by the ISR (FUN_0016f868 tests CSR.FINISH) | not counted (reads need a counter; gs_priv_writes 12/frame) | the pre-raise must SET CSR.FINISH; the parser's Finish must not raise again | counter + the consumed-count rule; pin: one INTC edge per FINISH packet |
| SIGNAL / SIGLBLID | 0 in Paris (gs_irq 3/frame = all FINISH) | none here | census row per kind; same pre-raise or parser-raised |
| GS local memory readback | does not exist in the engine | none | -- |
| display-list RAM reuse after FINISH | every frame | safe: SubmitVif1Staged COPIES the walked bytes incl. REF data at kick time | the ChaseVif1Tail END-slot re-read already happens after the rebuild |
| DMA channel status (D1_CHCR reads flush the deferred chain) | walked at the kernel-wait point; STR=0 after the walk | unchanged | -- |
| boundary / VSync ordering | VSync job rides submitOrdered behind the frame | order unchanged; packets may be unparsed at the boundary -> the fence must go, Verify reads the previous picture | fhash lane shifts; not an oracle |
| savestate F5 capture | Quiesce (worker idle, arbiter empty, drain idle gen x2) before the gs_queue sync | a pre-raised-but-unparsed FINISH must drain to 0 before INTC/GIFS sections are written | the count lives with the arbiter; add it to the quiesce predicate |
| GUEST SCHEDULING (the real risk) | today tid 3 blocks for W every frame; lower-priority threads (level loader, mc, movie) run in that window | with FINISH at the kick's own observation point and tid 1 blocked, the ISR is dispatched in the same syscall entry and tid 3 never blocks -> the leg_interrupts.cpp:74-97 starvation class (Sly 2's level loader after the intro FMV) | the pacer park (B) still yields every frame that fits a field; a heavy frame does not. Must re-run: cold boot -> intro -> first level load (loadstall route), vg_hub.sh 9 hubs, ls_run shape; a lower-priority-thread starvation census (time since a READY lower-prio thread last ran) |

## 4. Proof plan (gates, in order; every one must pass before the lever ships)
1. Inertness proof: gate the whole lever on `worker running` (MTVU on) so it is INERT in O1 (the <=294 cold-boot oracle, headless) and
   in the MTVU-0 + tick_sync deterministic composition (O6): 0 differing frames there proves the GATING, not the lever.
2. O4 (MTVU-1 Paris 600 lockstep d0/d1, pipeline_notes 1e(b)): GS census equal (gs_draws 2034878, xgkick 3624118/3151557472),
   gs_irq 1800, intc GS edges 1800 / VBON 670 / TIM2 670, ee_yield isrs 140 / idle_isrs 0 / deferred_collects 0. syscall_count and
   trips MAY move (the schedule moves by design) and must be REPORTED, not hidden.
3. O5 + a new pin: "early FINISH raises once, the parser's Finish is consumed, CSR.FINISH set at the raise" (tests/ee or tests/gif),
   plus test_ee_sched x4 (E420-E424 wake rules), GifDeferBoundaryOrder, Boundary*, Savestate* (F5/F8 round trip with a pre-raised
   FINISH in flight: verify_b shape).
4. Routes: loadstall route (cold boot -> intro FMV -> first level load) park=none; vg_hub.sh 9 hubs park=none; ls_run shape; the
   starvation census from sec 3 (no lower-priority READY thread starved longer than on main's binary by more than a field).
5. Perf judge with lever 3 (mem lane off, this round) in place: judge_pictures p50 / p99 gap / beats on hub_ep1, hub_ep5, hub_ep0 --
   the GS consumer (ps2r-gs 90 % busy, 36-53 pictures/s) caps pictures/s; frame_hz 60 alone raises the drop rate. Report both.
6. Measurement: a per-frame timestamped protocol trace (kick, last seal, FINISH raise, ISR, frame-done, SyncV, boundary end) behind an
   env knob -- the direct measure of W / L / B that today is inferred from tick_profile + `sample`.

## 5. What this round changed that the stage should know
- Lever 3: the mem lane is OFF in play (NATIVEEE_MEM_LANE forces) -> the 60-boundary 36-44 ms hitch is gone from every judged run;
  `beats == 0` / `p99 <= 20 ms` are now reachable in principle (the host's load was the remaining beat source tonight).
- W shrank ~15 % (loop dispatch, inline flag readers, per-kick fixed cost, seal wakes): 7.55 -> 6.42 ns/bundle same-minute; W
  9.7 -> 8.0 ms/frame on the same route. The series shape is unchanged; the lever above is the only shape change left.
- The MTVU worker no longer wakes a parked producer per seal when the drain thread runs (last in-flight segment only): E425's
  "park on idle-or-event" would make even that wake unnecessary.
