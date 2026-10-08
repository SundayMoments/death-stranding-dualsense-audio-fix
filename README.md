# Death Stranding DualSense Audio Fix

Fixes DualSense/BB controller-speaker audio routing in Death Stranding Director's Cut on Windows.

## Install

Download `XAudio2_9.dll` from
[Releases](https://github.com/SundayMoments/death-stranding-dualsense-audio-fix/releases),
copy it next to `ds.exe`, then launch the game normally.

## Uninstall

Delete `XAudio2_9.dll`.

The mod doesn't modify `ds.exe` or any other file on disk. Because it hooks Windows
audio functions and patches the running game's memory, some antivirus programs flag
it. See [SECURITY.md](SECURITY.md) for what it does, why it gets flagged, and how to
check your download.

## Logging

Logging is off by default. To turn it on, open the game's Steam Properties, go to General, and put this in Launch Options:

```text
cmd /c "set DS_DUALSENSE_AUDIO_FIX_LOG=1 && %command%"
```

Audio-buffer inspection and the game-state tracing hooks are only enabled when
logging is enabled. The audio-routing fix remains active with logging disabled.

## Build

Use Windows x64 with MinGW-w64 `g++` and `windres` from the same toolchain:

```powershell
.\build.ps1
```

This builds the player DLL, `outputs/build-info.json` (commit, compiler, dirty-tree
status, DLL SHA-256), and `outputs/SHA256SUMS.txt`. Use
`.\build.ps1 -IncludeDiagnostics` to also build the developer tools. Previously
built diagnostics are not deleted; package only the explicitly listed player files.
CI uploads the player package and developer tools as separate artifacts.

## Release

The version resource in `work/xaudio2_9_proxy.rc` is the release version. To publish:

1. Test in game with logging off and on: controller speaker on/off, switching BB
   Voice Output, an audio device change, and a game restart. Where possible, also
   check the fallback on a system where shared mode fails.
2. Update the version resource and merge to `main`.
3. Tag that commit `v<version>` (for example `v1.0.0`) and push the tag.

CI rebuilds from the tag, checks that the DLL version matches the tag, attests build
provenance, and publishes a GitHub release with the DLL, a zip of the player package,
and `SHA256SUMS.txt`. Builds record their compiler version but are not yet guaranteed
to be byte-for-byte reproducible across toolchains.

If antivirus vendors flag a release, submit that exact file (links in
[SECURITY.md](SECURITY.md)) and keep the submission IDs. A rebuild has a new hash and
doesn't clear detections of the old one.

## License

[GPL-3.0](LICENSE). The vendored [MinHook](work/third_party/minhook) is BSD-2-Clause.

Made by [SundayMoments](https://github.com/SundayMoments) / [SeanConnerX0](https://www.nexusmods.com/profile/SeanConnerX0).

Donations: [ko-fi.com/sundaymoments](https://ko-fi.com/sundaymoments)
