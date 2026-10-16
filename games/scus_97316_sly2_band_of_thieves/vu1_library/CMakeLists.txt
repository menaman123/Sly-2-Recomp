# games/scus_97316_sly2_band_of_thieves/vu1_library -- the VU1 HLE program library.
#
# STATUS: no target (boot stage). Every VU1 microprogram Sly 2 uploads is interpreted by the
# engine's generic VU1 path; consumers gate on if(TARGET ps2r_title_vu1_library). The .vutext
# section (0x259df0..0x262380) holds the title's microprograms; hashing them into a per-title
# registry is per-title HLE work for a later stage.
message(STATUS "games/scus_97316_sly2_band_of_thieves: no VU1 program library (every program interprets)")
