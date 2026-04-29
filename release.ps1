param([Parameter(Mandatory)][string]$Version)
$exe = "cmake-build-release\HxEditer-$Version-win64.exe"
if (-not (Test-Path $exe)) {
    Write-Error "Build $exe first -- run 'cmake --build cmake-build-release --target package'"
    return
}

$hash = (Get-FileHash $exe -Algorithm SHA256).Hash.ToLower()

# UTF-8 without BOM keeps the hash file readable on every shasum tool and
# preserves any non-ASCII filename if one ever shows up.
$line = "$hash  HxEditer-$Version-win64.exe"
[System.IO.File]::WriteAllText(
    (Join-Path (Get-Location) "SHA256SUMS.txt"),
    $line + "`n",
    (New-Object System.Text.UTF8Encoding $false))

# Surface the commit so a stale cmake-build-release/ doesn't quietly ship
# yesterday's binary tagged as today's release.
$head = (git rev-parse HEAD 2>$null)
$dirty = ""
if ($head -and (git status --porcelain 2>$null)) { $dirty = " (dirty)" }

Write-Host "Upload to a new v$Version GitHub release:"
Write-Host "  $exe"
Write-Host "  SHA256SUMS.txt  =  $hash"
if ($head) {
    Write-Host "  built from      =  $head$dirty"
} else {
    Write-Host "  built from      =  (git rev-parse HEAD failed)"
}
