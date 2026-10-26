# games/scus_97316_sly2_band_of_thieves/vu1_library -- the VU1 HLE program library.
#
# No target: every VU1 microprogram Sly 2 uploads runs on the engine's generic VU1 tiers;
# consumers gate on if(TARGET ps2r_title_vu1_library). The .vutext section (0x259df0..0x262380)
# holds the title's microprograms.
message(STATUS "games/scus_97316_sly2_band_of_thieves: no VU1 program library (every program interprets)")
