Death Stranding DualSense Audio Fix

Fixes DualSense/BB controller-speaker audio routing in Death Stranding Director's Cut on Windows.

Install:
Copy XAudio2_9.dll next to ds.exe, then launch the game normally.

Uninstall:
Delete XAudio2_9.dll.

The mod doesn't modify ds.exe or any other file on disk. Because it hooks Windows
audio functions and patches the running game's memory, some antivirus programs
flag it. SECURITY.md in the source repository explains why and how to check your
download. SHA256SUMS.txt lists the DLL's checksum.

Logging is off by default. To turn it on, open the game's Steam Properties, go to General, and put this in Launch Options:
cmd /c "set DS_DUALSENSE_AUDIO_FIX_LOG=1 && %command%"

The controller audio fix stays active with logging off.

Source code (GPL-3.0, see LICENSE.txt; MinHook is BSD-2-Clause):
https://github.com/SundayMoments/death-stranding-dualsense-audio-fix

Made by SundayMoments / SeanConnerX0:
https://github.com/SundayMoments
https://www.nexusmods.com/profile/SeanConnerX0

Donations:
https://ko-fi.com/sundaymoments
