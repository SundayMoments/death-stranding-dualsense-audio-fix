# Security and release notes

## What the player DLL does

`XAudio2_9.dll` is a user-installed proxy placed next to `ds.exe`. It forwards
XAudio2 exports to the Windows system DLL using a path from `GetSystemDirectoryW`.
It uses MinHook to intercept audio initialization in the process that loads it,
tries shared mode for controller audio, and falls back to the original exclusive
initialization if shared mode fails. It also patches a known controller-speaker
instruction sequence in game memory after checking expected bytes.

The player DLL has no source-level network client, updater, persistence installer,
or code for injecting into another process. It only activates when the host process
is `ds.exe`; loaded by any other process, it just forwards XAudio2 calls to the system
DLL. Install it only in the supported game's directory. The hard-coded game offsets
require compatibility testing after game updates. These statements describe the
reviewed source; they are not a guarantee about every file with this filename.

MinHook allocates executable memory, changes code-page permissions, and suspends
and adjusts threads in the current process when installing hooks. The player DLL
still needs these operations. The diagnostic injector and suspended-process launcher
are separate developer tools and are not part of the player package.

With logging disabled, the player DLL does not install diagnostic render-buffer,
GetService, CoCreateInstance, or game-state tracing detours, and it writes no files.
Logging can be explicitly enabled using `DS_DUALSENSE_AUDIO_FIX_LOG=1`. The routing
hooks and instruction patch remain active. These defaults do not depend on the DLL's
file name. Release builds omit the COFF symbol table; this repository's source is the
reference for review.

## Antivirus review

The proxy and memory-hooking behavior can resemble techniques used by malware.
This is a plausible reason for heuristic detections, not confirmation of any
vendor's detection rationale. Do not assume all alerts are false positives.

For a suspected false positive, retain the exact flagged DLL and archive. Record
their SHA-256 values, product version, download location, detection names, and
analysis dates. Ask each detecting vendor to review the actual flagged sample;
rebuilding creates a different hash and does not clear the previous release.

- [Microsoft software-developer submission](https://www.microsoft.com/en-us/wdsi/filesubmission)
- [Avast false-positive submission](https://www.avast.com/report-false-positive)
- [VirusTotal guidance and vendor contacts](https://docs.virustotal.com/docs/false-positive)

Source access helps a vendor review the behavior. Link the tagged source for the
flagged release along with its GitHub release page and build attestation. Logs written
with logging enabled include audio endpoint names and IDs from that PC; review them
before attaching them to a public issue.

## Publishing a release

1. Test controller-speaker enable/disable, a device change, and game restart with
   logging off and on. Confirm fallback behavior on a system where shared mode fails.
2. Push a `v<version>` tag matching the DLL version resource. CI builds from that
   clean commit, records the compiler version, and attests build provenance.
3. The release contains only the player DLL, its README, the project and MinHook
   licenses, build metadata, and checksum file. Diagnostic tools stay in a separate
   CI artifact.
4. If a trusted publisher certificate is available, Authenticode-sign and timestamp
   the final DLL. Update the manifest and checksum after signing. Version resources
   identify the product but do not authenticate it. Signing does not guarantee that
   antivirus products will consider the file clean.
5. Obtain vendor review for flagged release files and retain submission IDs. Recheck
   the exact hashes after vendors respond. Do not claim that source cleanup alone
   cleared detections, or recommend disabling antivirus protection.

Build metadata and GitHub attestations (`gh attestation verify XAudio2_9.dll --repo
<owner>/<repo>`) tie a release file to the commit and workflow run that built it.
They do not prove reproducible builds; the CI toolchain currently updates over time.
