# native/snd -- Sly 2's native sound driver (989snd + spiop)

The IOP's sound stack in Sly 2 is SCEA's **989snd_Library** (v3.01, exports `snd989` v1.08) plus Sucker Punch's
**spiop** streaming module, both embedded in the ELF and recompiled at build time (`iop/`). The EE drives them
over two SIF RPC servers 989snd registers itself: **0x123456** (the 107-entry command dispatcher) and **0x123457**
(the blocking bank load). This directory is the title's native answer to that traffic, behind
`title.native.iop` (and `title.native.spu` for the SPU face), with the hosted modules as fallback and oracle.

## What is native today (exact: lane-identical), what is forwarded

| fno | 989snd body | native | why / what it needs |
|---|---|---|---|
| 10 | `snd_GetGroupVolume(group)` = `g[0x52418+group*4]` (fn_4e220) | **yes** | reads the module's table (fno 9 writes it) |
| 26 | `snd_BankSoundIsValid(bank, index)` (fn_3fac8 / fn_39b24 for `SBlk`) | **yes** | pure over the bank header in IOP RAM |
| 90 | movie push: `SdVoiceTrans(chan, 0, iop_buf, spu_base+off, n)` + status (fn_3e5c0) | **yes** | the movie block's words (snd989_image.h) |
| 91 | movie read position: `SdGetAddr(NAX of movie voice) - spu_base` (fn_3e610) | **yes** | same |
| 76 | external command `{'SUKR', op, args}` (fn_3318c -> spiop's handler) | **ops 0x0b / 0x0c only** | pure reads of spiop's context word (0x5a29c -> ctx[0x20], 0x5a2c0 -> ctx[0x2c]; ctx = g[0x5f524]); the handler address is checked |
| 77 | the batch `{count, {u16 fno, u16 len, body}...}` | when **every record** is native | the reply-buffer semantics do not split |
| 0, 8, 24, 54, 59, 60, 61, 62, 76, 104 ; records 9, 33, 76 (SUKR), 92 ; bank 3 | init, bank load, silence-all, stream init, movie open/start/stop/close, external (SUKR) commands, group volume, sound params, play bank sound | **forwarded** | they mutate state the hosted module keeps relying on: the voice allocator (fn_4c750), the SPU-RAM allocator (fn_4272c), the 240 Hz tick's pending key-on/off and VMIX masks, banks, streams |

Boot lane (600 frames, boot.pad): `iop=on:983/199` -- 983 of the 1182 0x123456 round trips native (the 227
one-record SUKR-poll batches among them), replies and libsd ops byte-identical to the hosted run, the lane row =
THE BASELINE. Paris hub lane (900 frames): `900/3292` -- every round trip is a one-record batch of fno 76 (SUKR
op 6 x1727 / op 0xc x900), 33 (x1499) or 92 (x66); the per-frame poll is native, the rest is the stream + SFX
path that remains hosted (its tick does ~130k libsd ops per 900 frames).

## The two exactness rules (found on the lane, both required)

1. **The reply buffer is state.** The dispatcher (fn_3497c) never writes the result word: `result_ptr = reply+4;
   shim(buff); *(result_ptr+4) = -1; return reply`. Only a shim with a value stores it (`sw $v0, (result_ptr)`);
   a void shim (fno 90) leaves the previous caller's word, and the EE copies 12 bytes of the buffer. The batch
   shim (fn_34880) advances `result_ptr` by 4 per record and steps back 4 at the end. The native server answers
   THROUGH THE MODULE'S BUFFER (`g[0x5063c]`): it stores only when the shim would, writes the terminator, copies
   the first 12 bytes -- so hosted and native answers interleave exactly. (This was the earlier native RPC server's unexplained
   2-byte EE RAM delta at 0x443844: fno 90's reply carried the last fno-91 word in the hosted arm.)
2. **The kernel pass.** A hosted round trip is `SifRpcBridge::RunOnRpcThread -> DriveUntil`: one
   `IopKernel::Dispatch()` pass at the current IOP clock, which fires the due hard-timer beat (989snd's 240 Hz tick
   thread) and runs every ready thread. A native answer runs the same pass after its body (`Deps::drain_kernel`);
   without it the tick's key-offs slide after later pushes and audio/mem/syscalls all move.

Rule 3: nothing the module later relies on is mutated natively. Rule 4: a body declines (forwards) on state it
cannot read; a batch is dry-checked first so a declined batch is forwarded whole with no op applied twice.

## Files

- `snd_rpc_protocol.h` -- the EE<->IOP protocol (sids, fno names, the batch walker, SUKR ops, voice entries).
- `snd989_image.h` -- the layout facts of THIS 989snd image the bodies read (offsets + rules; no game bytes).
- `native_snd_server.{h,cpp}` -- `Snd989Server` (RpcRouter::Server): the dispatcher, the reply-buffer model, the
  bodies, the faces (`LibsdFace` = LibsdModule::Call, the hosted module's own entry and tap; `SpuCoreFace` =
  SpuCore directly, `title.native.spu`).
- `test_snd_replay.cpp` -- THE RPC-REPLAY ORACLE TEST: a NATIVEEE_RPC_TRACE of the hosted lane replayed through
  the server; replies + libsd op sequences compared per round trip (`sly2_test_snd_replay TRACE [--iop-ram
  <NATIVEEE_RAM_DUMP>.iop]`; no args = the synthetic reply-model self-check ctest runs).
- The installer is `../native_iop.{h,cpp}` (IopHost::SetServerObserver takes the router rows when 989snd
  registers them; IopHost::Memory() is the module's RAM; the kernel pass is IopHost::Table().Kernel().Dispatch()).

## The static closure of the IOP unit (from the ISO, not from runs)

- Modules the disc can load: none as files (the volume root is SYSTEM.CNF, SCUS_973.16, IOPRP300.IMG,
  Z6TAIL.DUP); the nine IOP modules are in the ELF's `.irx` section 0x472200..0x4e5e40 (TITLE_INPUTS.sha256);
  lgAud, 989snd_Library, spiop (+ IOPRP300's FILEIO) are recompiled at build time, MANIFEST-pinned. The code
  input of the unit is closed by construction.
- The EE's 989snd traffic (a static closure over the ELF's RPC call sites): the
  two wrappers 0x2325e0 (sync) and 0x232838 (queued as a batch record when the batch flag 0x2c4684 is set) have
  30 call sites with fno = $a0: **0x123456 fnos {0, 6, 8, 9, 10, 11, 19, 20, 21, 22, 23, 24, 25, 26, 33, 34, 54,
  59, 60, 61, 62, 64, 76, 90, 91, 92, 104} + 77 (the flush, 0x232bf8) = 28; 0x123457 {3}** (0x231fb0). The lanes
  exercise 15 of the 28 (the boot lane) and 4 (the hub).
- SUKR ops (the 12 `lui 0x524b` sites): **{0x0, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7, 0x8, 0xa, 0xb, 0xc}** through the
  fno-76 builders 0x233048 / 0x233088 / 0x2330d0, and **0x1** (open) through 0x233130 = fno 104 (the 0x24-byte
  record). spiop registers its command record (`'SUKR'`, 14 handlers: snd989_image.h SpiopImage) with
  snd989:93 = fn_33000 (`snd_RegisterExternalModule`: appended to the list at `g[0x50644]`); 989snd's fn_3318c /
  fn_33250 dispatch fno 76 / 104 to `handlers[op]`. (989snd's export table is at 0x4f368 with the standard 20-byte
  IRX header: export N = word N of the function list; 0..3 reserved.)

## The remaining port (the stream + SFX path): what it is, from the disassembly

THE WAY IN (proven by the bodies above): the module's RAM is the state, and a native body may READ AND WRITE it.
A 989snd function ported as structured C++ over the module's own data layout is exact by construction and the
hosted module continues from the state it leaves -- so the port can proceed function by function, each step
gated by the lane and the replay test, the 240 Hz tick last; moving the data into a native struct is the final,
mechanical step once nothing hosted reads it. Two kernel objects have to be honoured on the way: 989snd's global
lock (fn_42f58 / fn_430dc: owner tid `g[0x5229c]`, depth `g[0x52214]`, tag stack `g[0x52218..]`, the semaphore
`g[0x52528]` -- a body that would block on it must forward instead) and spiop's per-context semaphore (fn_5af94).
Measured sizes (instructions, static call closure to depth 3 from the disassembly):
- SUKR op 6 (stream set volume/pan, 1727/900 frames on the hub): fn_5aa98 (30) -> fn_5af94 (56) + fn_5bb38 (60,
  the stream record's vol/pan) -> snd989:32 = fn_3fba0 (37, `snd_SetSoundVolPan`, dispatch on handle type 4/5) ->
  fn_4b61c (249, the stream kind: fn_4e628 (267, the volume law), fn_4ed08 (45, the group scaling over
  `g[0x52418]`), fn_417f8, fn_4234c, fn_4b394, fn_4b3d8, fn_4c1ac/fn_4c2f8 (a second lock/unlock pair)) -- ~1100.
- fno 33 (sound parameter update, 1499/900): fn_3fc34 (66) -> fn_35c14 (312), fn_3fba0, fn_3ff4c, fn_3ffb0 ... ~1830.
- fno 92 (play bank sound, 66/900): fn_3f820 -> the voice allocator fn_4c750, the bank's tone table, ~17 SetParam
  + SetSwitch + Note2Pitch; static reach 60 functions / 5.6k.
- THE TICK: the hard timer (AllocHardTimer(1, 0x20, 1), fn_3a4ac+) wakes the tick thread fn_3a8d8 (SleepThread ->
  fn_4325c -> SignalSema g[0x518f0]) through the callback fn_3a90c (iWakeupThread). fn_4325c (247) + direct callees
  ~955: fn_431bc (the PollSema trylock), SdGetParam ENVX voice-end polls, fn_4cf5c / fn_4d074 (the pending KON /
  KOFF + VMIX masks -> SdSetSwitch), fn_4efb4 (156, the fades), fn_4d184 / fn_4dec0 (streams: NAX polls, 0x1000-byte
  VoiceTrans halves, LSAX), sceCdReadIOPm (the tick itself reads the disc for streams), fn_35a04 (sound-end handlers).
The hosted work that remains is one coupled machine; the pieces, with their 989snd functions:
- the **voice allocator** fn_4c750 (priority, 48 voices; the movie open takes two at priority 0x7f) and the
  **SPU-RAM allocator** fn_4272c (the movie region 0x19040 is reused by banks);
- the **tick** (hard timer, AllocHardTimer(1, 0x20, 1) at fn_3a4ac+, the tick thread 0x18f4 woken per beat): applies
  the per-core pending KON/KOFF/VMIX masks (`SetSwitch 0x15xx/0x16xx/0x18xx..0x1bxx`), polls ENVX (`GetParam
  0x5xx`) for voice end, steps volume fades (the table at 0x52498), refills streams (`GetAddr NAX` polls, 0x1000-byte
  `VoiceTrans` halves, `SetAddr` LSAX);
- **spiop**'s 14 SUKR handlers over 989snd's stream exports (snd989:25/27/28/29/32/48/49/93/99/119/121/126) and
  cdvdman reads (sceCdRead / sceCdSearchFile with the hosted path's completion timing);
- **banks**: fno 3's blocking EE->IOP pull (the `DMA>` handshake at EE 0x448400/0x448410) + VoiceTrans to SPU RAM,
  fno 92's play (~17 SetParam + SetSwitch + Note2Pitch from the bank's tone table), fno 33's parameter update.
A body that owns any of this must own ALL of it (the verdict of nat-iop-spu.md stands); the exact order of
operations is the recompiled module's, available as C++ in `<build>/generated/.../iop/snd989/` and as a
disassembly via `tools/irx_disasm.py`. The reply-buffer model and the kernel pass above are the two framework
pieces that port carries unchanged.

## Portability

No POSIX, no `_WIN32` / `__APPLE__`; the engine's types only. `tools/ci/win_cross_smoke.sh --tu-dir native/snd`
compiles both TUs for x86_64-pc-windows-msvc.
