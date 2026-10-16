// Sly 2: Band of Thieves (NTSC-U, SCUS_973.16) -- the TitleProfile the engine consumes as data.
//
// Every value below was read off the retail disc / ELF during the recon stage (see the
// repository's README and $WORK/recon_disc_notes.md for the evidence per field). Fields the
// disc does not establish are left at their defaults and marked UNKNOWN with what would
// establish them. Nothing here is per-title HLE: this is identity + ELF facts + boot env.
#include "engine/title/title_profile.h"

const TitleProfile& ActiveTitleProfile() {
    static const TitleProfile p = [] {
        TitleProfile t;
        // --- identity ---------------------------------------------------------------------
        t.serial = "SCUS_973.16";          // SYSTEM.CNF: BOOT2 = cdrom0:\SCUS_973.16;1, VER = 1.00, VMODE = NTSC
        t.crc = 0xD67D6E1Eu;               // zlib crc32 of the whole 2,597,048-byte ELF (PCSX2 GameDB key)
        t.elf_path = "SCUS_973.16";        // volume root
        t.elf_dir = "Sly2.rep";            // game_files/ subdirectory holding the user's copy
        t.elf_sha256 = "a511a94b1dbe8cd9bc9b764c34ffda3f88869713a338e188b4f407732fec6a73";

        // --- ELF facts (readelf over the retail ELF) -----------------------------------------
        // Two PT_LOADs: #0 vaddr 0x00100000 filesz 0x1dd230 memsz 0x352200 (RWX: .text 0x100000..
        // 0x259df0, .vutext, .rodata, .data, .sbss/.bss to 0x452200); #1 vaddr 0x00472200 size
        // 0x73c40 = the .irx section (nine embedded IOP module images; ElfLoader copies it too).
        // The 128 KiB gap 0x452200..0x472200 is the .stack section (NOBITS, outside both PT_LOADs).
        t.entry = 0x00100008u;
        t.load_base = 0x00100000u;         // PT_LOAD #0 p_vaddr
        t.load_end = 0x00452200u;          // PT_LOAD #0 memsz end (exclusive) -- the code+data image
        t.file_end = 0x002dd230u;          // PT_LOAD #0 filesz end (exclusive)
        // tid 1's $sp at the ELF entry. crt0 never reads $sp before InitMainThread (0x1001c4) and
        // then takes the kernel's answer, (0x452200 + 0x20000) & ~0xF = 0x00472200 (its own .stack
        // section). So this only has to be a valid, unused RAM address: the engine's convention
        // (top of the 32 MiB image minus the kernel reserve) is kept.
        t.main_stack_top = 0x01FFFF00u;
        t.sdk_version = "3.0.0";           // every PsII<lib> stamp in the ELF reads 3000 (libkernl, libgraph,
                                           // libdma, libcdvd, libpad, libmc, libscf, libmpeg, libipu; IOPRP300)

        // --- boot environment (crt0 at 0x100008; disassembled in the recon stage) ---------------
        t.boot.argv = {"cdrom0:\\SCUS_973.16;1"};
        t.boot.argv_block = 0x002dd380u;   // the `args` pointer crt0 hands InitMainThread (= .bss start;
                                           // crt0 zeroes .sbss/.bss first, then the kernel writes argc/argv there)
        t.boot.sbrk_break_word = 0;        // no libc sbrk in this ELF (no caller of the EndOfHeap stub 0x234a00)
        t.boot.heap_limit_cap = 0;         // crt0 discards InitHeap's return value
        t.boot.default_heap_base = 0;      // InitHeap is called with an explicit base (_end = 0x472200), never -1

        // --- files this module ships / expects, relative to the title directory / game_files -----
        t.sdk_bindings_file = "hle/sdk_bindings.tsv";
        t.stream_irx_file = "";            // no disc-side IRX: the IOP modules are inside the ELF (.irx section)
        t.disc_image_file = "Sly2.rep/Sly 2 - Band of Thieves.iso";   // first-run copy target (Config.paths.iso overrides)

        // --- GameDB quirks: UNKNOWN until PCSX2's GameIndex.yaml entry for SCUS-97316 is read.
        //     Left at "no quirk". boot_t0_mode / budget: UNKNOWN until the title's timer init is traced.
        t.quirks.xgkick_defer = false;
        t.quirks.round_sprite_mode = 0;

        // --- savestate schema / recompiler settings -------------------------------------------
        t.save.schema_version = 0;         // no anchors yet
        t.recomp.shard_count = 32;         // recomp/CMakeLists.txt: --shards 32 --init-parts 32

        // --- SDK statics in THIS ELF (libpad / libgraph / libmc): UNKNOWN. The libpad port table,
        //     sceGsGParam block and libmc client are found once the sce-library rows are identified
        //     (hle/sdk_bindings.tsv); the binders accept 0 and these rows are not installed yet.

        // --- the frame boundary site. This ELF links no sceGsSyncV: the game's vsync wait is
        //     0x0016f560 (0x16f560..0x16f58c): [0x2dfb58] = 1 (wait flag); WaitSema([0x2dfb4c]). The
        //     sema is created by the display init 0x1700d0, which also AddIntcHandler(2 = VBLANK_START,
        //     0x16f468) + EnableIntc(2); that handler clears the flag and iSignalSema's the sema
        //     (0x16f468..0x16f488) before the display flip. Callers: 0x16f590 (the frame loop) and
        //     0x16f788 (first-frame sync from 0x1700d0). The composition root installs the engine's
        //     WaitForVblank mirror there; without it the boot stage's first run parked as
        //     park=watchdog, watchdog_ra=0x16f580 (only a boundary could ever signal the sema).
        t.gs.vblank_wait_entry = 0x0016f560u;
        return t;
    }();
    return p;
}
