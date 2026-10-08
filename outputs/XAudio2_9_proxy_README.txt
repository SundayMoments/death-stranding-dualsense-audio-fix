Death Stranding DualSense Audio Fix

Fixes DualSense/BB controller-speaker audio routing in Death Stranding Director's Cut on Windows.

Install:
Copy XAudio2_9.dll next to ds.exe, then launch the game normally.

Uninstall:
Delete XAudio2_9.dll.

This does not modify ds.exe.

The executable on disk stays unchanged. The mod hooks Windows audio functions
and patches game memory while running. This can trigger antivirus heuristics;
suspected false positives need review by the detecting vendor.

Build metadata and the DLL SHA-256 are provided in build-info.json and
SHA256SUMS.txt. Product metadata is not a digital signature.

Logging is off by default. To turn it on, open the game's Steam Properties, go to General, and put this in Launch Options:
cmd /c "set DS_DUALSENSE_AUDIO_FIX_LOG=1 && %command%"

With logging disabled, diagnostic audio-buffer and game-state tracing hooks are
not installed. The controller audio fix remains active.

Source code (GPL-3.0, see LICENSE.txt; MinHook is BSD-2-Clause):
https://github.com/SundayMoments/death-stranding-dualsense-audio-fix

Made by SundayMoments / SeanConnerX0:
https://github.com/SundayMoments
https://www.nexusmods.com/profile/SeanConnerX0

Donations:
https://ko-fi.com/sundaymoments
