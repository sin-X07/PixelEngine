param([string]$Python = 'python')
$ErrorActionPreference = 'Stop'
$root = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
Push-Location $root
try {
    & $Python (Join-Path $PSScriptRoot 'bake.py')
    if ($LASTEXITCODE -ne 0) { throw 'Map perimeter bake failed' }
    $target = Join-Path $root 'assets/cs2d/perimeters'
    New-Item -ItemType Directory -Force $target | Out-Null
    foreach ($theme in @('ancient_ruins', 'dust_2', 'mirage')) {
        foreach ($part in @('ground', 'scene')) {
            $name = $theme + '_' + $part + '.png'
            Copy-Item -LiteralPath (Join-Path $root ('build-map-perimeters/baked/' + $name)) -Destination (Join-Path $target $name) -Force
        }
    }
} finally {
    Pop-Location
}
