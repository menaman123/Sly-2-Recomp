# Sly 2: Band of Thieves -- how the title cel-shades, and what the engine owes it

Stage CEL, 2026-10-20. This file belongs at REPO/docs/CEL_SHADING.md (written in the stage worktree
$WORK/wt/cel/CEL_SHADING.md; move it with the merge). Every number below is read from the PCSX2 2.7.329
per-draw dumps (SaveInfo <draw>_context.txt / _vertex.txt) of the three fixture frames -- Paris ledge f15
(ep1_pcsx2/hwdump_tex), Cairo museum f1860 (sh_pcsx2/hwdump_intro), palace hub f15 (wp_pcsx2/hwdump_ep2) --
and from our NATIVEEE_GS_VERTS traces of the same scenes (states/ep1_ledge_3090 key 11, intro_museum_1471
key 6, hub_ep2 key 11). The gate that measures it is verify/judge_cel.py (fixture verify/cel/cel_fixture.json).

## 1. Mechanism in one paragraph

There is no post-process, no hull mesh and no ramp texture. The "cel" look is two things the title does
itself: (a) INK -- one GS LINE primitive per visible silhouette / crease edge of each character, emitted by
the character's VU1 program right after the character's own body passes and the stencil-shadow block,
as a draw PAIR per edge (a colour pass that writes RGB only, then a Z pass that writes Z only), 1 px wide,
at the body's own depth, Z-tested GREATER against the body it outlines; (b) SHADE -- every lit mesh is drawn
twice from the same PSMT8 / PSMT4 index texture with TWO palettes: a lerp pass (palette A, Gouraud vertex
colour = the ambient tint) and an additive pass (palette B, grey vertex colour whose ALPHA carries the
per-vertex light intensity). The shading is therefore continuous per vertex; the "bands" a viewer sees are
the hand-painted palettes (palette B is a few flat tones) plus the ink.

## 1a. The OUTLINE (ink) pass

Per character (Paris twin f15: 474 edges = 948 LINE draws; ours key 11: 499 pairs = 998 draws; Cairo f1860:
513 pairs; palace f15: 516 pairs), each edge is two draws over the SAME two vertices:

| | colour pass | Z pass |
|---|---|---|
| PRIM | 0xc1 = LINE (type 1), IIP 0, TME 0, FGE 0, ABE 1, AA1 1, FST 0, CTXT 0 | 0x01 = LINE, ABE 0, AA1 0 |
| TEST | ATE 1, ATST NEVER (0), AREF 0, AFAIL RGB_ONLY (3), DATE 0, ZTE 1, ZTST GREATER (3) | same but AFAIL ZB_ONLY (2) |
| ALPHA | A 0 B 1 C 0 D 1 (Cs - Cd) * As + Cd, FIX 0 -- As = 0x80 => Cs (replace) | (ABE off) |
| RGBAQ | 16 / 16 / 16 / 128, flat (IIP 0) | 16 / 16 / 16 / 128 |
| XYZ2 x 2 | the edge's two vertices after XYOFFSET (OFX 1792.0, OFY 1936.0 on the twin's field; ours 1792.0 / 1936.5 on the other field), Z = the body vertex's Z (Paris: 56295..78808) | identical |
| FRAME / ZBUF | FBP 0 or 0x70000 (the title alternates), FBW 512, PSMCT32, FBMSK 0; ZBUF PSMZ32, ZMSK 0 | same |
| FOGCOL / PABE / FBA / DTHE / COLCLAMP | 0 / 0 / 0 / 0 / 1 | same |

ATST NEVER + AFAIL RGB_ONLY is the GS idiom "write colour, never Z": every pixel fails the alpha test and
the fail path writes RGB only. The second draw (AFAIL ZB_ONLY) then writes the ink's Z so later ink does not
over-draw it. ZTST GREATER against the body's own depth is what hides the interior edges and lets the
silhouette through: the body pass wrote Z = the surface depth; the ink shares those vertices, so an edge
passes only where it is nearer than what is already there -- the silhouette over the background, and
creases that face the camera.

