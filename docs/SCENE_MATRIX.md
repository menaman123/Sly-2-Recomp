# Scene matrix -- nativeee md5 cd995b09, pack pack_int3, tag r4b, 2026-10-22

Cells: 32 -- GREEN 24, RED 8, DEFECT 0. Bar per cell: pictures/s p50 >= 59 AND doubled frames (shown gaps > 20 ms) <= 1 % (skip 5 s). p50 = judge_pictures output lane; dbl = judge_pacing shown lane; Hz = run.meta clock frame_hz; `~load N` = the host's 1-min load at the cell's start was above 12; `~games N` = N other game / replay processes were running at its start (shared host: both are noise, re-run).

| scene | auto | gl | note |
|---|---|---|---|
| hub_ep0 | GREEN p50 60 / dbl 0.0 % / 60.0 Hz | GREEN p50 60 / dbl 0.3 % / 60.0 Hz (4 takes: p50 47/59/60/60) | ep0 intro: Cairo museum hall (HUD) |
| hub_ep1 | GREEN p50 60 / dbl 0.0 % / 60.0 Hz | GREEN p50 60 / dbl 0.0 % / 60.0 Hz (2 takes: p50 60/60) | ep1 nightclub: Paris hub, the reference scene (hub_ep1 is the stale-colour capture) |
| hub_ep2 | GREEN p50 60 / dbl 0.0 % / 60.0 Hz (3 takes: p50 60/60/60) | **RED** p50 60 / dbl 14.7 % / 59.5 Hz (4 takes: p50 60/60/60/60) [2f+ 4.9 %: jitter] | ep2 palace: jungle ledge (old-build capture: timing valid, colours stale) |
| hub_ep3 | GREEN p50 60 / dbl 0.0 % / 60.0 Hz (4 takes: p50 30/34/59/60) | GREEN p50 60 / dbl 0.0 % / 60.0 Hz (4 takes: p50 36/43/44/60) | ep3 temple: terraces |
| hub_ep4 | GREEN p50 60 / dbl 0.3 % / 60.0 Hz (5 takes: p50 47/47/52/58/60) | **RED** p50 56 / dbl 10.5 % / 59.9 Hz (4 takes: p50 42/43/45/56) [2f+ 0.4 %: jitter] | ep4 prison: the safehouse interior -- close-up skinned Bentley at the ThiefNet laptop, beam-dense room (a 3-D scene, not a 2-D menu) |
| hub_ep5 | GREEN p50 60 / dbl 0.0 % / 60.0 Hz ~games 1 | GREEN p50 60 / dbl 0.3 % / 59.7 Hz (3 takes: p50 60/60/60) | ep5 castle: roof |
| hub_ep6 | GREEN p50 60 / dbl 0.2 % / 60.0 Hz | GREEN p50 60 / dbl 0.0 % / 60.0 Hz | ep6 train: ground |
| hub_ep7 | GREEN p50 60 / dbl 0.2 % / 60.0 Hz | GREEN p50 60 / dbl 0.0 % / 60.0 Hz (3 takes: p50 36/41/60) | ep7 sawmill: binocucom view |
| hub_ep8 | GREEN p50 60 / dbl 0.0 % / 60.0 Hz | GREEN p50 60 / dbl 0.0 % / 60.0 Hz | ep8 blimp: deck |
| museum | GREEN p50 60 / dbl 0.0 % / 60.0 Hz | GREEN p50 60 / dbl 0.2 % / 60.0 Hz ~games 1 | Cairo museum (the intro level, frame 1471) |
| paris_spawn | **RED** p50 60 / dbl 17.1 % / 60.0 Hz (4 takes: p50 45/50/50/60) [2f+ 0.0 %: jitter] | **RED** p50 54 / dbl 16.4 % / 60.0 Hz (4 takes: p50 28/29/42/54) [2f+ 2.2 %: jitter] | the dense Paris spawn view (4913 draws), no input |
| ledge | GREEN p50 60 / dbl 0.0 % / 60.0 Hz (2 takes: p50 48/60) | GREEN p50 60 / dbl 0.9 % / 60.0 Hz ~games 1 (2 takes: p50 44/60) | Paris ledge (frame 3090) |
| job1_started | GREEN p50 60 / dbl 0.2 % / 59.9 Hz ~games 2 (2 takes: p50 55/60) | GREEN p50 60 / dbl 0.2 % / 60.0 Hz (3 takes: p50 50/60/60) | mission: Satellite Sabotage, on the plaza after the briefing |
| job1_dish1 | GREEN p50 60 / dbl 0.0 % / 60.0 Hz | **RED** p50 60 / dbl 3.9 % / 60.0 Hz (4 takes: p50 59/60/60/60) [2f+ 0.0 %: jitter] | mission: at dish 1 (the water tower) |
| briefing | GREEN p50 60 / dbl 0.0 % / 60.0 Hz (2 takes: p50 60/60) | **RED** p50 54 / dbl 19.4 % / 60.0 Hz (4 takes: p50 30/32/33/54) [2f+ 0.0 %: jitter] | cutscene: the Satellite Sabotage binocucom briefing (runs 0..~2200 on this state) |
| cold_boot | **RED** p50 60 / dbl 5.9 % / 55.7 Hz (4 takes: p50 60/60/60/60) | **RED** p50 60 / dbl 6.3 % / 55.8 Hz (4 takes: p50 56/59/60/60) | cold boot: Sly logo -> intro FMV -> attract / title, no input |

## Detail (every cell)

