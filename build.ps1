param([switch] $IncludeDiagnostics)

$ErrorActionPreference = "Stop"

$cxx = "g++"
$outDir = Join-Path $PSScriptRoot "outputs"
$workDir = Join-Path $PSScriptRoot "work"
$minHookDir = Join-Path $workDir "third_party\minhook"

New-Item -ItemType Directory -Force -Path $outDir | Out-Null
$buildDir = Join-Path $PSScriptRoot "build"
New-Item -ItemType Directory -Force -Path $buildDir | Out-Null

# Use the resource compiler from the same toolchain as g++.
$cxx = (Get-Command $cxx -ErrorAction Stop).Source
$windres = Join-Path (Split-Path $cxx) "windres.exe"
& $windres "$workDir\xaudio2_9_proxy.rc" -O coff -o "$buildDir\xaudio2_9_proxy.res.o"
if ($LASTEXITCODE -ne 0) { throw "windres failed with exit code $LASTEXITCODE" }

$minHookSources = @(
    "$minHookDir\src\buffer.c",
    "$minHookDir\src\hook.c",
    "$minHookDir\src\trampoline.c",
    "$minHookDir\src\hde\hde32.c",
    "$minHookDir\src\hde\hde64.c"
)

$minHookIncludes = @(
    "-I$minHookDir\include",
    "-I$minHookDir\src"
)

function Invoke-Cxx {
    param([string[]] $CompilerArgs)

    & $cxx @CompilerArgs
    if ($LASTEXITCODE -ne 0) {
        throw "g++ failed with exit code $LASTEXITCODE"
    }
}

Write-Host "Building XAudio2_9.dll proxy..."
# The player DLL uses release defaults regardless of its file name, only
# activates inside ds.exe, and ships without the COFF symbol table.
$compileArgs = @(
    "-std=c++17", "-O2", "-shared", "-static", "-s",
    "-DDS_DUALSENSE_PLAYER_BUILD",
    "-o", "$outDir\XAudio2_9.dll",
    "$workDir\xaudio2_9_proxy.cpp",
    "$workDir\ds_wasapi_trace.cpp",
    "$buildDir\xaudio2_9_proxy.res.o"
) + $minHookSources + $minHookIncludes + @("-lole32", "-lpropsys", "-luuid")
Invoke-Cxx -CompilerArgs $compileArgs

# Keep a record of the source and compiler that produced this exact DLL.
$revision = & git -C $PSScriptRoot rev-parse HEAD
if ($LASTEXITCODE -ne 0) { throw "Cannot determine source revision" }
$sourceStatus = @(& git -C $PSScriptRoot status --porcelain --untracked-files=normal)
if ($LASTEXITCODE -ne 0) { throw "Cannot determine source status" }
$compilerVersion = & $cxx --version
$manifest = [ordered]@{
    sourceCommit = $revision.Trim()
    sourceDirty = $sourceStatus.Count -gt 0
    compiler = $compilerVersion[0]
    file = "XAudio2_9.dll"
    sha256 = (Get-FileHash "$outDir\XAudio2_9.dll" -Algorithm SHA256).Hash.ToLowerInvariant()
}
$manifest | ConvertTo-Json | Set-Content "$outDir\build-info.json" -Encoding UTF8
"$($manifest.sha256)  XAudio2_9.dll" | Set-Content "$outDir\SHA256SUMS.txt" -Encoding ASCII

if (!$IncludeDiagnostics) {
    Write-Host "Proxy build complete. Use -IncludeDiagnostics to build developer tools."
    return
}

Write-Host "Building trace DLL..."
$compileArgs = @(
    "-std=c++17", "-O2", "-shared", "-static",
    "-o", "$outDir\ds_wasapi_trace.dll",
    "$workDir\ds_wasapi_trace.cpp"
) + $minHookSources + $minHookIncludes + @("-lole32", "-lpropsys", "-luuid")
Invoke-Cxx -CompilerArgs $compileArgs

Write-Host "Building standalone shared-fix DLL..."
$compileArgs = @(
    "-std=c++17", "-O2", "-shared", "-static",
    "-o", "$outDir\ds_dualsense_shared_fix.dll",
    "$workDir\ds_wasapi_trace.cpp"
) + $minHookSources + $minHookIncludes + @("-lole32", "-lpropsys", "-luuid")
Invoke-Cxx -CompilerArgs $compileArgs

Write-Host "Building WASAPI trace injector..."
$compileArgs = @(
    "-std=c++17", "-O2", "-static",
    "-o", "$outDir\ds_wasapi_trace_injector.exe",
    "$workDir\ds_wasapi_trace_injector.cpp"
)
Invoke-Cxx -CompilerArgs $compileArgs

Write-Host "Building suspended-process launcher..."
$compileArgs = @(
    "-std=c++17", "-O2", "-static", "-municode",
    "-o", "$outDir\ds_dualsense_shared_fix_launcher.exe",
    "$workDir\ds_dualsense_shared_fix_launcher.cpp"
)
Invoke-Cxx -CompilerArgs $compileArgs

Write-Host "Building read-only diagnostic probe..."
$compileArgs = @(
    "-std=c++17", "-O2", "-static",
    "-o", "$outDir\ds_dualsense_audio_probe.exe",
    "$workDir\ds_dualsense_audio_fix.cpp",
    "-lole32", "-lpropsys", "-luuid"
)
Invoke-Cxx -CompilerArgs $compileArgs

Write-Host "Build complete."
