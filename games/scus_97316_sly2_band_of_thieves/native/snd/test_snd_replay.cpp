// test_snd_replay.cpp -- THE RPC-REPLAY ORACLE TEST of the native 989snd server (native/snd/README.md).
//
// Input: an RPC trace the hosted (OFF) lane recorded with NATIVEEE_RPC_TRACE (leg_iop.cpp's format: per round
// trip `<iop_frame> RPC< sid fno`, the libsd ops the module reached `<iop_frame> SD ord a0 a1 a2 a3 extra -> v0`,
// then `<iop_frame> sid fno ssize send_hex -> reply rsize recv_hex`), optionally the IOP RAM image dumped at the
// frames cap (NATIVEEE_RAM_DUMP=<f> writes <f>.iop: the hosted module's state words and the bank headers).
//
// The replay: every 0x123456 round trip of the trace is offered to Snd989Server::Call with
//   * an EE RAM that holds the recorded request bytes at the recorded... (the trace keeps the bytes, not the
//     addresses: the test places them at fixed addresses) and a reply slot;
//   * an IOP RAM that is the dump (when given) with the MOVIE STATE WORDS re-derived from the trace itself at
//     each point in time (the dump is the end state: the movie is closed by then): the open's reply = the IOP
//     buffer, the start's SetAddr SSA ops = the SPU base and the voices (core | v<<1 low byte), the open's
//     SetTransIntrHandler = the channel; the reply buffer pointer from the recorded reply address;
//   * a ReplayFace that answers each libsd op the body performs from the NEXT recorded SD row of that round
//     trip and records (ordinal, the args the op uses);
//   * a fallback that MODELS THE MODULE: it applies the recorded reply bytes to the shared reply buffer (what
//     the hosted module's shim + dispatcher did) -- so the stale-word semantics are exercised across hosted
//     and native round trips.
// Checks, per natively served round trip: the 12 reply bytes == the recorded recv bytes; the op sequence
// (ordinal + used args, in order) == the recorded SD rows of the round trip, and their count. Per forwarded
// round trip: the server's decision must be "forward" exactly when the fno is not native (a native-capable fno
// forwarded is a FAIL: the census must read N/0 for the native set).
// Without a trace argument the test runs a synthetic self-check of the reply-buffer model (CI, no game data).
//
// Usage: sly2_test_snd_replay [TRACE [--iop-ram IMAGE]]
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "games/scus_97316_sly2_band_of_thieves/native/snd/native_snd_server.h"

using namespace ps2r::title::sly2;