| scene | backend | verdict | pictures/s p50 (p10) | doubled | shown p99 / max ms | >40 ms | frame_hz | logic_hz | draws/frame | worker ms | drain ms | consumer ms | floor / presenter | 2f+ fields | output gap p90 | frames (bound.) | wall s | park | load start -> end | games | out |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| hub_ep0 | auto | GREEN | 60 (60) | 0/596 (0.0 %) | 18.2 / 19.0 | 0 | 60.0 | 60.00 | 2965 | 6.33 | 3.92 | 4.17 | 1 / -1 | 0 (0.0 %) | 18.0 | 900 (900) | 20 | none | 10 -> 8 | 0 | pipe_r4b_hub_ep0_auto |
| hub_ep0 | gl | GREEN | 60 (59) | 2/590 (0.3 %) | 18.4 / 21.1 | 0 | 60.0 | 60.00 | 2965 | 7.76 | 4.09 | 13.88 | 2 / -2 | 0 (0.0 %) | 18.3 | 900 (900) | 20 | none | 6 -> 6 | 0 | pipe_r4b_hub_ep0_gl |
| hub_ep1 | auto | GREEN | 60 (60) | 0/593 (0.0 %) | 18.9 / 19.1 | 0 | 60.0 | 60.00 | 3061 | 7.57 | 4.39 | 5.11 | 1 / -1 | 0 (0.0 %) | 17.7 | 900 (900) | 20 | none | 9 -> 9 | 0 | pipe_r4b_hub_ep1_auto |
| hub_ep1 | gl | GREEN | 60 (60) | 0/594 (0.0 %) | 17.2 / 17.4 | 0 | 60.0 | 60.00 | 3061 | 6.44 | 3.52 | 8.97 | 1 / -1 | 0 (0.0 %) | 17.8 | 900 (900) | 21 | none | 9 -> 7 | 0 | pipe_r4b_hub_ep1_gl |
| hub_ep2 | auto | GREEN | 60 (60) | 0/590 (0.0 %) | 17.4 / 17.8 | 0 | 60.0 | 60.00 | 2738 | 7.76 | 4.50 | 5.55 | 1 / -1 | 0 (0.0 %) | 18.2 | 900 (900) | 20 | none | 6 -> 6 | 0 | pipe_r4b_hub_ep2_auto |
| hub_ep2 | gl | RED | 60 (48) | 82/556 (14.7 %) | 29.3 / 35.2 | 0 | 59.5 | 60.00 | 2739 | 8.55 | 4.55 | 11.02 | 38 / -11 | 27 (4.9 %) | 21.7 | 900 (900) | 20 | none | 6 -> 12 | 0 | pipe_r4b_hub_ep2_gl_first3 |
| hub_ep3 | auto | GREEN | 60 (60) | 0/594 (0.0 %) | 18.2 / 18.4 | 0 | 60.0 | 60.00 | 4111 | 8.67 | 5.26 | 5.43 | 1 / -1 | 0 (0.0 %) | 21.2 | 900 (900) | 20 | none | 6 -> 5 | 0 | pipe_r4b_hub_ep3_auto |
| hub_ep3 | gl | GREEN | 60 (59) | 0/589 (0.0 %) | 18.9 / 19.5 | 0 | 60.0 | 60.00 | 4111 | 8.77 | 4.35 | 11.02 | 0 / +0 | 0 (0.0 %) | 18.4 | 900 (900) | 21 | none | 6 -> 5 | 0 | pipe_r4b_hub_ep3_gl |
| hub_ep4 | auto | GREEN | 60 (60) | 2/589 (0.3 %) | 18.9 / 22.6 | 0 | 60.0 | 60.00 | 4185 | 11.03 | 6.69 | 7.34 | 1 / -1 | 0 (0.0 %) | 22.3 | 900 (900) | 21 | none | 5 -> 6 | 0 | pipe_r4b_hub_ep4_auto |
| hub_ep4 | gl | RED | 56 (54) | 58/555 (10.5 %) | 22.8 / 28.7 | 0 | 59.9 | 60.00 | 4182 | 12.93 | 5.87 | 18.11 | 40 / -38 | 2 (0.4 %) | 18.9 | 900 (900) | 21 | none | 5 -> 7 | 0 | pipe_r4b_hub_ep4_gl |
| hub_ep5 | auto | GREEN | 60 (60) | 0/591 (0.0 %) | 17.5 / 17.6 | 0 | 60.0 | 60.00 | 2597 | 6.81 | 3.80 | 4.95 | 0 / +0 | 0 (0.0 %) | 18.6 | 900 (900) | 21 | none | 11 -> 9 | 1 | pipe_r4b_hub_ep5_auto |
| hub_ep5 | gl | GREEN | 60 (60) | 2/592 (0.3 %) | 17.5 / 73.2 | 1 | 59.7 | 60.00 | 2597 | 5.12 | 2.46 | 7.80 | 3 / +0 | 3 (0.5 %) | 18.3 | 900 (900) | 20 | none | 12 -> 9 | 0 | pipe_r4b_hub_ep5_gl |
| hub_ep6 | auto | GREEN | 60 (59) | 1/593 (0.2 %) | 17.6 / 23.7 | 0 | 60.0 | 60.00 | 2630 | 5.82 | 3.44 | 4.69 | 0 / +0 | 0 (0.0 %) | 19.3 | 900 (900) | 20 | none | 7 -> 6 | 0 | pipe_r4b_hub_ep6_auto |
| hub_ep6 | gl | GREEN | 60 (60) | 0/592 (0.0 %) | 17.1 / 17.4 | 0 | 60.0 | 60.00 | 2630 | 4.95 | 2.46 | 7.71 | 0 / +0 | 0 (0.0 %) | 17.9 | 900 (900) | 20 | none | 6 -> 6 | 0 | pipe_r4b_hub_ep6_gl |
| hub_ep7 | auto | GREEN | 60 (60) | 1/594 (0.2 %) | 17.6 / 21.9 | 0 | 60.0 | 60.00 | 3869 | 12.66 | 6.59 | 11.16 | 0 / +0 | 0 (0.0 %) | 18.2 | 900 (900) | 21 | none | 6 -> 8 | 0 | pipe_r4b_hub_ep7_auto |
| hub_ep7 | gl | GREEN | 60 (59) | 0/594 (0.0 %) | 17.2 / 17.3 | 0 | 60.0 | 60.00 | 3870 | 7.33 | 3.41 | 16.70 | 0 / +0 | 0 (0.0 %) | 17.3 | 900 (900) | 20 | none | 8 -> 7 | 0 | pipe_r4b_hub_ep7_gl |
| hub_ep8 | auto | GREEN | 60 (60) | 0/596 (0.0 %) | 17.2 / 17.4 | 0 | 60.0 | 60.00 | 3344 | 5.87 | 3.35 | 4.23 | 1 / -1 | 0 (0.0 %) | 18.2 | 900 (900) | 20 | none | 7 -> 7 | 0 | pipe_r4b_hub_ep8_auto |
| hub_ep8 | gl | GREEN | 60 (60) | 0/593 (0.0 %) | 18.7 / 19.5 | 0 | 60.0 | 60.00 | 3344 | 5.71 | 2.78 | 8.56 | 1 / -1 | 0 (0.0 %) | 17.9 | 900 (900) | 20 | none | 7 -> 6 | 0 | pipe_r4b_hub_ep8_gl |
| museum | auto | GREEN | 60 (60) | 0/596 (0.0 %) | 19.2 / 19.7 | 0 | 60.0 | 60.00 | 2147 | 4.82 | 3.11 | 3.43 | 1 / -1 | 0 (0.0 %) | 17.9 | 900 (900) | 21 | none | 6 -> 6 | 0 | pipe_r4b_museum_auto |
| museum | gl | GREEN | 60 (60) | 1/593 (0.2 %) | 19.0 / 20.6 | 0 | 60.0 | 60.00 | 2147 | 5.04 | 3.07 | 7.89 | 1 / -1 | 0 (0.0 %) | 18.0 | 900 (900) | 21 | none | 6 -> 5 | 1 | pipe_r4b_museum_gl |
| paris_spawn | auto | RED | 60 (60) | 101/592 (17.1 %) | 23.7 / 24.9 | 0 | 60.0 | 60.00 | 4852 | 9.44 | 5.57 | 7.76 | 0 / +0 | 0 (0.0 %) | 21.3 | 900 (900) | 21 | none | 6 -> 6 | 0 | pipe_r4b_paris_spawn_auto_first3 |
| paris_spawn | gl | RED | 54 (53) | 88/535 (16.4 %) | 25.6 / 27.7 | 0 | 60.0 | 60.00 | 4852 | 9.36 | 4.92 | 16.43 | 58 / -46 | 12 (2.2 %) | 20.6 | 900 (900) | 21 | none | 6 -> 7 | 0 | pipe_r4b_paris_spawn_gl_first3 |
| ledge | auto | GREEN | 60 (60) | 0/589 (0.0 %) | 18.5 / 19.1 | 0 | 60.0 | 60.00 | 3091 | 6.74 | 3.82 | 4.61 | 1 / -1 | 0 (0.0 %) | 17.5 | 900 (900) | 21 | none | 11 -> 9 | 0 | pipe_r4b_ledge_auto |
| ledge | gl | GREEN | 60 (59) | 5/587 (0.9 %) | 19.1 / 21.2 | 0 | 60.0 | 60.00 | 3091 | 10.90 | 5.45 | 14.48 | 5 / -5 | 0 (0.0 %) | 18.1 | 900 (900) | 20 | none | 9 -> 8 | 1 | pipe_r4b_ledge_gl |
| job1_started | auto | GREEN | 60 (60) | 1/596 (0.2 %) | 17.1 / 23.1 | 0 | 59.9 | 60.00 | 3865 | 12.49 | 7.38 | 7.78 | 2 / -2 | 0 (0.0 %) | 17.9 | 900 (900) | 20 | none | 8 -> 8 | 2 | pipe_r4b_job1_started_auto |
| job1_started | gl | GREEN | 60 (60) | 1/594 (0.2 %) | 17.7 / 23.5 | 0 | 60.0 | 60.00 | 3865 | 7.83 | 3.81 | 10.58 | 0 / +0 | 0 (0.0 %) | 18.0 | 900 (900) | 21 | none | 6 -> 5 | 0 | pipe_r4b_job1_started_gl |
| job1_dish1 | auto | GREEN | 60 (60) | 0/597 (0.0 %) | 17.2 / 17.5 | 0 | 60.0 | 60.00 | 4135 | 8.09 | 4.70 | 5.02 | 1 / -1 | 0 (0.0 %) | 17.8 | 900 (900) | 21 | none | 8 -> 7 | 0 | pipe_r4b_job1_dish1_auto |
| job1_dish1 | gl | RED | 60 (60) | 23/587 (3.9 %) | 22.2 / 23.3 | 0 | 60.0 | 60.00 | 4135 | 8.15 | 4.11 | 11.69 | 3 / -3 | 0 (0.0 %) | 18.0 | 900 (900) | 21 | none | 5 -> 5 | 0 | pipe_r4b_job1_dish1_gl_first3 |
| briefing | auto | GREEN | 60 (60) | 0/591 (0.0 %) | 17.1 / 17.2 | 0 | 60.0 | 60.00 | 5057 | 12.05 | 6.95 | 10.19 | 0 / +0 | 0 (0.0 %) | 17.7 | 900 (900) | 21 | none | 8 -> 8 | 0 | pipe_r4b_briefing_auto |
| briefing | gl | RED | 54 (53) | 103/532 (19.4 %) | 23.7 / 24.8 | 0 | 60.0 | 60.00 | 5051 | 9.25 | 4.98 | 17.95 | 63 / -63 | 0 (0.0 %) | 20.2 | 900 (900) | 21 | none | 5 -> 4 | 0 | pipe_r4b_briefing_gl_first3 |
| cold_boot | auto | RED | 60 (34) | 75/1272 (5.9 %) | 36.0 / 439.9 | 9 | 55.7 | 59.95 | 2107 | 4.92 | 3.65 | 4.23 | 105 / -14 | 91 (7.2 %) | 23.1 | 1500 (1500) | 31 | none | 4 -> 6 | 0 | pipe_r4b_cold_boot_auto_first3 |
| cold_boot | gl | RED | 60 (33) | 80/1262 (6.3 %) | 35.8 / 413.0 | 13 | 55.8 | 59.95 | 2105 | 4.80 | 2.78 | 8.57 | 113 / -21 | 92 (7.3 %) | 18.7 | 1500 (1500) | 31 | none | 6 -> 7 | 0 | pipe_r4b_cold_boot_gl_first3 |

