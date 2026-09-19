#requires -Version 7.0
param()
$ErrorActionPreference = 'Stop'
$serverDirectory = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\Unreal\PackagedVR\Server'))
$serverExe = Join-Path $serverDirectory 'ACE.Server.exe'
if (!(Test-Path -LiteralPath $serverExe)) { throw "Server executable not found: $serverExe" }
$config = Get-Content -LiteralPath (Join-Path $serverDirectory 'Config.js') -Raw | ConvertFrom-Json
$port = [int]$config.Server.Network.Port
if ($port -lt 1 -or $port -gt 65534) { throw 'Invalid server UDP port in Config.js.' }

# Serialize launches through this helper and check actual OS listeners too.
# ACE's reusable UDP sockets otherwise permit two servers on the same ports.
$startupMutex = [Threading.Mutex]::new($false, 'Local\ACVR_ServerStart')
$locked = $false
try {
    try { $locked = $startupMutex.WaitOne(10000) }
    catch [Threading.AbandonedMutexException] { $locked = $true }
    if (!$locked) { throw 'Another server launch is in progress.' }

    $existing = @(Get-CimInstance Win32_Process -Filter "name='ACE.Server.exe'" |
        Where-Object { $_.ExecutablePath -eq $serverExe })
    if ($existing.Count -gt 1) {
        throw "Multiple copies of this server are running (PIDs $($existing.ProcessId -join ', ')). Close duplicates before retrying."
    }
    $listeners = @(Get-NetUDPEndpoint | Where-Object { $_.LocalPort -in $port,($port + 1) })
    if ($existing.Count -eq 1) {
        $serverProcessId = $existing[0].ProcessId
        $conflicts = @($listeners | Where-Object OwningProcess -ne $serverProcessId)
        if ($conflicts.Count) { throw 'Another process is using an ACE UDP port. A second server was not started.' }
        Write-Host "ACE server is already running (PID $serverProcessId). A second copy was not started."
        return
    }
    if ($listeners.Count) {
        throw "UDP $port or $($port + 1) is already occupied by PID(s) $($listeners.OwningProcess -join ', '). A second server was not started."
    }

    $started = Start-Process -FilePath $serverExe -WorkingDirectory $serverDirectory -WindowStyle Hidden -PassThru
    Write-Host "Starting ACE server (PID $($started.Id)), UDP $port/$($port + 1)."
    Write-Host "Startup log: $(Join-Path $serverDirectory 'ACE_Log.txt')"
}
finally {
    if ($locked) { $startupMutex.ReleaseMutex() }
    $startupMutex.Dispose()
}
