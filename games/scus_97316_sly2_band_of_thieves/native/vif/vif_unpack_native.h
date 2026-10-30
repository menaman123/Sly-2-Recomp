#pragma once
// VifUnpackNative: the typed UNPACK loops of Sly 2's VIF1 chains, writing straight
// into VU1 data memory -- the layout the VU1 programs (and the title's native VU1
// path) read: one 16-byte quadword of four 32-bit fields per vector, at
// (address + TOPS when the code's bit 15 is set) & 0x3FF, wrapping at 1024.
//
// PS2 rules restated from engine/vif/vif_unpack.cpp (the reference loop; the
// title's loops are byte-identical to it, proved by the title's unit test): S
// replicates one element to the four fields; V2 writes {x, y, x, y} (V2-32: W zero
// when the payload started on an odd word of its quadword); V3 takes W from the
// element that follows (the next vector's X, or the lookahead word -- nothing past
// the buffer; V3-32: zero when the vector's parity differs from the start parity);
// V4 copies; V4-5 expands RGB5A1 to {r<<3, g<<3, b<<3, a ? 0x80 : 0}; USN zero-
// extends, else sign-extend. STCYCL CL >= WL = the skipping write: of every CL
// destination quadwords only the first WL are written, the rest skipped (consuming
// no source); CL < WL (the filling write) is not taken here. STMOD adds ROW (1, 3)
// and/or the COL of the write cycle (2, 3). A masked UNPACK consults MASK per
// field and write cycle (rows 0-2, then row 3 repeats): 0 = the value, 1 = ROW,
// 2 = COL of the cycle, 3 = keep the existing memory.
#include <cstddef>
#include <cstdint>

struct VifRegs;

class VifUnpackNative {
public:
    // One UNPACK's static shape, decoded from the code word and the registers in force.
    struct Command {
        uint32_t format = 0;     // the format nibble (0x0 S-32 .. 0xF V4-5)
        bool masked = false;     // cmd bit 4
        bool usn = false;        // imm bit 14
        uint32_t addr_qw = 0;    // the destination quadword (TOPS already added)
        uint32_t n = 0;          // vectors to write (1..256)
        uint32_t cl = 1, wl = 1; // CYCLE in force (0 read as 1)
        uint32_t mode = 0;       // STMOD in force
        uint8_t phase = 0;       // parity of the payload's first word inside its quadword
        // The payload as the engine's executor sees it: the data words plus, for a V3
        // format (NeedsLookahead), the one lookahead word after them.
        const uint8_t* src = nullptr;
        size_t src_bytes = 0;
    };

    // A V3 format reads W from the element after each vector: the buffer must carry the lookahead word.
    static bool NeedsLookahead(uint32_t format);
    // True when a typed loop exists for this shape (every defined format, CL >= WL).
    // The caller falls back to the engine's VifUnpack otherwise.
    static bool Supports(const Command& c);
    // Runs the loop into `dmem` (the 16 KB VU1 data memory). `regs` supplies ROW/COL/MASK.
    static void Run(const Command& c, const VifRegs& regs, uint8_t* dmem);
};