## Worst scenes (lowest pictures/s p50 first; DEFECT cells first)

- briefing / gl: RED -- p50 54 < 59 doubled 19.4 % > 1 %; frame_hz 60.0, draws/frame 5051, worker 9.2 ms, drain 5.0 ms, consumer 17.9 ms, load 5, other games 0
- paris_spawn / gl: RED -- p50 54 < 59 doubled 16.4 % > 1 %; frame_hz 60.0, draws/frame 4852, worker 9.4 ms, drain 4.9 ms, consumer 16.4 ms, load 6, other games 0
- hub_ep4 / gl: RED -- p50 56 < 59 doubled 10.5 % > 1 %; frame_hz 59.9, draws/frame 4182, worker 12.9 ms, drain 5.9 ms, consumer 18.1 ms, load 5, other games 0
- paris_spawn / auto: RED -- doubled 17.1 % > 1 %; frame_hz 60.0, draws/frame 4852, worker 9.4 ms, drain 5.6 ms, consumer 7.8 ms, load 6, other games 0
- hub_ep2 / gl: RED -- doubled 14.7 % > 1 %; frame_hz 59.5, draws/frame 2739, worker 8.5 ms, drain 4.5 ms, consumer 11.0 ms, load 6, other games 0
- cold_boot / gl: RED -- doubled 6.3 % > 1 %; frame_hz 55.8, draws/frame 2105, worker 4.8 ms, drain 2.8 ms, consumer 8.6 ms, load 6, other games 0
- cold_boot / auto: RED -- doubled 5.9 % > 1 %; frame_hz 55.7, draws/frame 2107, worker 4.9 ms, drain 3.6 ms, consumer 4.2 ms, load 4, other games 0
- job1_dish1 / gl: RED -- doubled 3.9 % > 1 %; frame_hz 60.0, draws/frame 4135, worker 8.1 ms, drain 4.1 ms, consumer 11.7 ms, load 5, other games 0