Frame-order position (Paris twin f15, 4304 draws; ours key 11, 4874): full-field clear sprite (ordinal 5,
RGBA 0/0/0/128, Z 0 -- the mid-frame clear the E330 fix keys on) -> world -> Sly's body passes (ordinals
2284..2444: 41 lerp + 41 add draws, TBP0 0x181a00, 0x292000, 0x298000, 0x2a0000, 0x2a5000, 0x2a7800) ->
more world -> the stencil-shadow counter block (ordinals 2878..2886: FBMSK 0xffff00ff / 0x7fffbfff /
0x3fff sprites and the DATE volumes, judge_shadow's territory) -> the INK block (ordinals 2887..3834, one
contiguous run of 948 LINE draws) -> remaining world / transparent objects (3835..4292) -> the post sprites
(4293..4303: a full-field (128,128,128,0) ALPHA 0101 lerp, alpha-only FBMSK 0xff000000 sprites) -> the
HUD's two coloured LINESTRIPs (0,25,102 and 25,63,89 -- the binocucom reticle, not ink). The ink is drawn
AFTER the shadow block: the shadow darkening never touches it, and the ink lands on top of the shadow.

Who carries ink: in all three fixture frames every ink line sits in ONE contiguous block whose x/y/z
bounding box is the player character's (Paris: x 171..304, y 116..211, z 56k..79k). Buildings, props and
the world carry NO ink (their draws are the textured lerp/add pairs only). Guards were not in view in these
frames; a guard's ink would appear as a second LINE block at its own depth (judge_cel reports "other
carriers" from ink lines whose Z lies outside char_z). The 20 % of the twin's ink that lies > 8 px from the
body MASK in Paris / the palace is Sly's own cane and tail (draws partly outside char_box are excluded from
the mask, not from the ink).

VU1 program / block ids: the engine's DrawIr carries an identity stamp (image_hash, kick_pc, kick_ord); the
verts trace now prints it (`img= kpc= kord= hle=`), but under the shipped AOT + GIF-drain pipeline the stamp
is 0 on every draw of the traced frame (SetIdentity is not called on that path). The pack holds 8 VU1
images (pack_int1/pack.txt); attributing the ink block to one of them needs the stamp wired on the AOT
kick -- listed under "what remains" in the stage report.

Vertex positions: ours vs the twin agree within 0.2 px on the same edges (commit 77e8a35); the two
emulators differ by the field (OFY 1936.0 vs 1936.5 => a 0.5 px y shift the judge's align_geometry absorbs).

## 1b. The TOON lighting

Not a ramp. Each lit mesh strip is drawn twice (ours key 11: 1710 lerp + 1672 add triangle draws in the
frame; Sly alone 41 + 41):

| | lerp pass (palette A) | add pass (palette B) |
|---|---|---|
| PRIM | TRIANGLE/STRIP, IIP 1, TME 1, FGE 1, ABE 1, FST 0 (STQ) | same |
| TEX0 | TBP0 = the index texture (PSMT8 0x13 / PSMT4 0x14, TW/TH 4..7), CBP = palette A (e.g. 0x298000 / CBP 0x28e100), CPSM PSMCT32, CSM 0, CSA 0, TCC 1, TFX 0 (MODULATE) | same TBP0, CBP = palette B (0x298000 / 0x28e500) |
| TEST | ATE 1, ATST GEQUAL (5), AREF 126, AFAIL FB_ONLY (1), ZTST GREATER, Z written | ATE 1, ATST NEVER (0), AFAIL FB_ONLY (1): colour only, no Z |
| ALPHA | 0101: (Cs - Cd) * As + Cd with vertex A = 0x80 => Cs replaces | 0201: Cs * As + Cd (additive) |
| RGBAQ | Gouraud colour = the ambient tint (Paris mean 52/70/98, palace 69/43/55), A 0x80 | grey R=G=B (Paris median 131, palace 160), A = light intensity 5..127 per vertex (Paris median 73, palace 80) |

The lit colour is therefore  A[idx] * tint  +  B[idx] * grey * intensity -- continuous in the vertex
intensity (Gouraud), quantised only by the palettes' painted tones. Our vertex streams match the twin's:
add-pass alpha quartiles Paris ours 48/70/87 vs twin 49/73/87, palace 36/78/115 vs 40/80/115; the R
medians per material agree within 3. Both palettes of every body texture decode byte-identical to the
twin's (judge_cel ramp identity: Paris 14/14 textures max|diff| 0, palace 12/12; Cairo's twin dump has no
texture files, skipped). What the judge measures as "bands" are the luminance modes inside each
material's mask; where they differ it is lighting / shadow context, not a ramp (see the palace note in
the report).

