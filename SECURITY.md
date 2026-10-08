# Security

## What the mod does

`XAudio2_9.dll` goes next to `ds.exe`, and the game loads it in place of the Windows
XAudio2 DLL. It passes every XAudio2 call through to the real Windows DLL. While
Death Stranding is running, it also:

- Opens the DualSense speaker in shared mode instead of exclusive mode, so BB and
  controller-speaker audio can reach it. If shared mode fails, the game's original
  behavior is used.
- Patches one instruction sequence in the game's memory so a new BB Voice Output
  device takes effect right away. The original bytes are checked first; if they
  don't match (for example after a game update), the patch is skipped.

It does not:

- Modify `ds.exe` or any other file on disk.
- Connect to the internet, update itself, or run at startup.
- Do anything except pass XAudio2 calls through when loaded by a program other
  than `ds.exe`.
- Write any files unless you turn on logging.

## Why antivirus may flag it

To do its job, the mod uses techniques that malware also uses: it has the same name
as a Windows system DLL, it hooks Windows audio functions with
[MinHook](https://github.com/TsudaKageyu/minhook), and it changes code in the running
game's memory. Heuristic and machine-learning scanners often flag that combination
with generic names such as `Wacatac.B!ml`, `MalwareX-gen`, or `Agent`.

That is common for game mods, but don't assume every alert is a false positive. Only
use a copy downloaded from the
[Releases page](https://github.com/SundayMoments/death-stranding-dualsense-audio-fix/releases),
and check it as described below. The full source is in this repository.

## Check your download

Each release lists the SHA-256 checksums of its files. To get the checksum of your
copy, run this in PowerShell in the folder that contains the DLL:

```powershell
Get-FileHash XAudio2_9.dll -Algorithm SHA256
```

It should match the checksum in the release notes. Release files are built by
GitHub Actions directly from the tagged source. If you have the
[GitHub CLI](https://cli.github.com/), you can confirm that:

```powershell
gh attestation verify XAudio2_9.dll --repo SundayMoments/death-stranding-dualsense-audio-fix
```

## If your antivirus flags it

1. Check the checksum as described above.
2. If it matches, report the file to your antivirus vendor as a false positive.
   [Microsoft Defender](https://www.microsoft.com/en-us/wdsi/filesubmission) and
   [Avast/AVG](https://www.avast.com/report-false-positive) accept submissions, and
   VirusTotal keeps a [list of vendor contacts](https://docs.virustotal.com/docs/false-positive).
3. You can also open an issue with the detection name and the file's checksum.

Please don't turn off your antivirus to use the mod.

## Logs

Logging is off by default. If you turn it on (see the [README](README.md)), the log
is written to the game folder and includes the names and IDs of your audio devices.
Remove those before posting a log publicly.

## Reporting a security problem

Please report security problems privately from this repository's **Security** tab
(**Report a vulnerability**) instead of opening a public issue.
