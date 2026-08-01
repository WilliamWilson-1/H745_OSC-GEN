param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Debug',
    [string]$ProbeSerial = ''
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$cm4Image = Join-Path $repoRoot "CM4\build\$Configuration\NUCLEO_1_CM4.elf"
$cm7Image = Join-Path $repoRoot "CM7\build\$Configuration\NUCLEO_1_CM7.elf"
$programmer = (Get-Command STM32_Programmer_CLI -ErrorAction Stop).Source

foreach ($image in @($cm4Image, $cm7Image)) {
    if (-not (Test-Path -LiteralPath $image)) {
        throw "Missing image: $image. Run tools\build_all.ps1 -Configuration $Configuration first."
    }
}

$connection = @('-c', 'port=SWD', 'mode=UR', 'reset=HWrst')
if ($ProbeSerial) { $connection += "sn=$ProbeSerial" }

Write-Host 'Programming Cortex-M4 image at 0x08100000...'
& $programmer @connection '-w' $cm4Image '-v'
if ($LASTEXITCODE -ne 0) { throw "CM4 programming failed with exit code $LASTEXITCODE" }

Write-Host 'Programming Cortex-M7 image at 0x08000000 and resetting target...'
& $programmer @connection '-w' $cm7Image '-v' '-rst'
if ($LASTEXITCODE -ne 0) { throw "CM7 programming failed with exit code $LASTEXITCODE" }

Write-Host 'H745 CM4 + CM7 programming and verification completed.' -ForegroundColor Green