## Diff -- tag sm (before) -> tag r4b (after)

Per cell: verdict pictures/s-p50/doubled before -> after; draws/frame = same content?; worker ms and ns/bundle = same host state? (5-6 ns/bundle = quiet host, 9-11 = beside another game process).

#### sm -> r4b

| scene | backend | before | after | draws/f b -> a | worker ms b -> a | ns/bundle b -> a | changed |
|---|---|---|---|---|---|---|---|
| hub_ep0 | auto | **RED** 56/12.2% | GREEN 60/0.0% | 2965 -> 2965 | 6.6 -> 6.3 | 5.11 -> 4.91 | RED -> GREEN |
| hub_ep0 | gl | **RED** 56/8.5% | GREEN 60/0.3% | 2965 -> 2965 | 7.2 -> 7.8 | 5.58 -> 5.99 | RED -> GREEN |
| hub_ep1 | auto | **RED** 54/16.1% | GREEN 60/0.0% | 3039 -> 3061 | 7.2 -> 7.6 | 5.53 -> 5.82 | RED -> GREEN |
| hub_ep1 | gl | **RED** 37/75.4% | GREEN 60/0.0% | 4131 -> 3061 | 13.4 -> 6.4 | 7.55 -> 4.96 | RED -> GREEN |
| hub_ep2 | auto | **RED** 54/31.9% | GREEN 60/0.0% | 2731 -> 2738 | 10.6 -> 7.8 | 8.31 -> 6.08 | RED -> GREEN |
| hub_ep2 | gl | **RED** 52/32.8% | **RED** 60/14.7% | 2733 -> 2739 | 11.0 -> 8.5 | 8.60 -> 6.67 |  |
| hub_ep3 | auto | **RED** 41/60.7% | GREEN 60/0.0% | 3991 -> 4111 | 10.7 -> 8.7 | 5.95 -> 4.82 | RED -> GREEN |
| hub_ep3 | gl | **RED** 27/92.4% | GREEN 60/0.0% | 3991 -> 4111 | 14.2 -> 8.8 | 7.84 -> 4.87 | RED -> GREEN |
| hub_ep4 | auto | **RED** 40/94.8% | GREEN 60/0.3% | 4179 -> 4185 | 18.8 -> 11.0 | 8.97 -> 5.29 | RED -> GREEN |
| hub_ep4 | gl | **RED** 40/99.9% | **RED** 56/10.5% | 4179 -> 4182 | 19.0 -> 12.9 | 9.05 -> 6.18 |  |
| hub_ep5 | auto | **RED** 55/24.4% | GREEN 60/0.0% | 2616 -> 2597 | 7.9 -> 6.8 | 7.68 -> 6.63 | RED -> GREEN |
| hub_ep5 | gl | **RED** 54/30.7% | GREEN 60/0.3% | 2621 -> 2597 | 9.3 -> 5.1 | 8.96 -> 5.02 | RED -> GREEN |
| hub_ep6 | auto | **RED** 53/25.7% | GREEN 60/0.2% | 2741 -> 2630 | 8.2 -> 5.8 | 7.54 -> 5.55 | RED -> GREEN |
| hub_ep6 | gl | **RED** 51/40.4% | GREEN 60/0.0% | 2741 -> 2630 | 9.4 -> 4.9 | 8.56 -> 4.73 | RED -> GREEN |
| hub_ep7 | auto | **RED** 45/65.9% | GREEN 60/0.2% | 3881 -> 3869 | 12.1 -> 12.7 | 7.41 -> 7.78 | RED -> GREEN |
| hub_ep7 | gl | **RED** 33/100.0% | GREEN 60/0.0% | 3891 -> 3870 | 15.8 -> 7.3 | 9.63 -> 4.52 | RED -> GREEN |
| hub_ep8 | auto | **RED** 44/50.4% | GREEN 60/0.0% | 3317 -> 3344 | 10.6 -> 5.9 | 8.70 -> 4.76 | RED -> GREEN |
| hub_ep8 | gl | **RED** 39/52.0% | GREEN 60/0.0% | 3318 -> 3344 | 9.6 -> 5.7 | 7.83 -> 4.63 | RED -> GREEN |
| museum | auto | **RED** 56/7.8% | GREEN 60/0.0% | 2147 -> 2147 | 7.1 -> 4.8 | 7.19 -> 4.88 | RED -> GREEN |
| museum | gl | **RED** 56/8.0% | GREEN 60/0.2% | 2147 -> 2147 | 8.0 -> 5.0 | 8.01 -> 5.11 | RED -> GREEN |
| paris_spawn | auto | **RED** 43/85.8% | **RED** 60/17.1% | 4859 -> 4852 | 15.5 -> 9.4 | 8.14 -> 4.97 |  |
| paris_spawn | gl | **RED** 34/99.0% | **RED** 54/16.4% | 4862 -> 4852 | 16.5 -> 9.4 | 8.61 -> 4.92 |  |
| ledge | auto | **RED** 54/13.2% | GREEN 60/0.0% | 3074 -> 3091 | 9.9 -> 6.7 | 7.50 -> 5.08 | RED -> GREEN |
| ledge | gl | **RED** 35/89.9% | GREEN 60/0.9% | 4088 -> 3091 | 16.0 -> 10.9 | 9.19 -> 8.18 | RED -> GREEN |
| job1_started | auto | **RED** 43/75.3% | GREEN 60/0.2% | 3755 -> 3865 | 13.4 -> 12.5 | 8.48 -> 7.69 | RED -> GREEN |
| job1_started | gl | **RED** 42/86.0% | GREEN 60/0.2% | 3755 -> 3865 | 14.0 -> 7.8 | 8.80 -> 4.81 | RED -> GREEN |
| job1_dish1 | auto | **RED** 43/56.3% | GREEN 60/0.0% | 4123 -> 4135 | 14.1 -> 8.1 | 8.57 -> 4.90 | RED -> GREEN |
| job1_dish1 | gl | **RED** 37/74.1% | **RED** 60/3.9% | 4123 -> 4135 | 15.2 -> 8.1 | 9.18 -> 4.94 |  |
| briefing | auto | **RED** 39/99.3% | GREEN 60/0.0% | 5072 -> 5057 | 17.1 -> 12.1 | 8.93 -> 6.32 | RED -> GREEN |
| briefing | gl | **RED** 31/100.0% | **RED** 54/19.4% | 5077 -> 5051 | 18.6 -> 9.2 | 9.68 -> 4.86 |  |
| cold_boot | auto | **RED** 54/30.4% | **RED** 60/5.9% | 2028 -> 2107 | 7.3 -> 4.9 | 7.79 -> 5.04 |  |
| cold_boot | gl | **RED** 53/29.4% | **RED** 60/6.3% | 2024 -> 2105 | 6.9 -> 4.8 | 7.39 -> 4.91 |  |

