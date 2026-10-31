// snd989_native.cpp -- the ported-function table, its registration on the module (snd989_native.h), and Call.
#include "games/scus_97316_sly2_band_of_thieves/native/iop/snd989/snd989_native.h"

#include <cstdio>

#include "games/scus_97316_sly2_band_of_thieves/native/iop/snd989/snd989_bodies.h"

namespace ps2r::title::sly2::snd989 {

// Every ported function, in address order (the role names follow OpenGOAL's 989snd where it has one).
const PortedFn kPortedFns[] = {
    {0x33148, &fn_33148, "snd_FindExternalModule"},
    {0x3318c, &fn_3318c, "snd_ExternalCommand"},
    {0x33534, &fn_33534, "shim_fno9"},
    {0x338bc, &fn_338bc, "shim_fno26"},
    {0x33a04, &fn_33a04, "shim_fno33"},
    {0x34158, &fn_34158, "shim_fno90"},
    {0x34184, &fn_34184, "shim_fno91"},
    {0x344c4, &fn_344c4, "shim_fno76"},
    {0x34880, &fn_34880, "rpc_Batch"},
    {0x3497c, &fn_3497c, "rpc_Dispatch"},
    {0x35140, &fn_35140, "snd_ReportError"},
    {0x351c8, &fn_351c8, "dma_Done"},
    {0x35200, &fn_35200, "snd_VolumeCurve"},
    {0x35310, &fn_35310, "mod_WaveRandom"},
    {0x353a8, &fn_353a8, "mod_Start"},
    {0x35440, &fn_35440, "mod_UnlistAll"},
    {0x35480, &fn_35480, "mod_Unlist"},
    {0x35508, &fn_35508, "mod_List"},
    {0x355a4, &fn_355a4, "mod_Step"},
    {0x35a04, &fn_35a04, "endhandler_Poll"},
    {0x35aa0, &fn_35aa0, "mod_Depth"},
    {0x35c14, &fn_35c14, "pan_Rotate"},
    {0x35f48, &fn_35f48, "pan_RotateTick"},
    {0x369c8, &fn_369c8, "master_StepRamps"},
    {0x377a4, &fn_377a4, "seq_Tick"},
    {0x37a44, &fn_37a44, "seq_NoteOn"},
    {0x3926c, &fn_3926c, "seq_Step"},
    {0x393b0, &fn_393b0, "seq_SetVolPan"},
    {0x398cc, &fn_398cc, "seq_UpdatePitch"},
    {0x39b24, &fn_39b24, "bank_BlockSoundIsValid"},
    {0x39b44, &fn_39b44, "seq_VoiceEnded"},
    {0x39c98, &fn_39c98, "seq_PanLaw"},
    {0x39d2c, &fn_39d2c, "seq_VoicePriority"},
    {0x3a90c, &fn_3a90c, "tick_TimerBeat"},
    {0x3a9dc, &fn_3a9dc, "stream_CdReadDone"},
    {0x3aa3c, &fn_3aa3c, "trans_Done"},
    {0x3e1cc, &fn_3e1cc, "ramp_Done"},
    {0x3e5c0, &fn_3e5c0, "movie_Push"},
    {0x3e610, &fn_3e610, "movie_ReadPosition"},
    {0x3f440, &fn_3f440, "snd_Random"},
    {0x3fa7c, &fn_3fa7c, "snd_HandleOrZero"},
    {0x3fac8, &fn_3fac8, "snd_BankSoundIsValid"},
    {0x3fba0, &fn_3fba0, "snd_SetVolPan"},
    {0x3fc34, &fn_3fc34, "snd_SetParams"},
    {0x40eb8, &fn_40eb8, "master_SetVolume"},
    {0x40f64, &fn_40f64, "master_Tick"},
    {0x417f8, &fn_417f8, "snd_HandleRecord"},
    {0x41da0, &fn_41da0, "snd_StopHandle"},
    {0x42064, &fn_42064, "handle_NextVoice"},
    {0x4218c, &fn_4218c, "sndlist_Tick"},
    {0x4226c, &fn_4226c, "timer_Walk"},
    {0x4234c, &fn_4234c, "sndlist_Remove"},
    {0x42380, &fn_42380, "sndlist_Find"},
    {0x423b8, &fn_423b8, "sndlist_AllocNode"},
    {0x42400, &fn_42400, "sndlist_Append"},
    {0x42448, &fn_42448, "sndlist_Unlink"},
    {0x42f58, &fn_42f58, "snd_Lock"},
    {0x430dc, &fn_430dc, "snd_Unlock"},
    {0x431bc, &fn_431bc, "snd_TryLock"},
    {0x4325c, &fn_4325c, "snd_Tick"},
    {0x43638, &fn_43638, "snd_GetTick"},
    {0x44768, &fn_44768, "stream_NextChunk"},
    {0x45904, &fn_45904, "stream_Tick"},
    {0x45ccc, &fn_45ccc, "stream_PollVoice"},
    {0x461e8, &fn_461e8, "stream_AccountPlayed"},
    {0x471a0, &fn_471a0, "refill_Dispatch"},
    {0x47950, &fn_47950, "stream_ChunkCallback"},
    {0x47a88, &fn_47a88, "refill_Memory"},
    {0x49788, &fn_49788, "trans_Start"},
    {0x49c58, &fn_49c58, "trans_Step"},
    {0x4a7c4, &fn_4a7c4, "adpcm_FixFlags"},
    {0x4a9d4, &fn_4a9d4, "refill_Queue"},
    {0x4aa70, &fn_4aa70, "start_Queue"},
    {0x4b394, &fn_4b394, "stream_VolumeRequest"},
    {0x4b3d8, &fn_4b3d8, "stream_PanRequest"},
    {0x4b61c, &fn_4b61c, "stream_SetVolPan"},
    {0x4c1ac, &fn_4c1ac, "voice_Lock"},
    {0x4c2f8, &fn_4c2f8, "voice_Unlock"},
    {0x4c458, &fn_4c458, "voice_Mute"},
    {0x4c750, &fn_4c750, "voice_Alloc"},
    {0x4ca58, &fn_4ca58, "voice_KeyOn"},
    {0x4cf5c, &fn_4cf5c, "voice_ApplyKeyOns"},
    {0x4d074, &fn_4d074, "voice_ApplyKeyOffs"},
    {0x4d184, &fn_4d184, "voice_Ended"},
    {0x4d318, &fn_4d318, "voice_KeyOff"},
    {0x4dcc8, &fn_4dcc8, "voice_Pitch"},
    {0x4dd68, &fn_4dd68, "voice_PitchWords"},
    {0x4de04, &fn_4de04, "voice_Activate"},
    {0x4dec0, &fn_4dec0, "voice_Deactivate"},
    {0x4e628, &fn_4e628, "voice_VolumeLaw"},
    {0x4ed08, &fn_4ed08, "voice_GroupScale"},
    {0x4edbc, &fn_4edbc, "voice_RefreshGroupVolumes"},
    {0x4efb4, &fn_4efb4, "fade_Step"},
};
const uint32_t kPortedFnCount = sizeof(kPortedFns) / sizeof(kPortedFns[0]);

uint32_t Call(IopContext& c, uint32_t target, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3) {
    c.gpr[4] = a0;
    c.gpr[5] = a1;
    c.gpr[6] = a2;
    c.gpr[7] = a3;
    c.FixZero();
    if (const auto* fn = ps2r::iop::IopAbiSurface::Resolve(target)) {
        (*fn)(c);
        c.FixZero();
    } else {
        std::printf("[IOP-RECOMP] Unresolved JALR to 0x%08X (native snd989 body)\n", target);
    }
    return c.gpr[2];
}

}  // namespace ps2r::title::sly2::snd989

namespace ps2r::title::sly2 {

void RegisterSnd989Natives(NativeIopModule& m) {
    for (uint32_t i = 0; i < snd989::kPortedFnCount; ++i) {
        const snd989::PortedFn& p = snd989::kPortedFns[i];
        // A function another registration already covers (the frame's Snd989Lock: fn_42f58 / fn_430dc) keeps that body.
        bool taken = false;
        for (const NativeIopModule::Candidate& cnd : m.Candidates())
            if (cnd.addr == p.addr) taken = true;
        if (!taken) m.Add(p.addr, p.body, p.name);
    }
}

}  // namespace ps2r::title::sly2
