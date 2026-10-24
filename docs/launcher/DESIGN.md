# The launcher -- design specification

This is the recorded design of the NativeEE launcher as shipped for Sly 2: Band of Thieves. The user's brief named a
`docs/launcher/DESIGN.md` "merged at e2ee6168"; neither exists in the TwinsanityEvolved tree nor in this one, so the
rules below are reconstructed from the implementation that does exist (`engine/app/launcher/launcher_theme.*`,
`launcher_style.*`, the controls) and the three TwinsanityEvolved screenshots, then extended for the setup window
(section 6; decision D-L10 replaced the earlier five-page first run). The launcher code is the engine's and is
title-neutral; everything Sly-shaped is in `launcher/title.json`.

## 1. Who it is for, in one sentence

A player who owns the disc, downloads one application, opens it, chooses the disc image, sets a few things up, presses
Build the game, and then Play. The launcher never looks for the disc by itself (D-L10), never shows a developer word
(see section 9) and never refuses anything silently (section 7).

## 2. Window

- Minimum 1024 x 640; opens at 1120 x 720 or the size it was closed at (remembered in launcher.json).
- Title bar: the game's name (`name`).
- Before the game is built the setup window fills the window (section 6). After it: a 220 px sidebar at the left
  (the game's name, "Launcher <version>", the four pages Games / Save States / Settings / About, the game folder with
  Open at the bottom) and the current page at the right. Cmd/Ctrl+1..4 switch pages; Cmd/Ctrl+Shift+S writes a
  screenshot to Logs/.
- Light or dark follows the system (`LauncherTheme::followSystem`); every colour is a theme role, never a literal.

## 3. Palette (role: light / dark)

| Role | Light | Dark | Used for |
|---|---|---|---|
| Window | #F3F4F6 | #141518 | the page background |
| Sidebar | #E9EBEF | #1B1C21 | the sidebar column |
| Surface | #FFFFFF | #22242A | cards, sheets, inputs |
| SurfaceRaised | #FFFFFF | #2A2D34 | a hovered card, menus |
| Line | #D9DCE2 | #33363E | 1 px borders, the progress track, separators |
| Text | #15171B | #F0F1F4 | titles, labels |
| TextMuted | #5E6470 | #A1A6B2 | descriptions, facts |
| TextFaint | #8A909C | #6D7380 | the step counter, disabled text |
| Accent | #D9541E | #F0693A | the primary button, the selected sidebar item, the progress fill, selection rings |
| AccentText | #FFFFFF | #141518 | text on Accent |
| AccentSoft | #FBE9E0 | #3A251C | the selected segment, the selected sidebar item's fill |
| Ok | #2E8B57 | #4CC27B | "Ready to play." |
| Warn | #B7791F | #E0A43A | the "Not supported yet" badge and reason, an out-of-date state |
| Error | #C0392B | #E8604F | a failure sentence, a missing disc |

## 4. Type scale (pixel sizes, the system font; the fixed-pitch system font for Mono)

| Style | Size | Weight | Used for |
|---|---|---|---|
| Display | 28 | DemiBold | the page title (PageHeader), "Ready to play" |
| Title | 17 | DemiBold | card titles, group titles, sheet headings |
| Body | 13 | Normal | descriptions, status lines, help |
| Label | 13 | Medium | row labels, small section titles |
| Caption | 11 | Normal | facts, the step counter, notes, badge text |
| Mono | 12 | - | the Details log, the folder layout preview |

Pixel sizes, not points: a point is one pixel at macOS's 72 dpi but 1.33 at Windows' 96, so points made every text a
third larger on Windows.

## 5. Spacing, shape, controls

- Spacing tokens S1..S6 = 4 / 8 / 16 / 24 / 32 / 48 px. Page content margins: 32 left/right/bottom; page header 16
  above the title, 24 below the description. Cards 16 apart; card padding 16.
- Radii: controls 6, cards and sheets 10. Control height 32; the one thing to do on a page is 40 high ("large").
  Rows (FolderRow, SettingsRow) are 48 high with a 1 px Line below.
- Buttons (FlatButton): Primary = Accent fill + AccentText; Secondary = 1 px Line outline; Quiet = Accent text only.
  Focus = a 2 px Accent ring outside the control. One Primary per screen.
- Segmented control: up to six choices as one flat bar, the selected segment AccentSoft with Accent text; more than six
  choices become a popup button. Switch: a 40 x 22 pill, Accent when on.
