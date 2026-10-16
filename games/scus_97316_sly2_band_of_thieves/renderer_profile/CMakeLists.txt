# games/scus_97316_sly2_band_of_thieves/renderer_profile -- the GS class-renderer profile.
#
# STATUS: no target (boot stage). Without a profile the GS register stream takes the generic
# software path; consumers gate on if(TARGET ps2r_title_renderer_profile).
message(STATUS "games/scus_97316_sly2_band_of_thieves: no renderer profile (draws take the generic path)")