namespace {

struct SdRow { int ord; uint32_t a0, a1, a2, a3, extra, v0; };
struct RoundTrip {
    uint64_t frame = 0;
    uint32_t sid = 0, fno = 0, ssize = 0, rsize = 0, reply_addr = 0;
    std::vector<uint8_t> send, recv;   // first 64 bytes each (the trace's cap)
    std::vector<SdRow> ops;            // the libsd ops inside the round trip
    bool send_truncated = false;
};

std::vector<uint8_t> Hex(const std::string& s) {
    std::vector<uint8_t> v;
    if (s == "-") return v;
    for (size_t i = 0; i + 1 < s.size(); i += 2) v.push_back(static_cast<uint8_t>(std::strtoul(s.substr(i, 2).c_str(), nullptr, 16)));
    return v;
}

std::vector<RoundTrip> Parse(const char* path, std::vector<SdRow>& tick_ops) {
    std::vector<RoundTrip> out;
    std::FILE* f = std::fopen(path, "r");
    if (!f) { std::fprintf(stderr, "cannot open %s\n", path); std::exit(2); }
    char line[4096];
    bool in = false;
    RoundTrip cur;
    while (std::fgets(line, sizeof(line), f)) {
        if (line[0] == '#' || line[0] == 'N') continue;   // comments, NAT rows
        char tag[32] = {};
        unsigned long long frame = 0;
        if (std::sscanf(line, "%llu %31s", &frame, tag) != 2) continue;
        if (std::strcmp(tag, "RPC<") == 0) {
            cur = RoundTrip();
            cur.frame = frame;
            std::sscanf(line, "%llu RPC< %x %u", &frame, &cur.sid, &cur.fno);
            in = true;
            continue;
        }
        if (std::strcmp(tag, "SD") == 0) {
            SdRow r{};
            std::sscanf(line, "%llu SD %d %x %x %x %x %x -> %x", &frame, &r.ord, &r.a0, &r.a1, &r.a2, &r.a3, &r.extra, &r.v0);
            if (in) cur.ops.push_back(r); else tick_ops.push_back(r);
            continue;
        }
        if (std::strcmp(tag, "DMA>") == 0) continue;
        // the completion row: frame sid fno ssize send_hex -> reply rsize recv_hex
        char send_hex[256] = {}, recv_hex[256] = {};
        unsigned sid = 0, fno = 0, ssize = 0, rsize = 0, reply = 0;
        if (std::sscanf(line, "%llu %x %u %u %255s -> %x %u %255s", &frame, &sid, &fno, &ssize, send_hex, &reply, &rsize, recv_hex) == 8) {
            if (!in) { cur = RoundTrip(); cur.frame = frame; cur.sid = sid; cur.fno = fno; }
            cur.ssize = ssize; cur.rsize = rsize; cur.reply_addr = reply;
            cur.send = Hex(send_hex); cur.recv = Hex(recv_hex);
            cur.send_truncated = ssize > cur.send.size();
            out.push_back(cur);
            in = false;
        }
    }
    std::fclose(f);
    return out;
}

// The replay face: answers from the round trip's recorded ops, in order; records what the body asked.
class ReplayFace final : public SdFace {
public:
    std::deque<SdRow> script;
    std::vector<SdRow> asked;
    uint32_t Next(int ord, SdRow asked_row) {
        asked.push_back(asked_row);
        if (script.empty() || script.front().ord != ord) { mismatch++; return 0; }
        const uint32_t v0 = script.front().v0;
        script.pop_front();
        return v0;
    }
    uint32_t GetAddr(uint32_t entry) override { return Next(6, SdRow{6, entry, 0, 0, 0, 0, 0}); }
    uint32_t VoiceTrans(uint32_t chan, uint32_t mode, uint32_t iop, uint32_t spu, uint32_t size) override {
        return Next(9, SdRow{9, chan, mode, iop, spu, size, 0});
    }
    uint32_t VoiceTransStatus(uint32_t chan, uint32_t flag) override { return Next(11, SdRow{11, chan, flag, 0, 0, 0, 0}); }
    int mismatch = 0;
};

// The module's model as the fallback: the recorded reply lands in the shared reply buffer and the EE slot.
class ModelFallback final : public RpcRouter::Server {
public:
    std::vector<uint8_t>* iop = nullptr;
    std::vector<uint8_t>* ee = nullptr;
    const RoundTrip* rt = nullptr;
    void Call(const RpcRouter::CallRequest& req) override {
        if (!rt || rt->reply_addr == 0 || rt->recv.empty()) return;
        std::memcpy(iop->data() + rt->reply_addr, rt->recv.data(), rt->recv.size());
        std::memcpy(ee->data() + req.recv, rt->recv.data(), rt->recv.size());
        forwarded++;
    }
    int forwarded = 0;
};

constexpr uint32_t kEeSend = 0x1000, kEeRecv = 0x2000;

int SelfCheck() {
    // The reply-buffer model on a synthetic image: fno 10 (table read, stores) then fno 90 (void: the word stays).
    std::vector<uint8_t> iop(2u << 20, 0), ee(0x4000, 0);
    const uint32_t reply = 0xd1400;
    snd989::WriteWord(iop.data(), snd989::Image::kReplyBufPtr, reply);
    snd989::WriteWord(iop.data(), reply, 0xffffffffu);
    snd989::WriteWord(iop.data(), snd989::Image::kParamTable + 2 * 4, 0x400);
    snd989::WriteWord(iop.data(), snd989::Image::kMovieIopBuf, 0x8ae00);
    snd989::WriteWord(iop.data(), snd989::Image::kMovieSpuBase, 0x19040);
    iop[snd989::Image::kMovieChan] = 0; iop[snd989::Image::kMovieChan + 1] = 0;
    ReplayFace face;
    face.script.push_back(SdRow{9, 0, 0, 0x8ae00, 0x19040, 0x400, 0x400});
    face.script.push_back(SdRow{11, 0, 1, 0, 0, 0, 1});
    Snd989Server::Deps d;
    d.face = &face;
    d.ee_ram = [&](uint32_t a, uint32_t n) -> uint8_t* { return a + n <= ee.size() ? ee.data() + a : nullptr; };
    d.iop_ram = [&](uint32_t a, uint32_t n) -> uint8_t* { return a + n <= iop.size() ? iop.data() + a : nullptr; };
    Snd989Server s(snd989::kSidCommand, d);
    RpcRouter::CallRequest r;
    r.server_id = snd989::kSidCommand; r.func = 10; r.send = kEeSend; r.send_size = 4; r.recv = kEeRecv; r.recv_size = 12;
    snd989::WriteWord(ee.data(), kEeSend, 2);
    s.Call(r);
    if (snd989::ReadWord(ee.data(), kEeRecv) != 0xffffffffu || snd989::ReadWord(ee.data(), kEeRecv + 4) != 0x400 ||
        snd989::ReadWord(ee.data(), kEeRecv + 8) != 0xffffffffu) { std::puts("FAIL selfcheck fno10 reply"); return 1; }
    r.func = 90; r.send_size = 8;
    snd989::WriteWord(ee.data(), kEeSend, 0x400); snd989::WriteWord(ee.data(), kEeSend + 4, 0);
    std::memset(ee.data() + kEeRecv, 0, 12);
    s.Call(r);
    if (snd989::ReadWord(ee.data(), kEeRecv + 4) != 0x400) { std::puts("FAIL selfcheck fno90 stale word"); return 1; }
    if (face.asked.size() != 2 || face.mismatch) { std::puts("FAIL selfcheck fno90 ops"); return 1; }
    if (s.Served() != 2) { std::puts("FAIL selfcheck served count"); return 1; }
    std::puts("PASS selfcheck (reply-buffer model: stored word, stale word, 2 ops)");
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) return SelfCheck();
    const char* trace = argv[1];
    const char* image = nullptr;
    for (int i = 2; i + 1 < argc; i++) if (std::strcmp(argv[i], "--iop-ram") == 0) image = argv[i + 1];

