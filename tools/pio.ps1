param([Parameter(ValueFromRemainingArguments=$true)][string[]]$PioArguments)
$projectRoot = Split-Path -Parent $PSScriptRoot
$localPython = Join-Path $projectRoot '.tools\python'
if (Test-Path -LiteralPath $localPython) {
    $env:PYTHONPATH = $localPython
    $env:PLATFORMIO_CORE_DIR = Join-Path $projectRoot '.tools\platformio'
}
if (-not $PioArguments) { $PioArguments = @('run') }
Push-Location -LiteralPath $projectRoot
try {
    & python -m platformio @PioArguments
    $pioExitCode = $LASTEXITCODE
} finally {
    Pop-Location
}
exit $pioExitCode
