$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$env:PYTHONPATH = Join-Path $repo 'build/host-python'
$url = 'http://127.0.0.1:8089/'
try {
    $running = (Invoke-RestMethod ($url + 'status') -TimeoutSec 4).port -like 'COM3*'
} catch {
    $running = $false
}
if (-not $running) {
    $python = (Get-Command python.exe).Source
    & $python -c 'import serial'
    if ($LASTEXITCODE) { throw '请先执行 python -m pip install --target build/host-python pyserial==3.5' }
    Start-Process $python -ArgumentList 'host/boot_status.py' -WorkingDirectory $repo -WindowStyle Hidden
    Start-Sleep -Milliseconds 800
}
Start-Process (Join-Path ${env:ProgramFiles(x86)} 'Microsoft/Edge/Application/msedge.exe') -ArgumentList $url
