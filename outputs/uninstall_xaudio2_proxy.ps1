$ErrorActionPreference = "Stop"

$gameDir = "C:\Program Files (x86)\Steam\steamapps\common\DEATH STRANDING DIRECTORS CUT"
$source = Join-Path $PSScriptRoot "XAudio2_9.dll"
$dest = Join-Path $gameDir "XAudio2_9.dll"

if (Get-Process ds -ErrorAction SilentlyContinue) {
    throw "Close Death Stranding before uninstalling the proxy."
}

$backups = @(Get-ChildItem -LiteralPath $gameDir -Filter "XAudio2_9.dll.codex_backup_*" -ErrorAction SilentlyContinue |
    Sort-Object LastWriteTime -Descending)

if ($backups.Count -gt 0) {
    if (Test-Path -LiteralPath $dest) {
        $removed = Join-Path $gameDir ("XAudio2_9.dll.codex_removed_{0:yyyyMMdd_HHmmss}" -f (Get-Date))
        Move-Item -LiteralPath $dest -Destination $removed
        Write-Host "Moved installed proxy aside:"
        Write-Host "  $removed"
    }
    Move-Item -LiteralPath $backups[0].FullName -Destination $dest
    Write-Host "Restored backup:"
    Write-Host "  $dest"
    exit 0
}

if (!(Test-Path -LiteralPath $dest)) {
    Write-Host "No local XAudio2_9.dll proxy is installed."
    exit 0
}

if (Test-Path -LiteralPath $source) {
    $srcHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $source).Hash
    $dstHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $dest).Hash
    if ($srcHash -eq $dstHash) {
        Remove-Item -LiteralPath $dest
        Write-Host "Removed installed XAudio2_9.dll proxy:"
        Write-Host "  $dest"
        exit 0
    }
}

throw "Refusing to remove $dest because it does not match this proxy and no backup was found."
