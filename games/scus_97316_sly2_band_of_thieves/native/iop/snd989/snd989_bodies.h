// snd989_bodies.h -- the ported 989snd_Library functions (one declaration per recompiled function, named by
// its address in the module as loaded at 0x33000) and the table the installer puts in the module's seam.
#pragma once

#include <cstdint>

#include "games/scus_97316_sly2_band_of_thieves/native/iop/snd989/snd989_abi.h"
#include "games/scus_97316_sly2_band_of_thieves/native/iop/snd989/snd989_state.h"

namespace ps2r::title::sly2::snd989 {

struct PortedFn {
    uint32_t addr;        // the function's address in the module (its seam slot)
    PlainFn body;         // the host body
    const char* name;     // the role (OpenGOAL's name where it has one)
};
// Every ported function, in address order. RegisterSnd989Natives (snd989_native.cpp) Add()s each to the module.
extern const PortedFn kPortedFns[];
extern const uint32_t kPortedFnCount;

// snd989_tick.cpp -- the 240 Hz tick and what it reaches directly.
void fn_35140(IopContext&);   // snd_ReportError(code, a, b, c, d)
void fn_42f58(IopContext&);   // snd_Lock(tag)
void fn_430dc(IopContext&);   // snd_Unlock()
void fn_431bc(IopContext&);   // snd_TryLock() (the tick's)
void fn_4325c(IopContext&);   // snd_Tick(): the 240 Hz step
void fn_43638(IopContext&);   // snd_GetTick()
void fn_4cf5c(IopContext&);   // voice_ApplyKeyOns()
void fn_4d074(IopContext&);   // voice_ApplyKeyOffs()
void fn_4d184(IopContext&);   // voice_Ended(v)
void fn_4d318(IopContext&);   // voice_KeyOff(v)

// snd989_volume.cpp -- master volume, ramps, group fades, the voice-table lock.
void fn_3e1cc(IopContext&);   // ramp_Done(slot)
void fn_369c8(IopContext&);   // master_StepRamps()
void fn_40eb8(IopContext&);   // master_SetVolume(core, l, r)
void fn_40f64(IopContext&);   // master_Tick()
void fn_4c1ac(IopContext&);   // voice_Lock(wait, tag)
void fn_4c2f8(IopContext&);   // voice_Unlock()
void fn_4edbc(IopContext&);   // voice_RefreshGroupVolumes(groups)
void fn_4efb4(IopContext&);   // fade_Step()

// snd989_seq.cpp -- the sound-handle list, the sequencer step, the stop path.
void fn_35a04(IopContext&);   // endhandler_Poll()
void fn_377a4(IopContext&);   // seq_Tick(h)
void fn_3926c(IopContext&);   // seq_Step(h)
void fn_41da0(IopContext&);   // snd_StopHandle(h, force, release, why)
void fn_4218c(IopContext&);   // sndlist_Tick()
void fn_4226c(IopContext&);   // timer_Walk(node, h)

// snd989_rpc.cpp -- the 0x123456 RPC face and the hot shims.
void fn_3318c(IopContext&);   // snd_ExternalCommand(tag, op, ...)
void fn_33148(IopContext&);   // snd_FindExternalModule(tag)
void fn_33534(IopContext&);   // shim fno 9 (group volume)
void fn_338bc(IopContext&);   // shim fno 26
void fn_344c4(IopContext&);   // shim fno 76
void fn_34158(IopContext&);   // shim fno 90
void fn_34184(IopContext&);   // shim fno 91
void fn_3497c(IopContext&);   // rpc_Dispatch(fno, buff, size)
void fn_34880(IopContext&);   // rpc_Batch(buff) (fno 77)
void fn_35200(IopContext&);   // snd_VolumeCurve(_, i)
void fn_39b24(IopContext&);   // bank_BlockSoundIsValid(bank, index)
void fn_3e5c0(IopContext&);   // movie_Push(size, offset)
void fn_3e610(IopContext&);   // movie_ReadPosition()
void fn_3fac8(IopContext&);   // snd_BankSoundIsValid(bank, index)

// snd989_stream.cpp -- the streamed sound handle's tick.
void fn_44768(IopContext&);   // stream_NextChunk(h, kind)
void fn_45904(IopContext&);   // stream_Tick(h)
void fn_45ccc(IopContext&);   // stream_PollVoice(st, set_flag)
void fn_461e8(IopContext&);   // stream_AccountPlayed(st, pos)

// snd989_param.cpp -- volume / pan / pitch setters, the auto-pan, the modulators, the stream chunk callbacks, fno 33.
void fn_33a04(IopContext&);   // shim_fno33(buff)
void fn_355a4(IopContext&);   // mod_Step(node)
void fn_35aa0(IopContext&);   // mod_Depth(node)
void fn_35c14(IopContext&);   // pan_Rotate(h, target, dir, speed, mode)
void fn_35f48(IopContext&);   // pan_RotateTick(node, rec)
void fn_393b0(IopContext&);   // seq_SetVolPan(h, vol, pan, no_children)
void fn_398cc(IopContext&);   // seq_UpdatePitch(rec)
void fn_39c98(IopContext&);   // seq_PanLaw(angle, vol)
void fn_39d2c(IopContext&);   // seq_VoicePriority(vol, def)
void fn_3fba0(IopContext&);   // snd_SetVolPan(h, vol, pan)
void fn_3fc34(IopContext&);   // snd_SetParams(h, mask, vol, pan, a, b)
void fn_42380(IopContext&);   // sndlist_Find(list, mask)
void fn_4234c(IopContext&);   // sndlist_Remove(list, mask)
void fn_42400(IopContext&);   // sndlist_Append(list, node)
void fn_42448(IopContext&);   // sndlist_Unlink(list, node)
void fn_423b8(IopContext&);   // sndlist_AllocNode()
void fn_47950(IopContext&);   // stream_ChunkCallback(st, arg, which)
void fn_4b394(IopContext&);   // stream_VolumeRequest(cur, req)
void fn_4b3d8(IopContext&);   // stream_PanRequest(cur, req)
void fn_4b61c(IopContext&);   // stream_SetVolPan(h, vol, pan)
void fn_4dcc8(IopContext&);   // voice_Pitch(note, fine, a2, a3)
void fn_4dd68(IopContext&);   // voice_PitchWords(def, mod, a2, a3, base, *note, *fine)
void fn_4e628(IopContext&);   // voice_VolumeLaw(v0, v1, pan, v2, pan_b, v3, pan_c, out, flags)

// snd989_voice.cpp -- voice allocation / key-on / the active list, the note-on event, the LFSR, the modulator list.
void fn_35310(IopContext&);   // mod_WaveRandom(node, phase)
void fn_353a8(IopContext&);   // mod_Start(node)
void fn_35440(IopContext&);   // mod_UnlistAll(rec)
void fn_35480(IopContext&);   // mod_Unlist(node)
void fn_35508(IopContext&);   // mod_List(node)
void fn_37a44(IopContext&);   // seq_NoteOn(rec, entry, *index)
void fn_39b44(IopContext&);   // seq_VoiceEnded(v, rec)
void fn_3f440(IopContext&);   // snd_Random()
void fn_4c458(IopContext&);   // voice_Mute(v, on)
void fn_4c750(IopContext&);   // voice_Alloc(grp, prio, keep)
void fn_4ca58(IopContext&);   // voice_KeyOn(v, noise)
void fn_4de04(IopContext&);   // voice_Activate(v)
void fn_4dec0(IopContext&);   // voice_Deactivate(v)

// snd989_refill.cpp -- the stream refill machinery.
void fn_351c8(IopContext&);   // dma_Done(*sema)
void fn_3aa3c(IopContext&);   // trans_Done(ch)
void fn_471a0(IopContext&);   // refill_Dispatch(offset)
void fn_47a88(IopContext&);   // refill_Memory(offset, size, blocks, flags)
void fn_49788(IopContext&);   // trans_Start(ch)
void fn_49c58(IopContext&);   // trans_Step(ch)
void fn_4a7c4(IopContext&);   // adpcm_FixFlags(buf, size, more, mark_end)
void fn_4a9d4(IopContext&);   // refill_Queue(hr)
void fn_4aa70(IopContext&);   // start_Queue(hr)

// snd989_seqev.cpp -- the sequence event handlers (rec, def, entry).
void fn_3821c(IopContext&);   // seqev_RandomRepeat
void fn_3832c(IopContext&);   // seqev_RandomDelay
void fn_38074(IopContext&);   // seqev_Nop0
void fn_3807c(IopContext&);   // seqev_Nop1
void fn_38084(IopContext&);   // seqev_EndScan
void fn_38378(IopContext&);   // seqev_RandomPitch
void fn_38578(IopContext&);   // seqev_RandomVolumeByte
void fn_38998(IopContext&);   // seqev_Repeat
void fn_37f4c(IopContext&);   // seqev_Modulator
void fn_397d4(IopContext&);   // seq_SetPitchOffset(h, cents)

// snd989_play.cpp -- the play request, the handle pool, the CD side, group volume.
void fn_33770(IopContext&);   // shim_Play(buff)
void fn_37000(IopContext&);   // seq_Start(bank, index, vol, pan, req)
void fn_3dcb8(IopContext&);   // loader_PushStatus(which, status)
void fn_3eda0(IopContext&);   // movie_GroupVolume(group)
void fn_3f820(IopContext&);   // snd_Play(req)
void fn_4111c(IopContext&);   // handle_Alloc(table, count, type, stride)
void fn_41334(IopContext&);   // seq_AllocHandle(def)
void fn_413a8(IopContext&);   // sndlist_Link(rec)
void fn_4144c(IopContext&);   // handle_Release(rec, force)
void fn_42d3c(IopContext&);   // timer_Insert(node)
void fn_44a58(IopContext&);   // stream_SetupRecord(h, st, source, a3, pair, flags)
void fn_464e0(IopContext&);   // snd_IsStarted(h)
void fn_46d8c(IopContext&);   // cd_Wait(mode)
void fn_46ed0(IopContext&);   // cd_IssueRead()
void fn_4a8a8(IopContext&);   // stream_AllocRecord(kind)
void fn_4b24c(IopContext&);   // stream_ChainRecord(h, st)
void fn_4e02c(IopContext&);   // voice_RequestKeyOff(core, bit)
void fn_4e100(IopContext&);   // group_SetVolume(group, vol)

// snd989_stream2.cpp -- the stream start, its records / handle, the stream key-on, the reader's CD request, the loader read.
void fn_3384c(IopContext&);   // shim_fno54(buff)
void fn_3b080(IopContext&);   // load_Read(buf, size)
void fn_3deec(IopContext&);   // heap_Alloc(size, kind, *out_size)
void fn_3e110(IopContext&);   // trans_TakeChannel()
void fn_42c84(IopContext&);   // timer_Unlink(node)
void fn_42dd0(IopContext&);   // timer_FindDue(node, tick)
void fn_43d00(IopContext&);   // stream_SetRecordRange(kind, first, last)
void fn_43d2c(IopContext&);   // stream_AllocRecords(*r0, *r1, two, with_voices, kind)
void fn_43f30(IopContext&);   // stream_Start(src0, src1, arg0, arg1, vol, pan, kind, h, chunk_id, flags)
void fn_46840(IopContext&);   // cd_RequestRead(sector, sectors, buf)
void fn_46e94(IopContext&);   // cd_LastError()
void fn_493b0(IopContext&);   // cd_ReadFailed()
void fn_4a050(IopContext&);   // stream_KeyOn(st, all)
void fn_4b164(IopContext&);   // stream_AllocHandle()

// snd989_control.cpp -- stop-all, the voice-mask key-off, the periodic timers, the loader seek, the ramp-done wait, resets, shims.
void fn_33250(IopContext&);   // snd_ExternalCommandArgs(name, op, arg0, arg1)
void fn_33310(IopContext&);   // shim_fno6(buff)
void fn_33514(IopContext&);   // shim_fno8(buff)
void fn_33560(IopContext&);   // shim_fno10(buff)
void fn_33804(IopContext&);   // shim_fno19(buff)
void fn_33828(IopContext&);   // shim_fno20(buff)
void fn_33e5c(IopContext&);   // shim_fno25(buff)
void fn_34044(IopContext&);   // shim_fno59(buff)
void fn_3408c(IopContext&);   // shim_fno60(buff)
void fn_340ac(IopContext&);   // shim_fno61(buff)
void fn_340cc(IopContext&);   // shim_fno62(buff)
void fn_34514(IopContext&);   // shim_fno34(buff)
void fn_3a8d0(IopContext&);   // tick_Rate()
void fn_3a964(IopContext&);   // ramp_DoneCallback()
void fn_3ad30(IopContext&);   // load_Seek(offset, whence)
void fn_3c5d8(IopContext&);   // bank_Load(a0, a1, size, kind)
void fn_3d0d8(IopContext&);   // ramp_DrainSema()
void fn_3d118(IopContext&);   // ramp_WaitDone(wait)
void fn_3e0c0(IopContext&);   // heap_Free(p)
void fn_3e678(IopContext&);   // movie_ClearBuffer()
void fn_41074(IopContext&);   // handle_ResetPool(table, count, type, stride)
void fn_410c0(IopContext&);   // seq_ResetPools()
void fn_41c9c(IopContext&);   // snd_StopAll()
void fn_426dc(IopContext&);   // timer_Reset()
void fn_4272c(IopContext&);   // timer_Step(ticks)
void fn_42924(IopContext&);   // timer_Add(when, period)
void fn_4d5f0(IopContext&);   // voice_KeyOffMasks(masks, notify)
void fn_4dfa4(IopContext&);   // voice_Deactivate2(v)
void fn_4dfc4(IopContext&);   // voice_RequestKeyOn(core, bit, fx)
void fn_4e0b0(IopContext&);   // voice_ResetSlots()
void fn_4e220(IopContext&);   // group_GetVolume(group)
void fn_4e23c(IopContext&);   // snd_SetPanMode(mode)

// snd989_misc.cpp -- handle validation, the timer / CD callbacks, group scaling, the owned-voice scan.
void fn_3a90c(IopContext&);   // tick_TimerBeat(arg)
void fn_3a9dc(IopContext&);   // stream_CdReadDone(status)
void fn_3fa7c(IopContext&);   // snd_HandleOrZero(h)
void fn_417f8(IopContext&);   // snd_HandleRecord(h)
void fn_42064(IopContext&);   // handle_NextVoice(h, start)
void fn_4ed08(IopContext&);   // voice_GroupScale(vol, group)

}  // namespace ps2r::title::sly2::snd989