GREEN 0/32 before -> 24/32 after; 24 cell(s) turned GREEN, 0 lost it.

## Before -- the cells of tag sm

### before: tag sm

Cells: 32 -- GREEN 0, RED 32, DEFECT 0. Bar per cell: pictures/s p50 >= 59 AND doubled frames (shown gaps > 20 ms) <= 1 % (skip 5 s). p50 = judge_pictures output lane; dbl = judge_pacing shown lane; Hz = run.meta clock frame_hz; `~load N` = the host's 1-min load at the cell's start was above 12; `~games N` = N other game / replay processes were running at its start (shared host: both are noise, re-run).

| scene | auto | gl | note |
|---|---|---|---|
| hub_ep0 | **RED** p50 56 / dbl 12.2 % / 56.1 Hz | **RED** p50 56 / dbl 8.5 % / 56.2 Hz | ep0 intro: Cairo museum hall (HUD) |
| hub_ep1 | **RED** p50 54 / dbl 16.1 % / 54.1 Hz | **RED** p50 37 / dbl 75.4 % / 39.9 Hz | ep1 nightclub: Paris hub, the reference scene (hub_ep1 is the stale-colour capture) |
| hub_ep2 | **RED** p50 54 / dbl 31.9 % / 49.0 Hz ~load 12 | **RED** p50 52 / dbl 32.8 % / 47.6 Hz ~load 12 | ep2 palace: jungle ledge (old-build capture: timing valid, colours stale) |
| hub_ep3 | **RED** p50 41 / dbl 60.7 % / 43.3 Hz | **RED** p50 27 / dbl 92.4 % / 30.8 Hz | ep3 temple: terraces |
| hub_ep4 | **RED** p50 40 / dbl 94.8 % / 40.8 Hz ~load 14 [2f+ 34.1 %: jitter] | **RED** p50 40 / dbl 99.9 % / 42.0 Hz [2f+ 42.1 %: jitter] | ep4 prison: the safehouse interior -- close-up skinned Bentley at the ThiefNet laptop, beam-dense room (a 3-D scene, not a 2-D menu) |
| hub_ep5 | **RED** p50 55 / dbl 24.4 % / 52.7 Hz [2f+ 8.5 %: jitter] | **RED** p50 54 / dbl 30.7 % / 50.5 Hz | ep5 castle: roof |
| hub_ep6 | **RED** p50 53 / dbl 25.7 % / 51.2 Hz [2f+ 10.3 %: jitter] | **RED** p50 51 / dbl 40.4 % / 48.8 Hz [2f+ 17.5 %: jitter] | ep6 train: ground |
| hub_ep7 | **RED** p50 45 / dbl 65.9 % / 44.9 Hz [2f+ 22.7 %: jitter] | **RED** p50 33 / dbl 100.0 % / 36.8 Hz | ep7 sawmill: binocucom view |
| hub_ep8 | **RED** p50 44 / dbl 50.4 % / 42.4 Hz | **RED** p50 39 / dbl 52.0 % / 44.4 Hz | ep8 blimp: deck |
| museum | **RED** p50 56 / dbl 7.8 % / 56.6 Hz | **RED** p50 56 / dbl 8.0 % / 56.6 Hz ~load 13 | Cairo museum (the intro level, frame 1471) |
| paris_spawn | **RED** p50 43 / dbl 85.8 % / 43.5 Hz [2f+ 16.9 %: jitter] | **RED** p50 34 / dbl 99.0 % / 38.8 Hz | the dense Paris spawn view (4913 draws), no input |
| ledge | **RED** p50 54 / dbl 13.2 % / 53.8 Hz | **RED** p50 35 / dbl 89.9 % / 35.0 Hz ~load 15 ~games 1 | Paris ledge (frame 3090) |
| job1_started | **RED** p50 43 / dbl 75.3 % / 43.4 Hz [2f+ 23.2 %: jitter] | **RED** p50 42 / dbl 86.0 % / 40.2 Hz [2f+ 42.0 %: jitter] | mission: Satellite Sabotage, on the plaza after the briefing |
| job1_dish1 | **RED** p50 43 / dbl 56.3 % / 41.9 Hz | **RED** p50 37 / dbl 74.1 % / 39.2 Hz | mission: at dish 1 (the water tower) |
| briefing | **RED** p50 39 / dbl 99.3 % / 38.9 Hz | **RED** p50 31 / dbl 100.0 % / 34.9 Hz | cutscene: the Satellite Sabotage binocucom briefing (runs 0..~2200 on this state) |
| cold_boot | **RED** p50 54 / dbl 30.4 % / 47.3 Hz | **RED** p50 53 / dbl 29.4 % / 48.7 Hz | cold boot: Sly logo -> intro FMV -> attract / title, no input |