    std::vector<SdRow> tick_ops;
    std::vector<RoundTrip> rts = Parse(trace, tick_ops);
    std::vector<uint8_t> iop(2u << 20, 0), ee(0x4000, 0);
    bool have_image = false;
    if (image) {
        if (std::FILE* f = std::fopen(image, "rb")) {
            have_image = std::fread(iop.data(), 1, iop.size(), f) == iop.size();
            std::fclose(f);
        }
        if (!have_image) std::fprintf(stderr, "warning: %s is not a 2 MiB IOP image; bank queries will be forwarded\n", image);
    }
    ReplayFace face;
    ModelFallback model;
    model.iop = &iop; model.ee = &ee;
    Snd989Server::Deps d;
    d.face = &face;
    d.fallback = &model;
    d.ee_ram = [&](uint32_t a, uint32_t n) -> uint8_t* { return a + n <= ee.size() ? ee.data() + a : nullptr; };
    d.iop_ram = [&](uint32_t a, uint32_t n) -> uint8_t* { return a + n <= iop.size() ? iop.data() + a : nullptr; };
    Snd989Server s(snd989::kSidCommand, d);

    // The movie state words, re-derived from the trace as the replay advances (the image is the END state).
    struct Movie { uint32_t iop_buf = 0, spu_base = 0; int32_t voice0 = -1, voice1 = -1; int16_t chan = 0; } movie;
    auto apply_movie = [&] {
        snd989::WriteWord(iop.data(), snd989::Image::kMovieIopBuf, movie.iop_buf);
        snd989::WriteWord(iop.data(), snd989::Image::kMovieSpuBase, movie.spu_base);
        snd989::WriteWord(iop.data(), snd989::Image::kMovieVoice0, static_cast<uint32_t>(movie.voice0));
        snd989::WriteWord(iop.data(), snd989::Image::kMovieVoice1, static_cast<uint32_t>(movie.voice1));
        iop[snd989::Image::kMovieChan] = static_cast<uint8_t>(movie.chan);
        iop[snd989::Image::kMovieChan + 1] = static_cast<uint8_t>(movie.chan >> 8);
    };

