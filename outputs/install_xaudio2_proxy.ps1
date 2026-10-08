$ErrorActionPreference = "Stop"

$gameDir = "C:\Program Files (x86)\Steam\steamapps\common\DEATH STRANDING DIRECTORS CUT"
$source = Join-Path $PSScriptRoot "XAudio2_9.dll"
$dest = Join-Path $gameDir "XAudio2_9.dll"

if (Get-Process ds -ErrorAction SilentlyContinue) {
    throw "Close Death Stranding before installing the proxy."
}

if (!(Test-Path -LiteralPath $source)) {
    throw "Missing source proxy: $source"
}

if (!(Test-Path -LiteralPath $gameDir)) {
    throw "Game directory not found: $gameDir"
}

if (Test-Path -LiteralPath $dest) {
    $srcHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $source).Hash
    $dstHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $dest).Hash
    if ($srcHash -eq $dstHash) {
        Write-Host "Proxy is already installed:"
        Write-Host "  $dest"
        exit 0
    }

    $backup = Join-Path $gameDir ("XAudio2_9.dll.codex_backup_{0:yyyyMMdd_HHmmss}" -f (Get-Date))
    Move-Item -LiteralPath $dest -Destination $backup
    Write-Host "Backed up existing local XAudio2_9.dll:"
    Write-Host "  $backup"
}

Copy-Item -LiteralPath $source -Destination $dest
Write-Host "Installed XAudio2_9.dll proxy:"
Write-Host "  $dest"
Write-Host "Launch Death Stranding normally through Steam."
