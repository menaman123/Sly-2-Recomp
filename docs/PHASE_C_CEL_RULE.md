# Phase C (display-rate re-view / frame-cap interpolation) -- the cel rule

Stage CEL, 2026-10-20. Companion to CEL_SHADING.md. Belongs next to it at REPO/docs/.

## The rule

A character's INK (the LINE draw pairs of CEL_SHADING.md 1a) and its BODY passes (1b) share vertices:
every ink edge's two XYZ2 are two of the body strip's vertices, at the body's depth, Z-tested GREATER
against the body's own Z. Any presentation layer that moves draws between ticks -- FrameReview
(video.render_rate=display: the previous tick's frame re-viewed at the camera interpolated between the
two latest ticks; `engine/render/review/frame_review.h`), or any future frame-cap interpolator -- MUST
transform the ink pass and its body pass IDENTICALLY:

1. same Space class: if the body draws of a character are `Space::Screen` (re-projected through
   K = P^-1 . V_T^-1 . C(phase) . P), its ink LINE draws are `Space::Screen` too; never `Fixed`;
2. same camera phase: the body and the ink of one tick are re-issued from the SAME recorded frame T at
   the SAME phase (never body from T re-viewed and ink from T+1, never a cut on one and not the other);
3. same depth transform: the re-viewed Z of an ink vertex equals the re-viewed Z of the body vertex it
   came from (the ink's ZTST GREATER decision must be re-made against the re-viewed body Z, not the
   recorded one), and the STQ rescale by the depth ratio does not apply to the ink (TME 0).

Otherwise the outline DETACHES: the body moves with the camera phase and the ink stays (or vice versa),
a 1-px black contour floating up to the per-tick camera motion (several pixels at a pan) beside the
character on every intermediate picture -- the most visible artefact a 120 Hz mode can produce on this
title, since the eye reads the ink as the object's edge.

## Why the classifier is the risk

The title also draws LINESTRIPs that ARE screen-fixed: the binocucom / HUD reticle (PRIM type 2,
colours 0/25/102 and 25/63/89, ZTST ALWAYS, FBMSK 0xff000000 phases, last draws of the frame). A
classifier that keys "untextured line => HUD => Fixed" classes the ink wrongly. The ink is told apart
by its raster state, not its primitive type:

| | ink (Space::Screen, with the body) | HUD reticle (Space::Fixed) |
|---|---|---|
| PRIM type | 1 LINE (pairs over the same two vertices) | 2 LINESTRIP |
| TEST | ATST NEVER + AFAIL RGB_ONLY / ZB_ONLY, ZTE 1 ZTST GREATER | ZTST ALWAYS / no Z dependence |
| Z | the body's depth range (Paris 56k..79k) | 5k..6.5k constant |
| RGBAQ | 16/16/16/128 | coloured |
| position in frame | right after the stencil-shadow block, before the post sprites | after the post sprites |

Rule for the classifier: a LINE draw whose two vertices coincide (within 0.5 px and the same Z) with
vertices of a Space::Screen draw of the same tick inherits that draw's Space and camera; a LINE draw
with ZTE 1 and a Z inside the frame's scene depth range is Space::Screen; everything else is Fixed.
Key on content identity (handler / image / kick_pc / PRIM / TEST / FBP + geometry), never on ordinal
(BUG-009's lesson).

## The gate: judge_cel at 120 fps

When render_rate=display is un-parked (config.h: PARKED, Charter 3 item 6), add to the pipeline:

    PACK=... ./wt_run.sh <track> cel120 600 window -- --set video.render_rate=display \
        --set harness.display_hz=120 --dump-frames ... (NATIVEEE_DUMP_EVERY=1 from the Paris ledge state)

and, for every INTERMEDIATE picture (the re-viewed ones, odd presenter fields), run judge_cel in its
"attachment" form against the picture's OWN re-viewed draw list (FrameReview must write the re-projected
vertices of the frame it replays into the verts trace, tagged `review=1`, so the judge builds the body
mask and the ink raster of THAT picture):

- attachment: >= 0.98 of the ink-raster pixels lie within dilate(body mask, 1) of the same picture
  (the body mask from the re-viewed body passes, the ink raster from the re-viewed LINE pairs);
- no floating ink: near-black (L < 40) pixels within 6 px of the body mask but farther than 2 px from
  it, outside the ink raster, <= 2 % of the ink pixel count (the detached-contour signature);
- the standard judge_cel case on the TICK pictures (even fields) stays PASS -- the re-view must not
  change what the tick draws (gs_replay hash-identical for the tick pictures with interp on vs off).

Fail any of the three => the interpolator moved the ink and the body differently; the fix is in the
classifier / pairing, never in the ink.