    uint64_t checked = 0, native_ok = 0, native_bad = 0, fwd = 0, fwd_native_capable = 0, skipped_bank = 0, skipped_trunc = 0, skipped_volatile = 0;
    std::map<uint32_t, uint64_t> native_by_fno;
    for (const RoundTrip& rt : rts) {
        if (rt.sid != snd989::kSidCommand) continue;
        if (rt.rsize != 12) continue;
        // The reply buffer pointer: the recorded reply address of every 0x123456 round trip (the module's one buffer).
        if (rt.reply_addr) snd989::WriteWord(iop.data(), snd989::Image::kReplyBufPtr, rt.reply_addr);
        // Learn the movie state from the hosted round trips BEFORE offering the next one.
        if (rt.fno == snd989::kMovieSoundOpen && !rt.recv.empty()) {
            movie.iop_buf = snd989::ReadWord(rt.recv.data(), 4);
            int seen = 0;
            for (const SdRow& op : rt.ops) {
                if (op.ord == 1 && ((op.a0 >> 8) & 0xff) == 0) {   // VOLL of the two movie voices, in allocation order
                    const int v = snd989::EntryVoice(op.a0);
                    if (seen == 0) movie.voice0 = v; else if (seen == 1) movie.voice1 = v;
                    seen++;
                }
                if (op.ord == 15) movie.chan = static_cast<int16_t>(op.a0);
            }
        }
        if (rt.fno == snd989::kMovieSoundStart) {
            for (const SdRow& op : rt.ops) if (op.ord == 5 && ((op.a0 >> 8) & 0xff) == 0x20) { movie.spu_base = op.a1; break; }
        }
        // The SPU base is allocated by the open (no libsd op shows it); the first push's recorded transfer
        // target minus its request offset is the only place the trace spells it before the start's SSA.
        if (rt.fno == snd989::kMovieSoundPush && movie.spu_base == 0 && rt.send.size() >= 8) {
            for (const SdRow& op : rt.ops) if (op.ord == 9) { movie.spu_base = op.a3 - snd989::ReadWord(rt.send.data(), 4); break; }
        }
        if (rt.fno == snd989::kMovieSoundClose) { movie = Movie(); movie.voice0 = movie.voice1 = -1; }
        apply_movie();
        if (rt.send_truncated) { skipped_trunc++; continue; }
        if (rt.fno == snd989::kBankSoundQuery && !have_image) { skipped_bank++; continue; }
        // fno 10 reads the group-volume table, which fno 9 records rewrite over the run: the END-state image
        // cannot stand in for it (verified live on the lane instead: the ON census's fno10 count + the row).
        if (rt.fno == snd989::kQuery10) { skipped_volatile++; continue; }

        // Offer the round trip.
        std::memset(ee.data() + kEeRecv, 0, 12);
        if (!rt.send.empty()) std::memcpy(ee.data() + kEeSend, rt.send.data(), rt.send.size());
        face.script.assign(rt.ops.begin(), rt.ops.end());
        face.asked.clear();
        face.mismatch = 0;
        model.rt = &rt;
        const int fwd_before = model.forwarded;
        RpcRouter::CallRequest r;
        r.server_id = rt.sid; r.func = rt.fno; r.send = kEeSend; r.send_size = rt.ssize; r.recv = kEeRecv; r.recv_size = 12;
        s.Call(r);
        checked++;
        const bool forwarded = model.forwarded != fwd_before;
        if (forwarded) {
            fwd++;
            if (Snd989Server::IsNativeFno(rt.fno)) {
                fwd_native_capable++;
                std::printf("FORWARDED native-capable fno %u at iop frame %llu\n", rt.fno, (unsigned long long)rt.frame);
            }
            continue;
        }
        // Natively served: reply bytes + op sequence.
        bool ok = std::memcmp(ee.data() + kEeRecv, rt.recv.data(), 12) == 0;
        if (face.asked.size() != rt.ops.size() || face.mismatch) ok = false;
        for (size_t i = 0; ok && i < rt.ops.size(); i++) {
            const SdRow& want = rt.ops[i];
            const SdRow& got = face.asked[i];
            if (want.ord != got.ord) { ok = false; break; }
            if (want.ord == 6 && want.a0 != got.a0) ok = false;
            if (want.ord == 9 && (want.a0 != got.a0 || want.a1 != got.a1 || want.a2 != got.a2 || want.a3 != got.a3 || want.extra != got.extra)) ok = false;
            if (want.ord == 11 && (want.a0 != got.a0 || want.a1 != got.a1)) ok = false;
        }
        if (ok) { native_ok++; native_by_fno[rt.fno]++; }
        else {
            native_bad++;
            if (native_bad <= 20) {
                std::printf("MISMATCH fno %u iop frame %llu: reply got", rt.fno, (unsigned long long)rt.frame);
                for (int i = 0; i < 12; i++) std::printf("%02x", ee[kEeRecv + i]);
                std::printf(" want");
                for (uint8_t b : rt.recv) std::printf("%02x", b);
                std::printf(" ops got %zu want %zu\n", face.asked.size(), rt.ops.size());
            }
        }
    }
    std::printf("replay: round trips %zu (0x123456 checked %llu) native ok %llu bad %llu forwarded %llu (native-capable forwarded %llu) "
                "skipped: bank-without-image %llu truncated-send %llu volatile-state(fno10) %llu; tick ops %zu\n",
                rts.size(), (unsigned long long)checked, (unsigned long long)native_ok, (unsigned long long)native_bad,
                (unsigned long long)fwd, (unsigned long long)fwd_native_capable, (unsigned long long)skipped_bank,
                (unsigned long long)skipped_trunc, (unsigned long long)skipped_volatile, tick_ops.size());
    std::printf("native by fno:");
    for (auto& kv : native_by_fno) std::printf(" %u=%llu", kv.first, (unsigned long long)kv.second);
    std::printf("\n");
    const bool pass = native_bad == 0 && fwd_native_capable == 0 && native_ok > 0;
    std::puts(pass ? "PASS" : "FAIL");
    return pass ? 0 : 1;
}
