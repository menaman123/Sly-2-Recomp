// snd989_native.cpp -- the installer, the ported-function table, CallAddr, and the census row (snd989_native.h).
#include "games/scus_97316_sly2_band_of_thieves/native/iop/snd989/snd989_native.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "games/scus_97316_sly2_band_of_thieves/native/iop/snd989/snd989_bodies.h"

namespace ps2r::title::sly2::snd989 {

// Every ported function, in address order (the role names follow OpenGOAL's 989snd where it has one).
const PortedFn kPortedFns[] = {
    {0x33148, &fn_33148, "snd_FindExternalModule"},
    {0x3318c, &fn_3318c, "snd_ExternalCommand"},
    {0x33534, &fn_33534, "shim_fno9"},
    {0x338bc, &fn_338bc, "shim_fno26"},
    {0x34158, &fn_34158, "shim_fno90"},
    {0x34184, &fn_34184, "shim_fno91"},
    {0x344c4, &fn_344c4, "shim_fno76"},
    {0x34880, &fn_34880, "rpc_Batch"},
    {0x3497c, &fn_3497c, "rpc_Dispatch"},
    {0x35140, &fn_35140, "snd_ReportError"},
    {0x35200, &fn_35200, "snd_VolumeCurve"},
    {0x35a04, &fn_35a04, "endhandler_Poll"},
    {0x369c8, &fn_369c8, "master_StepRamps"},
    {0x377a4, &fn_377a4, "seq_Tick"},
    {0x3926c, &fn_3926c, "seq_Step"},
    {0x39b24, &fn_39b24, "bank_BlockSoundIsValid"},
    {0x3a90c, &fn_3a90c, "tick_TimerBeat"},
    {0x3a9dc, &fn_3a9dc, "stream_CdReadDone"},
    {0x3e1cc, &fn_3e1cc, "ramp_Done"},
    {0x3e5c0, &fn_3e5c0, "movie_Push"},
    {0x3e610, &fn_3e610, "movie_ReadPosition"},
    {0x3fa7c, &fn_3fa7c, "snd_HandleOrZero"},
    {0x3fac8, &fn_3fac8, "snd_BankSoundIsValid"},
    {0x40eb8, &fn_40eb8, "master_SetVolume"},
    {0x40f64, &fn_40f64, "master_Tick"},
    {0x417f8, &fn_417f8, "snd_HandleRecord"},
    {0x41da0, &fn_41da0, "snd_StopHandle"},
    {0x42064, &fn_42064, "handle_NextVoice"},
    {0x4218c, &fn_4218c, "sndlist_Tick"},
    {0x4226c, &fn_4226c, "timer_Walk"},
    {0x42f58, &fn_42f58, "snd_Lock"},
    {0x430dc, &fn_430dc, "snd_Unlock"},
    {0x431bc, &fn_431bc, "snd_TryLock"},
    {0x4325c, &fn_4325c, "snd_Tick"},
    {0x43638, &fn_43638, "snd_GetTick"},
    {0x4c1ac, &fn_4c1ac, "voice_Lock"},
    {0x4c2f8, &fn_4c2f8, "voice_Unlock"},
    {0x4cf5c, &fn_4cf5c, "voice_ApplyKeyOns"},
    {0x4d074, &fn_4d074, "voice_ApplyKeyOffs"},
    {0x4d184, &fn_4d184, "voice_Ended"},
    {0x4d318, &fn_4d318, "voice_KeyOff"},
    {0x4ed08, &fn_4ed08, "voice_GroupScale"},
    {0x4edbc, &fn_4edbc, "voice_RefreshGroupVolumes"},
    {0x4efb4, &fn_4efb4, "fade_Step"},
};
const uint32_t kPortedFnCount = sizeof(kPortedFns) / sizeof(kPortedFns[0]);

uint32_t CallAddr(IopContext& c, uint32_t target, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3) {
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

NativeOptions NativeOptions::FromEnv(const char* env_value) {
    NativeOptions o;
    if (!env_value) return o;
    std::string s(env_value);
    size_t pos = 0;
    while (pos <= s.size()) {
        size_t end = s.find(';', pos);
        if (end == std::string::npos) end = s.size();
        const std::string entry = s.substr(pos, end - pos);
        pos = end + 1;
        const size_t eq = entry.find('=');
        if (eq == std::string::npos || entry.substr(0, eq) != "snd989") continue;
        const std::string v = entry.substr(eq + 1);
        if (v == "off" || v == "0") {
            o.enabled = false;
        } else if (v == "on" || v == "1" || v.empty()) {
            o.enabled = true;
        } else {
            o.enabled = true;
            size_t p = 0;
            while (p <= v.size()) {
                size_t e = v.find(',', p);
                if (e == std::string::npos) e = v.size();
                const std::string a = v.substr(p, e - p);
                p = e + 1;
                if (!a.empty()) o.only.push_back(static_cast<uint32_t>(std::strtoul(a.c_str(), nullptr, 16)));
            }
        }
    }
    return o;
}

int InstallNative(const NativeOptions& opt) {
    int n = 0;
    for (uint32_t i = 0; i < kPortedFnCount; ++i) {
        const PortedFn& p = kPortedFns[i];
        const bool wanted = opt.enabled && (opt.only.empty() || std::find(opt.only.begin(), opt.only.end(), p.addr) != opt.only.end());
        if (!wanted) {
            iop_snd989::iop_seam_install(p.addr, nullptr);
            continue;
        }
        if (iop_snd989::iop_seam_install(p.addr, p.body)) ++n;
        else std::fprintf(stderr, "snd989 native: no recompiled function at 0x%x (%s)\n", p.addr, p.name);
    }
    return n;
}

void UninstallNative() {
    for (uint32_t i = 0; i < kPortedFnCount; ++i) iop_snd989::iop_seam_install(kPortedFns[i].addr, nullptr);
}

std::string NativeCensusRow() {
    uint64_t native = 0, recompiled = 0;
    uint32_t installed = 0;
    struct Hot { uint32_t addr; uint64_t calls; };
    std::vector<Hot> hot;
    for (uint32_t k = 0; k < iop_snd989::kFnSlotCount; ++k) {
        const IopFnSlot& s = iop_snd989::iop_fn_slots[k];
        native += s.native_calls;
        recompiled += s.recompiled_calls;
        if (s.native) ++installed;
        if (s.recompiled_calls) hot.push_back({s.addr, s.recompiled_calls});
    }
    std::sort(hot.begin(), hot.end(), [](const Hot& a, const Hot& b) { return a.calls != b.calls ? a.calls > b.calls : a.addr < b.addr; });
    // NATIVEEE_IOP_NATIVE_REPORT=<file>: every slot's counts (addr size native recompiled), the port's work list.
    if (const char* rep = std::getenv("NATIVEEE_IOP_NATIVE_REPORT")) {
        if (std::FILE* f = std::fopen(rep, "w")) {
            for (uint32_t k = 0; k < iop_snd989::kFnSlotCount; ++k) {
                const IopFnSlot& s = iop_snd989::iop_fn_slots[k];
                std::fprintf(f, "%05x %5u %10llu %10llu\n", s.addr, s.size / 4, static_cast<unsigned long long>(s.native_calls),
                             static_cast<unsigned long long>(s.recompiled_calls));
            }
            std::fclose(f);
        }
    }
    std::string row = "snd989:" + std::to_string(native) + "/" + std::to_string(recompiled) + ":fns=" + std::to_string(installed) + "/" +
                      std::to_string(iop_snd989::kFnSlotCount);
    if (!hot.empty()) {
        row += ":top=";
        char buf[48];
        for (size_t i = 0; i < hot.size() && i < 5; ++i) {
            std::snprintf(buf, sizeof(buf), "%s%xx%llu", i ? "," : "", hot[i].addr, static_cast<unsigned long long>(hot[i].calls));
            row += buf;
        }
    }
    return row;
}

}  // namespace ps2r::title::sly2::snd989
