Death Stranding DualSense shared-endpoint fix experiment

What it tries:
- Death Stranding appears to open the DualSense endpoint exclusively for one
  controller-audio path.
- BB Voice Output then tries to open the same DualSense endpoint as a normal
  shared stream and fails with AUDCLNT_E_DEVICE_IN_USE.
- This shim changes DualSense-looking exclusive IAudioClient::Initialize calls
  to shared mode before the endpoint is monopolized.

Files:
- ds_dualsense_shared_fix_launcher.exe
- ds_dualsense_shared_fix.dll
- ds_dualsense_shared_fix.log is written next to the DLL.

How to test:
1. Fully close Death Stranding.
2. Leave Steam open.
3. From PowerShell in this folder, run:
   .\ds_dualsense_shared_fix_launcher.exe
4. In-game, set BB Voice Output to "Speakers (DualSense Wireless Controller)".
5. Reload or trigger BB audio.
6. Run:
   .\ds_dualsense_audio_probe.exe --access-test

Success signs:
- BB audio plays from the DualSense controller.
- The access test no longer reports AUDCLNT_E_DEVICE_IN_USE for the DualSense
  endpoint.
- ds_dualsense_shared_fix.log contains:
  forceDualSenseShared=yes
  FORCE_SHARED attempting
  calledShare=shared
  hr=0x00000000

If the launcher cannot start the Steam build directly:
- Start the game normally, then immediately run:
  .\ds_wasapi_trace_injector.exe 0 .\ds_dualsense_shared_fix.dll
- This is less reliable because the exclusive stream may already exist before
  injection.

Safety:
- This does not edit ds.exe on disk.
- It patches process memory through MinHook while the game is running.
- Restarting the game removes the shim.
- This is an experiment; if audio breaks or the game crashes, close the game and
  launch normally.