## 1c. Engine dependencies the ink and shade stand on

- LINE / POINT rasterisation (77e8a35, edca83b): the engine emitted every line draw and dropped it (min 3
  vertices in DrawPipeline::Prepare and both backends). PrimExpand turns LINE / LINESTRIP / POINT kick
  output into one-pixel quads -- +-0.5 px along the MINOR axis, one sample per major step = the GS DDA line
  (GS User's Manual 3.4.3); attributes copied from the owning endpoint; MinVerts(prim) = 1 / 2 / 3.
  PCSX2's HW renderer expands lines to the PERPENDICULAR 1-px band instead, so its diagonals are up to 2 px
  thick where the GS and ours draw 1 px (judge_cel's thickness gate allows 1 px / 20 %).
- Z ordering / Z precision: the ink is Z-tested GREATER against the body at the SAME vertices. Ours
  interpolates v_z noperspective and writes gl_FragDepth = z * 2^-32 into a GL_DEPTH_COMPONENT32F target
  (z < 2^17 here: exact), so the tie / nearer decision is made on the same numbers the GS uses; the visible
  fraction of the line raster matches the twin (Paris 0.688 vs 0.667, Cairo 0.98 vs 0.96, palace 0.63 vs
  0.68). A 24-bit depth buffer would quantise these z to 256-unit steps and reject whole creases.
- PRIM attribute flush (77e8a35, gs_registers SetPrim): the colour pass and the Z pass alternate ABE /
  AA1 on every edge with vertices pending; a same-type PRIM write that changes a draw-carried attribute
  now flushes first, so each pass carries its own bits in the IR.
- ATST NEVER + AFAIL RGB_ONLY / ZB_ONLY: the alpha-fail write masks must be honoured per pass (RGB without
  Z, Z without RGB); the body's add pass relies on the same idiom (colour without Z).
- VU1 clip / winding: the ink edges are emitted already clipped by the VU1 program (no vertex outside the
  scissor in any fixture); lines have no winding. Culling of the body passes must not cull the ink's
  owner, or the ink outlines a hole (ours: body draw counts 85 / 89 / 86 vs the twin's 85 / 82 / 81 in the
  char boxes).
- CLUT decode: both palettes (CBP A / B) of the PSMT8/4 textures byte-identical to the twin's (Oracle A);
  CSM 0, CSA 0 throughout; point-sampled (video.texture_filter=point; TEX1 MMAG/MMIN as the title sets).
- S/T/Q: FST 0 (STQ) on every body pass; the engine's noperspective STQ + per-pixel divide (the GS rule)
  keeps palette indices on the right texel so the painted tones stay flat.
- The E330 mid-frame clear (solidity stage): the frame-opening full-field sprite (ordinal 5) is the due
  clear the render target keys on; the ink lands long after it and is unaffected, but a mis-keyed clear
  would wipe the body's Z and let every interior edge through.
- Phase C (frame-cap interpolation): the ink pair must travel with its body pass -- see
  PHASE_C_CEL_RULE.md.
