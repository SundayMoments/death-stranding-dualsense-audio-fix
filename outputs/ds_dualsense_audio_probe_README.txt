Death Stranding DualSense controller-audio probe

Purpose:
- This is a read-only diagnostic tool.
- It does not patch ds.exe.
- It does not write to process memory.
- It does not edit ds.exe on disk.

Why:
- The current question is whether Death Stranding is targeting the correct Windows
  render endpoint for DualSense controller-speaker effects.
- Linux/Proton working strongly suggests the audio content/events are valid, and
  the Windows endpoint/device path is the likely failure point.

Recommended run:
1. Connect the DualSense by USB.
2. Start Death Stranding Director's Cut.
3. Get to a point where controller-speaker effects should exist.
4. From PowerShell, run:
   .\ds_dualsense_audio_probe.exe

What to look for:
- The endpoint list should include an active:
  "Speakers (DualSense Wireless Controller)"
- The session list should show whether ds.exe owns an audio session on that
  endpoint, another endpoint, or no active render endpoint.
- Session volume/mute should show volume near 1.000 and muted=no.
- The controller-speaker desired/active ids show Death Stranding's internal
  Wwise output selection state. -1 means no selected/active controller-speaker
  output in that slot.

Commands:
- Full read-only scan:
  .\ds_dualsense_audio_probe.exe

- Windows render endpoints only:
  .\ds_dualsense_audio_probe.exe --endpoints

- ds.exe audio sessions by endpoint:
  .\ds_dualsense_audio_probe.exe --sessions

- ds.exe sessions plus internal controller-speaker ids:
  .\ds_dualsense_audio_probe.exe --diagnose

- ds.exe endpoint peak meters for 15 seconds:
  .\ds_dualsense_audio_probe.exe --monitor 15

- Shared-mode access test for controller/ds.exe endpoints:
  .\ds_dualsense_audio_probe.exe --access-test

Monitor interpretation:
- Run --monitor immediately before triggering an effect that should play through
  the controller speaker.
- If the DualSense endpoint reports "hardware meter support: no" and Death
  Stranding has exclusive control, a 0.0000 meter can be meter blindness rather
  than proof of silence.
- If DualSense moves above 0.0000 but the controller is silent, the problem is
  likely after the app stream reaches Windows, such as endpoint format, device
  driver behavior, controller firmware/transport, or endpoint volume outside the
  app session.

Access-test interpretation:
- AUDCLNT_E_DEVICE_IN_USE on "Speakers (DualSense Wireless Controller)" means
  another client is blocked from opening a shared stream. With ds.exe owning the
  active session, that strongly supports Death Stranding holding the controller
  endpoint in exclusive mode.
- S_OK means a shared stream could be opened at that moment.

Notes:
- If ds.exe appears under a non-DualSense endpoint while controller speaker
  effects should be active, that supports the wrong-endpoint hypothesis.
- If ds.exe does not appear under any endpoint, the game may not be creating the
  controller-speaker stream at all, or it may be using a path not visible through
  the normal audio session enumerator.
- The old experimental Wwise callback/device-id patch path has been removed from
  this deliverable.