### Detail (every cell)

| scene | backend | verdict | pictures/s p50 (p10) | doubled | shown p99 / max ms | >40 ms | frame_hz | logic_hz | draws/frame | worker ms | drain ms | consumer ms | floor / presenter | 2f+ fields | output gap p90 | frames (bound.) | wall s | park | load start -> end | games | out |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| hub_ep0 | auto | RED | 56 (56) | 74/609 (12.2 %) | 30.0 / 34.6 | 0 | 56.1 | 60.00 | 2965 | 6.60 | 4.22 | 4.67 | 42 / +0 | 42 (6.9 %) | 23.2 | 900 (900) | 20 | none | 5 -> 7 | 0 | pipe_sm_hub_ep0_auto |
| hub_ep0 | gl | RED | 56 (56) | 52/612 (8.5 %) | 32.1 / 33.0 | 0 | 56.2 | 60.00 | 2965 | 7.21 | 4.03 | 13.32 | 42 / -5 | 37 (6.0 %) | 20.4 | 900 (900) | 20 | none | 7 -> 8 | 0 | pipe_sm_hub_ep0_gl |
| hub_ep1 | auto | RED | 54 (54) | 101/629 (16.1 %) | 33.0 / 33.9 | 0 | 54.1 | 60.00 | 3039 | 7.16 | 4.30 | 5.11 | 68 / +0 | 68 (10.8 %) | 26.1 | 900 (900) | 21 | none | 8 -> 8 | 0 | pipe_sm_hub_ep1_auto |
| hub_ep1 | gl | RED | 37 (34) | 471/625 (75.4 %) | 40.6 / 43.5 | 8 | 39.9 | 59.93 | 4131 | 13.35 | 7.09 | 19.36 | 307 / +36 | 343 (54.9 %) | 31.5 | 900 (900) | 25 | none | 8 -> 11 | 0 | pipe_sm_hub_ep1_gl |
| hub_ep2 | auto | RED | 54 (33) | 220/690 (31.9 %) | 36.9 / 46.5 | 5 | 49.0 | 60.00 | 2731 | 10.64 | 6.36 | 7.72 | 154 / -7 | 147 (21.3 %) | 30.9 | 900 (900) | 26 | none | 12 -> 12 | 0 | pipe_sm_hub_ep2_auto |
| hub_ep2 | gl | RED | 52 (34) | 231/704 (32.8 %) | 42.7 / 49.3 | 12 | 47.6 | 60.00 | 2733 | 11.03 | 6.00 | 15.33 | 175 / +2 | 177 (25.1 %) | 31.2 | 900 (900) | 26 | none | 12 -> 14 | 0 | pipe_sm_hub_ep2_gl |
| hub_ep3 | auto | RED | 41 (37) | 395/651 (60.7 %) | 40.1 / 43.5 | 7 | 43.3 | 60.00 | 3991 | 10.72 | 7.01 | 7.58 | 238 / -35 | 203 (31.2 %) | 30.9 | 900 (900) | 26 | none | 5 -> 6 | 0 | pipe_sm_hub_ep3_auto |
| hub_ep3 | gl | RED | 27 (25) | 641/694 (92.4 %) | 53.6 / 78.9 | 124 | 30.8 | 60.13 | 3991 | 14.23 | 7.95 | 19.69 | 665 / +1 | 666 (96.0 %) | 43.4 | 900 (900) | 31 | none | 7 -> 9 | 0 | pipe_sm_hub_ep3_gl |
| hub_ep4 | auto | RED | 40 (38) | 675/712 (94.8 %) | 41.3 / 150.2 | 10 | 40.8 | 60.00 | 4179 | 18.76 | 10.64 | 12.07 | 341 / -98 | 243 (34.1 %) | 28.1 | 900 (900) | 26 | none | 14 -> 15 | 0 | pipe_sm_hub_ep4_auto |
| hub_ep4 | gl | RED | 40 (39) | 676/677 (99.9 %) | 30.4 / 40.3 | 1 | 42.0 | 59.94 | 4179 | 18.95 | 9.15 | 25.10 | 336 / -51 | 285 (42.1 %) | 26.8 | 900 (900) | 26 | none | 12 -> 11 | 0 | pipe_sm_hub_ep4_gl |
| hub_ep5 | auto | RED | 55 (47) | 160/655 (24.4 %) | 32.4 / 37.3 | 0 | 52.7 | 60.00 | 2616 | 7.95 | 4.69 | 6.13 | 89 / -33 | 56 (8.5 %) | 25.4 | 900 (900) | 21 | none | 11 -> 9 | 0 | pipe_sm_hub_ep5_auto |
| hub_ep5 | gl | RED | 54 (40) | 212/691 (30.7 %) | 34.4 / 46.2 | 1 | 50.5 | 60.07 | 2621 | 9.31 | 4.64 | 14.10 | 128 / -22 | 106 (15.3 %) | 27.8 | 900 (900) | 25 | none | 9 -> 12 | 0 | pipe_sm_hub_ep5_gl |
| hub_ep6 | auto | RED | 53 (46) | 168/653 (25.7 %) | 34.6 / 77.1 | 3 | 51.2 | 60.00 | 2741 | 8.23 | 4.87 | 6.38 | 109 / -42 | 67 (10.3 %) | 25.8 | 900 (900) | 20 | none | 12 -> 10 | 0 | pipe_sm_hub_ep6_auto |
| hub_ep6 | gl | RED | 51 (37) | 276/684 (40.4 %) | 35.3 / 39.5 | 0 | 48.8 | 60.00 | 2741 | 9.38 | 4.90 | 14.47 | 157 / -37 | 120 (17.5 %) | 29.5 | 900 (900) | 26 | none | 10 -> 12 | 0 | pipe_sm_hub_ep6_gl |
| hub_ep7 | auto | RED | 45 (42) | 427/648 (65.9 %) | 45.0 / 92.2 | 12 | 44.9 | 60.07 | 3881 | 12.10 | 6.90 | 11.57 | 235 / -88 | 147 (22.7 %) | 29.6 | 900 (900) | 26 | none | 12 -> 19 | 0 | pipe_sm_hub_ep7_auto |
| hub_ep7 | gl | RED | 33 (31) | 617/617 (100.0 %) | 38.9 / 48.1 | 4 | 36.8 | 59.94 | 3891 | 15.83 | 8.05 | 29.55 | 514 / +89 | 603 (97.7 %) | 33.9 | 900 (900) | 31 | none | 12 -> 11 | 0 | pipe_sm_hub_ep7_gl |
| hub_ep8 | auto | RED | 44 (31) | 347/688 (50.4 %) | 43.5 / 87.5 | 19 | 42.4 | 60.00 | 3317 | 10.62 | 6.56 | 8.31 | 283 / +30 | 313 (45.5 %) | 33.4 | 900 (900) | 26 | none | 11 -> 11 | 0 | pipe_sm_hub_ep8_auto |
| hub_ep8 | gl | RED | 39 (34) | 333/641 (52.0 %) | 39.4 / 46.3 | 5 | 44.4 | 60.00 | 3318 | 9.61 | 4.89 | 14.32 | 220 / +23 | 243 (37.9 %) | 30.2 | 900 (900) | 25 | none | 11 -> 9 | 0 | pipe_sm_hub_ep8_gl |
| museum | auto | RED | 56 (55) | 46/588 (7.8 %) | 33.7 / 296.7 | 1 | 56.6 | 60.00 | 2147 | 7.11 | 4.71 | 5.70 | 52 / -13 | 39 (6.6 %) | 19.6 | 900 (900) | 21 | none | 9 -> 9 | 0 | pipe_sm_museum_auto |
| museum | gl | RED | 56 (55) | 48/603 (8.0 %) | 33.2 / 34.3 | 0 | 56.6 | 60.00 | 2147 | 7.96 | 4.39 | 11.33 | 37 / -1 | 36 (6.0 %) | 19.3 | 900 (900) | 21 | none | 13 -> 12 | 0 | pipe_sm_museum_gl |
| paris_spawn | auto | RED | 43 (41) | 568/662 (85.8 %) | 37.1 / 42.7 | 2 | 43.5 | 59.86 | 4859 | 15.52 | 9.42 | 12.33 | 251 / -139 | 112 (16.9 %) | 26.9 | 900 (900) | 26 | none | 12 -> 11 | 0 | pipe_sm_paris_spawn_auto |
| paris_spawn | gl | RED | 34 (31) | 578/584 (99.0 %) | 41.8 / 58.1 | 8 | 38.8 | 60.11 | 4862 | 16.48 | 9.02 | 26.88 | 450 / +82 | 532 (91.1 %) | 33.3 | 900 (900) | 25 | none | 11 -> 11 | 0 | pipe_sm_paris_spawn_gl |
| ledge | auto | RED | 54 (53) | 89/674 (13.2 %) | 34.1 / 38.4 | 0 | 53.8 | 60.00 | 3074 | 9.94 | 6.04 | 6.94 | 77 / -6 | 71 (10.5 %) | 27.3 | 900 (900) | 21 | none | 11 -> 11 | 0 | pipe_sm_ledge_auto |
| ledge | gl | RED | 35 (28) | 657/731 (89.9 %) | 49.1 / 132.0 | 41 | 35.0 | 60.09 | 4088 | 16.00 | 8.05 | 24.54 | 524 / -11 | 513 (70.2 %) | 37.0 | 900 (900) | 31 | none | 15 -> 19 | 1 | pipe_sm_ledge_gl |
| job1_started | auto | RED | 43 (39) | 496/659 (75.3 %) | 39.9 / 53.6 | 7 | 43.4 | 60.00 | 3755 | 13.44 | 8.13 | 8.67 | 253 / -100 | 153 (23.2 %) | 28.5 | 900 (900) | 26 | none | 9 -> 10 | 0 | pipe_sm_job1_started_auto |
| job1_started | gl | RED | 42 (34) | 569/662 (86.0 %) | 41.1 / 74.4 | 11 | 40.2 | 60.06 | 3755 | 13.98 | 6.97 | 18.83 | 324 / -46 | 278 (42.0 %) | 30.3 | 900 (900) | 26 | none | 9 -> 9 | 0 | pipe_sm_job1_started_gl |
| job1_dish1 | auto | RED | 43 (32) | 389/691 (56.3 %) | 43.4 / 49.6 | 15 | 41.9 | 60.06 | 4123 | 14.12 | 8.84 | 9.54 | 293 / -7 | 286 (41.4 %) | 32.6 | 900 (900) | 26 | none | 9 -> 9 | 0 | pipe_sm_job1_dish1_auto |
| job1_dish1 | gl | RED | 37 (29) | 533/719 (74.1 %) | 45.1 / 53.1 | 23 | 39.2 | 59.94 | 4123 | 15.18 | 7.79 | 21.54 | 382 / -57 | 325 (45.2 %) | 35.4 | 900 (900) | 31 | none | 9 -> 11 | 0 | pipe_sm_job1_dish1_gl |
| briefing | auto | RED | 39 (37) | 688/693 (99.3 %) | 39.0 / 41.5 | 5 | 38.9 | 60.00 | 5072 | 17.10 | 10.43 | 14.50 | 375 / -19 | 356 (51.4 %) | 28.7 | 900 (900) | 26 | none | 11 -> 11 | 0 | pipe_sm_briefing_auto |
| briefing | gl | RED | 31 (30) | 623/623 (100.0 %) | 40.1 / 43.6 | 7 | 34.9 | 60.00 | 5077 | 18.60 | 10.05 | 31.63 | 598 / +28 | 626 (100.5 %) | 35.3 | 900 (900) | 31 | none | 11 -> 14 | 0 | pipe_sm_briefing_gl |
| cold_boot | auto | RED | 54 (30) | 397/1304 (30.4 %) | 49.8 / 574.4 | 22 | 47.3 | 60.00 | 2028 | 7.32 | 5.27 | 6.25 | 339 / -98 | 241 (18.5 %) | 32.0 | 1500 (1500) | 36 | none | 10 -> 9 | 0 | pipe_sm_cold_boot_auto |
| cold_boot | gl | RED | 53 (28) | 375/1275 (29.4 %) | 49.2 / 420.5 | 14 | 48.7 | 59.96 | 2024 | 6.88 | 4.09 | 11.22 | 316 / -83 | 233 (18.3 %) | 32.7 | 1500 (1500) | 36 | none | 12 -> 11 | 0 | pipe_sm_cold_boot_gl |

