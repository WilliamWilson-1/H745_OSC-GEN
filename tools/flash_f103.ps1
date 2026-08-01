param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Debug',
    [string]$ProbeSerial = ''
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$image = Join-Path $repoRoot "coprocessor\stm32f103_hmi\build\$Configuration\stm32f103_hmi.elf"
$programmer = (Get-Command STM32_Programmer_CLI -ErrorAction Stop).Source

if (-not (Test-Path -LiteralPath $image)) {
    throw "Missing image: $image. Run tools\build_all.ps1 -Configuration $Configuration first."
}

$connection = @('-c', 'port=SWD', 'mode=UR', 'reset=HWrst')
if ($ProbeSerial) { $connection += "sn=$ProbeSerial" }

Write-Warning 'Verify that this ST-Link is physically connected to the STM32F103, not the H745.'
& $programmer @connection '-w' $image '-v' '-rst'
if ($LASTEXITCODE -ne 0) { throw "F103 programming failed with exit code $LASTEXITCODE" }

Write-Host 'F103 programming and verification completed.' -ForegroundColor Green
