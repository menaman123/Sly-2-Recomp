# games/scus_97316_sly2_band_of_thieves/vu1_library -- the VU1 HLE program library.
#
# No target: consumers gate on if(TARGET ps2r_title_vu1_library). The .vutext section
# (0x259df0..0x262380) holds the title's microprograms. Without title.native.vu1 every microprogram runs
# on the engine's generic VU1 tiers; with it (native/vu1/) the generated native code of the known images
# runs through the engine's AOT registry -- a different seam from the HLE library this slot is for.
message(STATUS "games/scus_97316_sly2_band_of_thieves: no VU1 program library (every program interprets)")
