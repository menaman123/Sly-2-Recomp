# The launcher -- design specification

This is the recorded design of the NativeEE launcher as shipped for Sly 2: Band of Thieves. The user's brief named a
`docs/launcher/DESIGN.md` "merged at e2ee6168"; neither exists in the TwinsanityEvolved tree nor in this one, so the
rules below are reconstructed from the implementation that does exist (`engine/app/launcher/launcher_theme.*`,
`launcher_style.*`, the controls) and the three TwinsanityEvolved screenshots, then extended for the five-page first
run. The launcher code is the engine's and is title-neutral; everything Sly-shaped is in `launcher/title.json`.

## 1. Who it is for, in one sentence

A player who owns the disc, downloads one application, opens it, points it at the disc image, and presses Play. The
launcher never shows a developer word (see section 9) and never refuses anything silently (section 7).

## 2. Window

- Minimum 1024 x 640; opens at 1120 x 720 or the size it was closed at (remembered in launcher.json).
- Title bar: the game's name (`name`).
- Before the game is prepared the first run fills the window (section 6). After it: a 220 px sidebar at the left
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
  quarter when indeterminate), a Caption below (percent and time left). There is exactly ONE of these on the
  Preparing page and nowhere else.
- Sheets (SheetOverlay): the page dimmed 40 %, a Surface panel 640 wide (720 for the first run's details), heading in
  Title, buttons bottom right, Escape presses the cancelling button. Details is a Mono log with Copy and Close.
- The badge: Caption text in Warn on a soft Warn fill (#FBF1DC light / #3A3220 dark), radius 9, beside the card title.

## 6. The first run (five pages, "Step N of 5" in TextFaint above each title)

1. **Where is your game?** -- the discs found in ~/Downloads, ~/Desktop, ~/Documents and removable volumes (the
   manifest's `scan`), plus drag-and-drop anywhere on the list and "Add a disc image…". One card per image, identified
   by CONTENT (section 7). Supported cards are selectable (click, Space or Return); the first supported one is selected
   by itself. Next is disabled until one is selected. "Not now" leaves for the pages.
2. **Where should the game live?** -- one FolderRow (a suggested `~/Desktop/<Game name>`, Change…, Open in Finder /
   Explorer) and a Mono preview of what goes in it: `<binary>`, `nativeee.ini`, `memcards/`, `saves/`, `cache/`, then
   the manifest's `portable_note`. Next commits the suggestion when nothing was changed.
3. **Which computer is it for?** -- a segmented control of the manifest's `targets`, this machine preselected (os +
   architecture). Another computer: the note says the files are written into the game folder, to copy there and finish
   preparing on that machine; it does not play here. When the bundled runner cannot do a target the note says why in
   Warn and the Prepare button is disabled. The button reads **Prepare**.
4. **Preparing your game** -- the ONE progress bar; above it "Disc: <file> · usually <estimate>"; the caption "NN% ·
   about N minutes left" (or "time left not known yet"); Stop at the bottom left; Details opens the log. On failure the
   bar is replaced by the failure sentence in Error, with **Try again** (Primary) and Details; Back is offered again.
   On success the page advances by itself.
5. **Ready to play** -- Display title, a muted sentence naming the folder, **Play** (Primary, large, 200 wide), "Open
   the game folder", and **Done** at the bottom right. For another computer: "Ready for the other computer", no Play.

The flow is reopened from the pages: a card whose game needs preparing opens it at page 3; "Choose a folder…" opens
page 2; "Prepare for another computer…" (Games) opens page 3.

## 7. States that must exist (and never be silent)

- **Empty** (EmptyState): a 64 px disc glyph, a Title sentence, a Body sentence, one Primary button and an optional
  Quiet one. Games / page 1: "No disc image found — drop yours here" + where was searched + "Choose a disc image…" /
  "Search again". Save States: "No save states yet" + how to make one. Settings and Save States without a game folder:
  "Choose where the game lives first" + "Choose a folder…".
- **Error** (EmptyState in Error tone): a file that is not a disc image ("… cannot be read as a disc image (…)"), a
  disc of another game ("This is not a <name> disc image. "<file>" has no <executable> on it."), a Play that failed.
- **Recognised but not supported**: a card WITH the badge "Not supported yet" and the manifest's reason in Warn
  (another region: `disc.alternates[].reason`; a modified image: `disc.modified_reason`). No action button. Shown after
  the supported cards.
- **Missing**: the disc file is gone -> the card says so in Error and offers Remove.
- **Out of date**: prepared by an older launcher -> Warn status, the action reads Prepare.
- **Preparing failed**: section 6, page 4. **No runner bundled**: the same page with the honest sentence from
  UnavailablePrepareRunner (never a fake success).

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
  git clone, python, compiler, linker, ELF, sha256, json. The prepare step has ONE name: "Preparing your game".
  "Preparing" / "Prepare" / "prepared" are the only verbs for it.
- No placeholder text (lorem, TODO, TBD, placeholder, TextLabel), no emoji.
- Sentences, not fragments; the game is "the game" or its name; the user is "you"; folders are "folders"; the disc
  image is "your disc image" or "the disc". Times are honest: "about 4 minutes left", "under a minute left", "time left
  not known yet" -- never a bar that lies.
- Ellipses only on actions that open a picker ("Choose a disc image…", "Change…", "Forget…").

## 10. Files

- `launcher/title.json` (schema 2) -- the only Sly-shaped input. `tests/app/launcher/test_launcher_screens.cpp` uses a
  neutral "Example Game" manifest so the lint applies to engine strings.
- `<game folder>/prepared.json` -- disc checksum, platform, launcher version, when; decides Play vs Prepare.
- `~/Library/Application Support/<app_data_folder>/launcher.json` (Windows %LOCALAPPDATA%, Linux ~/.local/share) --
  the launcher's own state: the discs, the game folder, sub-folder overrides, the window. `Logs/` beside it.
- `docs/launcher/screenshots/*.png` -- every screen, written by the screens test at 1024 x 640.
