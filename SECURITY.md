# Security policy

## Scope

Sly-2-Recomp is a title module for the NativeEE engine: it is built into a native program that runs
code recompiled from the user's own copy of a PlayStation 2 game. The program reads a disc image the
user supplies, memory-card images under the user's data directory, and the title's own tables. It opens
no network connection and installs nothing.

Vulnerabilities in the engine (the recompiler, the loaders, the ISO9660 reader, the savestate / memory
card code, the GL renderer, the launcher) belong to [NativeEE](https://github.com/menaman123/NativeEE);
report them there. This repository's own attack surface is small: the title tables the binary reads at
start (`hle/sdk_bindings.tsv`, `TITLE_INPUTS.sha256`), the build scripts (`build.sh`, `build.ps1`), the
CI workflows and their helpers (`.github/`, `tools/ci/`).

## Reporting

Report privately through GitHub's "Report a vulnerability" (Security -> Advisories) on this repository,
or to the maintainer listed on the GitHub profile. Include the engine pin (`git submodule status`), the
platform, and a reproduction that uses NO game data: describe the input shape (a crafted ISO9660 image,
a crafted memory-card file, a crafted TSV row) rather than attaching bytes of the game. Expect an
acknowledgement within a week.

## What is NOT a security issue

- The program refusing a disc whose ELF hash differs from `TITLE_INPUTS.sha256`: that is the gate
  working (only the NTSC-U `SCUS_973.16` revision is supported).
- Crashes or hangs *inside the recompiled game code* on game data: those are correctness bugs of the
  port (open a normal issue with the `run.meta` rows).
- Anything that requires the user to run an attacker's build of the binary.

## Supply chain

The repository has one source dependency, the `engine/` submodule, pinned by commit. The engine's
runtime dependencies (SDL2, FFmpeg's LGPL libraries) come from the system package manager or vcpkg, never
from this tree. CI runs on GitHub-hosted runners with `permissions: contents: read`. Only
`windows-release.yml` has `contents: write` and uses secrets (a read-only token for the private repository
holding the two disc files it builds from); it runs on tags and by hand, never on pull requests. The title
lanes that need a disc are gated behind a repository variable, so a fork's CI never expects game data.