### Worst scenes (lowest pictures/s p50 first; DEFECT cells first)

- hub_ep3 / gl: RED -- p50 27 < 59 doubled 92.4 % > 1 %; frame_hz 30.8, draws/frame 3991, worker 14.2 ms, drain 8.0 ms, consumer 19.7 ms, load 7, other games 0
- briefing / gl: RED -- p50 31 < 59 doubled 100.0 % > 1 %; frame_hz 34.9, draws/frame 5077, worker 18.6 ms, drain 10.1 ms, consumer 31.6 ms, load 11, other games 0
- hub_ep7 / gl: RED -- p50 33 < 59 doubled 100.0 % > 1 %; frame_hz 36.8, draws/frame 3891, worker 15.8 ms, drain 8.1 ms, consumer 29.6 ms, load 12, other games 0
- paris_spawn / gl: RED -- p50 34 < 59 doubled 99.0 % > 1 %; frame_hz 38.8, draws/frame 4862, worker 16.5 ms, drain 9.0 ms, consumer 26.9 ms, load 11, other games 0
- ledge / gl: RED -- p50 35 < 59 doubled 89.9 % > 1 %; frame_hz 35.0, draws/frame 4088, worker 16.0 ms, drain 8.0 ms, consumer 24.5 ms, load 15, other games 1
- hub_ep1 / gl: RED -- p50 37 < 59 doubled 75.4 % > 1 %; frame_hz 39.9, draws/frame 4131, worker 13.4 ms, drain 7.1 ms, consumer 19.4 ms, load 8, other games 0
- job1_dish1 / gl: RED -- p50 37 < 59 doubled 74.1 % > 1 %; frame_hz 39.2, draws/frame 4123, worker 15.2 ms, drain 7.8 ms, consumer 21.5 ms, load 9, other games 0
- briefing / auto: RED -- p50 39 < 59 doubled 99.3 % > 1 %; frame_hz 38.9, draws/frame 5072, worker 17.1 ms, drain 10.4 ms, consumer 14.5 ms, load 11, other games 0