- Cards: Surface, 1 px Line, radius 10; SurfaceRaised on hover; a selected card (first run) has a 2 px Accent ring. A
  card's artwork is generated from the title id (two tones + a diagonal band + the initials): nothing from the disc.
- Progress (ProgressPanel): a line of text above (what is happening), a 4 px bar (Line track, Accent fill; a sweeping
  quarter when indeterminate), a Caption below (percent and time left). There is exactly ONE of these, at the bottom
  of the setup window while the game is built, and nowhere else.
- Sheets (SheetOverlay): the page dimmed 40 %, a Surface panel 640 wide (720 for the setup window's details), heading in
  Title, buttons bottom right, Escape presses the cancelling button. Details is a Mono log with Copy and Close.
- The badge: Caption text in Warn on a soft Warn fill (#FBF1DC light / #3A3220 dark), radius 9, beside the card title.

## 6. The setup window (decision D-L10: ONE window, the player chooses the disc)

The user's correction, recorded as **D-L10** (it supersedes the five-step first run of the first design): *"The launcher
should open and let me select the game ISO to recompile; it should NOT look for it automatically. The process: I open
the launcher and a window pops up with a bunch of settings to set up and then also a place where to select the ISO and
build the game."* So: no folder or volume scanning anywhere (the `DiscScanner` is gone; a `scan` section in an older
manifest is ignored), and the first run is one scrolling page, "Set up your game", with ONE primary button at the
bottom. Top to bottom:

1. **Your game** -- a path field ("Drop your disc image here, or choose it…"), **Choose…** (a file dialog filtered by
   `disc.extensions`), and drag-and-drop anywhere on the window. The chosen file is identified by CONTENT (section 8)
   and shown as one card: the supported disc gets the selection ring and "Your disc, recognised by what is on it. Press
   Build the game below."; another region or a modified image gets the badge "Not supported yet" with the manifest's
   reason in Warn; a file that is not a disc image or is another game shows the reason in Error below the field. The
   Build button is enabled ONLY for a supported disc; while it is disabled a Caption beside it says why ("Choose your
   disc image first.", the region reason, "The disc image is no longer where it was.").
2. **Where the game lives** -- the **Game folder** row (the manifest's `game_folder.default`, for Sly 2
   `~/Documents/Sly 2`, else `~/Documents/<safe name>`; Change…; Open in Finder / Explorer), then one FolderRow per
   `game_folder.subfolders` entry with `ask` (for Sly 2 **Memory cards** and **Save states**; the cache is the
   pipeline's and is not asked -- it stays on the Settings page's Folders group), each with its plain caption
   (`help`: "Your game's saved progress. Inside the game folder unless you choose somewhere else." / "Snapshots you
   can resume from."), then the note. A sub-folder shows its RELATIVE form while it is inside the game folder
   (`memcards`, `saves`) and follows the game folder when that changes; one chosen elsewhere shows the absolute path
   and the note changes from the manifest's `portable_note` to "The memory cards live outside the game folder;
   moving the folder leaves them where they are." (one sentence per folder outside) -- D-L15. Each sub-folder is the
   options file's own key the GAME reads (`subfolders.<id>.key`: `paths.mc_dir`, `paths.save_dir`,
   `paths.user_data`), written as an absolute path into `<folder>/nativeee.ini` when the folder is chosen, when Build
   is pressed and again after the pipeline rewrote the file (D-L13); the Settings page's Folders group reads the same
   keys, so the setup window, Settings and the game share ONE store. The folder is only created when Build is pressed
   (or when Change… picks one); a sub-folder chosen before that waits in the model.
3. **Settings** -- the manifest's groups and rows (section 10), the same rows as the Settings page, filtered for this
   machine (`choices[].platforms`), with the `locked_by` rule. Before the game is built the values are kept in the
   window and written to `<folder>/nativeee.ini` when Build is pressed, and again when the pipeline finishes (it writes
   a fresh options file -- D-L13). With a game folder already chosen (a returning player) a change is written at once.
4. **Advanced** (a Quiet toggle, closed by default) -- "Which computer": a segmented control of the manifest's
   `targets`, this machine preselected. The bundled pipeline makes the game only for the computer it runs on, so another
   choice shows the honest note in Warn and disables Build (D-L11); the section stays for a release that can.
5. **Build the game** -- the one Primary button (large, bottom right; "Not now" beside it leaves for the pages). It
   chooses the suggested folder when none was, makes the sub-folders, writes the settings, and runs the PrepareRunner.
   While building: the ONE progress bar at the bottom of the same window (the pipeline's phase line above --
   "Getting the files your game needs", "Checking your disc", "Preparing your game (12 of 38)" --, "NN% · about N
   minutes left" below, the estimate "usually about 1 to 2 minutes" before the first progress), **Stop** and
   **Details**; the inputs above are disabled. On failure: the sentence in Error (the pipeline's own plain text), **Try
   again** (Primary), Details, Not now. Done: the bar full, "Your game is ready", "<name> is in <folder>. Press Play, or
   start it from that folder whenever you like.", **Play** (Primary) and "Go to the launcher"; the card now reads
   "Ready to play.".

Then the window becomes the launcher (Games / Save States / Settings / About). A returning player (folder chosen, the
game built from the current disc by this launcher version) lands directly on Games, never on the setup window. The
setup window comes back from Games > **Add a game…** (also the empty state's button; empty picker, folder and settings
as they are), from a card whose game is not built yet, and from the game folder's Change… on Settings (opened on the
current disc).

Headless, for CI and the cold-start judge: `ps2r_launcher --prepare --index URL --iso IMAGE --folder DIR --jobs N
--json REPORT [--subfolder memcards=DIR] [--subfolder saves=DIR]` runs the same BundledPrepareRunner without a window, prints progress on stderr and the game's path on
stdout, writes the same report the command-line tool does, exits 0 when the game is ready. `--index URL` (and
`--release TAG`) also override the manifest's `release.index_url` in the window, so an offline release can be judged.

## 7. States that must exist (and never be silent)

- **Empty** (EmptyState): a 64 px disc glyph, a Title sentence, a Body sentence, one Primary button and an optional
  Quiet one. Games: "No game yet" + "Add your own disc image of <name> and the launcher builds the game from it…" +
  "Add a game…". The setup window with no disc: "No disc image chosen yet." under the field. Save States: "No save
  states yet" + how to make one. Settings and Save States without a game folder: "Choose where the game lives first" +
  "Choose a folder…".
- **Error**: a file that is not a disc image ("… cannot be read as a disc image (…)"), a disc of another game ("This
  is not a <name> disc image. "<file>" has no <executable> on it.") -- in Error under the setup window's field (and as
  an EmptyState in Error tone on Games after a drop); a Play that failed.
- **Recognised but not supported**: a card WITH the badge "Not supported yet" and the manifest's reason in Warn
  (another region: `disc.alternates[].reason`; a modified image: `disc.modified_reason`). No action button. Shown after
  the supported cards.
- **Missing**: the disc file is gone -> the card says so in Error and offers Remove.
- **Out of date**: built by an older launcher -> Warn status, the action reads "Build the game" (opens the setup window).
- **Building failed**: section 6, step 5. **No pipeline bundled** (a launcher built with PS2R_LAUNCHER_PREPARE=OFF):
  the same state with the honest sentence from UnavailablePrepareRunner (never a fake success).

## 8. What a disc IS (identification by content)

The file name is never consulted. The ISO 9660 primary volume descriptor and root directory are read; the manifest's
`disc.fingerprint.file` (the executable's name on the disc) must be in the root; `disc.required_files` must be there;
the executable's bytes are hashed (SHA-256) and compared with `disc.fingerprint.sha256`. Outcomes: Supported /
OtherRegion (an `alternates[]` entry with `supported:false`) / Modified (the right executable, another checksum) /
NotTheGame / NotAnIso / Unreadable. `ps2r_launcher --identify IMAGE` prints the outcome and exits 0 for Supported.

## 9. Words (enforced by tools/launcher_lint.py = ctest LauncherLint, and by LauncherScreens over visible strings)

- No codename anywhere in engine/app/launcher: crash, twinsanity, sly, cooper, slus, sles, sces, scus, scps, scka.
  The game's name, ids and regions come from title.json at run time.
- No developer vocabulary in any string a player could read: recompile, shard, AOT, cmake, ninja, clang, toolchain,
  git clone, python, compiler, linker, ELF, sha256, json. The step has ONE name while it runs: "Preparing your game"
  (the pipeline's phase lines, tools/prepare/ui_strings.h). The button that starts it reads "Build the game" (the
  user's word; never "recompile" or "prepare" on a button), and a built game is "built" / "Ready to play.".
- No placeholder text (lorem, TODO, TBD, placeholder, TextLabel), no emoji.
- Sentences, not fragments; the game is "the game" or its name; the user is "you"; folders are "folders"; the disc
  image is "your disc image" or "the disc". Times are honest: "about 4 minutes left", "under a minute left", "time left
  not known yet" -- never a bar that lies.
- Ellipses only on actions that open a picker or another window ("Choose…", "Change…", "Add a game…", "Forget…").

## 10. Files

- `launcher/title.json` (schema 2) -- the only Sly-shaped input: `release.index_url` (default the GitHub Releases API
  of `release.repo`), `release.estimate`, `game_folder.default`, `game_folder.subfolders.<id>` = `{label, default,
  key, help, ask}` (the key the game reads for that folder; `ask` = asked on the setup window, default true except
  for `cache`); no `scan` section any more.
  `tests/app/launcher/test_launcher_screens.cpp` uses a neutral "Example Game" manifest so the lint applies to engine
  strings.
- `<game folder>/prepared.json` -- disc checksum, platform, launcher version, when; decides Play vs Build the game. The
  pipeline's own stamp is `<game folder>/cache/prepared.json` (release tag, bundle, binary) and its log
  `<game folder>/cache/logs/prepare.log` (what Details opens).
- `~/Library/Application Support/<app_data_folder>/launcher.json` (Windows %LOCALAPPDATA%, Linux ~/.local/share) --
  the launcher's own state: the discs, the game folder, the window. `Logs/` beside it. The sub-folders are NOT here:
  they are `<game folder>/nativeee.ini`'s `paths.mc_dir` / `paths.save_dir` / `paths.user_data` (D-L15).
- `docs/launcher/screenshots/*.png` -- every screen, written by the screens test at 1024 x 640.

## 11. Decisions recorded for the setup window

- **D-L10** (the user's correction, 2026-10-24): the launcher never looks for the disc; one setup window with the disc
  picker, the folder, the settings and "Build the game"; supersedes the five-step first run (D-L2's Desktop suggestion
  is superseded by D-L12).
- **D-L11**: the bundled pipeline makes the game for this computer only; "Which computer" lives under Advanced with
  the honest note for another choice, instead of pretending a cross-build.
- **D-L12**: the game folder is suggested as `game_folder.default` (`~/Documents/Sly 2`), else `~/Documents/<name>`.
- **D-L13**: the pipeline rewrites `nativeee.ini` when it finishes; the launcher writes the player's settings choices
  again after `finished()` (and once before, when Build is pressed), so nothing chosen on the setup window is lost.
- **D-L14**: the headless `--prepare` mode is the launcher's own BundledPrepareRunner on a QCoreApplication, not a
  shell-out to nativeee-prepare: the judge's launcher driver runs the code path the Build button runs.
- **D-L15** (the user's request, 2026-10-24: "put a setting to ask the user where the memory card and save states
  should be"): the setup window asks, under the Game folder row, with the manifest's `subfolders` that carry `ask`.
  The store is the options file the game reads -- `paths.mc_dir` for the memory cards, `paths.save_dir` for the save
  states (`SavestateSlots::Root`: `<save_dir>/savestates/slot<N>.ps2st`; the launcher's Save States page lists
  `${saves}/savestates`, the same place), `paths.user_data` for the cache -- written as absolute paths (the engine
  resolves no relative path), so a game started from the folder without the launcher reads the same places. The
  launcher keeps the relative/absolute distinction by position: a path inside the game folder is shown relative and
  follows the game folder when it changes; one outside stays where it is and the note says so. Before this the
  pipeline's fresh `nativeee.ini` carried no `paths.save_dir`, so a game started from the folder put its slots under
  `<folder>/cache/logs/savestates` while the launcher listed `<folder>/saves/savestates`; the re-apply after Build
  (D-L13) now closes that gap. The `--set paths.*=` run arguments stay (the same values; harmless).
- **D-L16**: the frozen `launcher_settings_judge.py` keeps `paths.mc_dir` / `paths.save_dir` "not judged" (its leg
  pins the memory card itself) and is not edited; the two rows are judged through the launcher's own path by the
  engine's `tests/app/launcher/launcher_folders_judge.py` (headless Build with `--subfolder memcards=DIR --subfolder
  saves=DIR`, the file read back by `nativeee_settings`, the game started FROM the folder with
  `harness.save_at_frame` writing its slot under the chosen save states folder).
