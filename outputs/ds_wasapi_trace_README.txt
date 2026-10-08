Death Stranding WASAPI/controller-speaker trace

Purpose:
- This is the next diagnostic after proving Death Stranding owns the DualSense
  endpoint exclusively.
- It injects a DLL into ds.exe and hooks WASAPI COM methods inside the process.
- It logs endpoint activation, IAudioClient::Initialize, render format, and
  per-channel render-buffer peaks from IAudioRenderClient::ReleaseBuffer.

Files:
- ds_wasapi_trace_injector.exe
- ds_wasapi_trace.dll
- ds_wasapi_trace.log is created next to the DLL after injection.

How to capture:
1. Start Death Stranding Director's Cut.
2. Connect the DualSense and enable controller speaker effects in-game.
3. From PowerShell in this folder, run:
   .\ds_wasapi_trace_injector.exe
4. In-game, toggle Controller Speaker off/on, reload a save, or otherwise force
   the controller-speaker stream to be recreated.
5. Trigger BB/controller-speaker audio.
6. Open ds_wasapi_trace.log.

What to look for:
- IAudioClient::Initialize on "Speakers (DualSense Wireless Controller)".
- share=exclusive versus share=shared.
- format fields: sample rate, channel count, bits, sample type, channel mask.
- render summary lines with controller=yes.

Interpretation:
- controller=yes with all channel peaks at 0.00000 means the controller bus is
  being opened but is receiving silence, or ReleaseBuffer is using the silent
  flag.
- nonzero peaks only on unexpected channels suggests a channel mapping/layout
  bug against the 4-channel DualSense endpoint.
- nonzero peaks on expected channels but no sound suggests a driver/controller
  transport or exclusive-format compatibility problem after the app writes data.
- no controller Initialize/GetService lines after injection usually means the
  stream already existed before injection. Toggle Controller Speaker, reload, or
  restart the game and inject earlier.

Safety:
- This does not edit ds.exe on disk.
- It does patch process code/vtables in memory for tracing, so it is more
  invasive than ds_dualsense_audio_probe.exe.
- Restarting the game removes the tracer.
