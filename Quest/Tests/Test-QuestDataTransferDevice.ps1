#requires -Version 5.1
param(
    [Parameter(Mandatory)][string]$AdbPath,
    [Parameter(Mandatory)][string]$Serial,
    [string]$Source
)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
. (Join-Path $root 'Sharing/Quest-DataTransfer.ps1')
$AdbPath=(Resolve-Path -LiteralPath $AdbPath).Path
$fixture=Join-Path $root ('Logs/DataTransferDevice-'+[guid]::NewGuid().ToString('N')+"/Friend's Quest")
New-Item -ItemType Directory -Path $fixture -Force | Out-Null
$generatedSample = !$Source
if ($generatedSample) {
$source=Join-Path $fixture 'binary sample.dat'
$block=New-Object byte[] (1024*1024)
for($i=0; $i -lt $block.Length; $i++){ $block[$i]=[byte]($i % 256) }
$stream=[IO.File]::Create($source)
try { for($i=0; $i -lt 32; $i++){ $stream.Write($block,0,$block.Length) } }
finally { $stream.Dispose() }
}
$package='com.acecommunity.questtest'
$deviceArgs=@('-s',$Serial)
function Invoke-Adb {
 param([string[]]$Arguments)
 $ErrorActionPreference='Continue'
 $output=@(& $AdbPath @Arguments 2>&1)
 if($LASTEXITCODE){throw "ADB failed: $($output -join ' ')"}
 $output|ForEach-Object {[string]$_}
}
# Only a uniquely named probe is written; installed DATs and player data are
# untouched, and the game does not need to be stopped for this regression.
$remote='files/ace-transfer-probe-'+[guid]::NewGuid().ToString('N')+'.bin'
try {
    Send-QuestDataFile -Source $source -Destination $remote
    $size=(& $AdbPath -s $Serial shell run-as $package stat -c '%s' $remote | Out-String).Trim()
    if($LASTEXITCODE -ne 0){throw 'Device size verification failed'}
    $hash=((& $AdbPath -s $Serial shell run-as $package sha256sum $remote | Out-String).Trim() -split '\s+')[0]
    if($LASTEXITCODE -ne 0){throw 'Device hash verification failed'}
    $local=(Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash
    if($size -ne [string](Get-Item -LiteralPath $source).Length -or $hash -ne $local){throw 'Device binary transfer changed or truncated bytes'}
    [ordered]@{status='passed';bytes=[long]$size;sha256=$hash;powerShell=$PSVersionTable.PSVersion.ToString();transport='push-and-app-copy';allByteValues=$generatedSample;appPrivateStorage=$true} |
        ConvertTo-Json | Set-Content -LiteralPath (Join-Path $fixture 'result.json') -Encoding UTF8
    Write-Output "PASS: $size bytes binary transfer into Quest app storage, exact SHA256 match. Evidence: $fixture"
} finally {
    # Exact test-owned filename, no recursion or wildcard removal on the device.
    & $AdbPath -s $Serial shell run-as $package rm -f $remote | Out-Null
    if($LASTEXITCODE -ne 0){Write-Warning "Could not remove temporary transfer probe: $remote"}
}
