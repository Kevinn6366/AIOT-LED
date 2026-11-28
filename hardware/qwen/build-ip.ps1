param([string]$VivadoBin = 'D:/vivado/Vivado/2019.1/bin')
$ErrorActionPreference = 'Stop'
Set-Location (Join-Path $PSScriptRoot '../..')
& "$VivadoBin/vivado_hls.bat" -f hardware/qwen/hls.tcl -l build/qwen-hls.log
$ip = 'build/qwen_all_hls/sol/impl/ip'
$log = Join-Path $ip 'autoimpl.log'
# The 2019.1 packager encodes dates beyond 2021 as an overflowing 32-bit revision.
if ((Test-Path $log) -and (Select-String $log -Quiet -Pattern 'bad lexical cast') -and
    (Select-String $log -Quiet -Pattern 'set_property core_revision')) {
    Push-Location $ip
    try {
        $source = Get-Content run_ippack.tcl -Raw
        $source -replace 'set Revision\s+"[0-9]+"','set Revision 1' |
            Set-Content run_ippack.tcl -Encoding ascii
        & ./pack.bat
        if ($LASTEXITCODE) {throw 'Vivado IP packaging failed'}
    } finally {Pop-Location}
}
if (!(Test-Path "$ip/component.xml")) {throw 'No packaged Q projection IP'}
