param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Debug'
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$f103Root = Join-Path $repoRoot 'coprocessor\stm32f103_hmi'

function Invoke-Checked {
    param([scriptblock]$Command, [string]$Description)
    & $Command
    if ($LASTEXITCODE -ne 0) {
        throw "$Description failed with exit code $LASTEXITCODE"
    }
}

Push-Location $repoRoot
try {
    Invoke-Checked { cmake --preset $Configuration } 'H745 configure'
    Invoke-Checked { cmake --build --preset $Configuration --parallel } 'H745 build'
}
finally {
    Pop-Location
}

Push-Location $f103Root
try {
    Invoke-Checked { cmake --preset $Configuration } 'F103 configure'
    Invoke-Checked { cmake --build --preset $Configuration --parallel } 'F103 build'
}
finally {
    Pop-Location
}

Write-Host "All firmware images built successfully ($Configuration)." -ForegroundColor Green
