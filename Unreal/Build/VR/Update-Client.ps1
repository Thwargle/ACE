#requires -Version 5.1
param(
    [Parameter(Mandatory)][string]$Installer,
    [Parameter(Mandatory)][ValidatePattern('^[A-Fa-f0-9]{64}$')][string]$ExpectedSha256,
    [Parameter(Mandatory)][string]$InstallDirectory,
    [Parameter(Mandatory)][int]$GameProcessId,
    [ValidateSet('Desktop','VR')][string]$Mode = 'Desktop',
    [switch]$NoDialogs
)
$ErrorActionPreference = 'Stop'
$logPath = Join-Path $PSScriptRoot 'update-result.txt'
$installed = $false
$stage = 'Validating update'
function Start-UpdateProcess([string]$Path, [string[]]$Arguments, [string]$Directory, [string]$LaunchStage, [bool]$Visible) {
    "$LaunchStage`: $Path" | Add-Content -LiteralPath $logPath
    # Start-Process replaces Win32Exception with a generic error on Windows
    # PowerShell 5.1, losing the native code needed for bounded lock retries.
    $start = New-Object System.Diagnostics.ProcessStartInfo
    $start.FileName = $Path
    $start.Arguments = $Arguments -join ' '
    $start.WorkingDirectory = $Directory
    $start.UseShellExecute = $false
    $start.WindowStyle = if ($Visible) {[System.Diagnostics.ProcessWindowStyle]::Normal} else {[System.Diagnostics.ProcessWindowStyle]::Hidden}
    for ($attempt = 0; ; $attempt++) {
        try {
            return [System.Diagnostics.Process]::Start($start)
        } catch {
            $nativeError = $null
            for ($exception = $_.Exception; $exception; $exception = $exception.InnerException) {
                if ($exception -is [System.ComponentModel.Win32Exception]) { $nativeError = $exception.NativeErrorCode; break }
            }
            # Retry failed launches only. Do not rerun an installer after it starts.
            if ($attempt -lt 16 -and $nativeError -in @(5,32,33)) {
                "$LaunchStage temporarily blocked; retry $($attempt + 1). Windows error $nativeError`: $($_.Exception.Message)" | Add-Content -LiteralPath $logPath
                Start-Sleep -Milliseconds 500
                continue
            }
            throw "Path: $Path`nWindows error $nativeError`: $($_.Exception.Message)"
        }
    }
}
try {
    'Starting Windows update.' | Set-Content -LiteralPath $logPath
    $payload = (Resolve-Path -LiteralPath $Installer).Path
    $installRoot = (Resolve-Path -LiteralPath $InstallDirectory).Path.TrimEnd('\','/')
    "Installer: $payload`nInstall directory: $installRoot" | Add-Content -LiteralPath $logPath
    $gameExe = Join-Path $installRoot 'ACUnreal\Binaries\Win64\ACUnreal.exe'
    if (!(Test-Path -LiteralPath $gameExe) -or !(Test-Path -LiteralPath (Join-Path $installRoot 'ACUnreal.exe'))) {
        throw 'The selected game folder is incomplete. Please use the website installer.'
    }
    # Recheck after leaving the game, before executing any downloaded bytes.
    if ((Get-FileHash -LiteralPath $payload -Algorithm SHA256).Hash -ne $ExpectedSha256) {
        throw 'The update failed verification. Reopen the game and download it again.'
    }
    $stage = 'Waiting for game shutdown'
    $stage | Add-Content -LiteralPath $logPath
    $gameProcess = Get-Process -Id $GameProcessId -ErrorAction SilentlyContinue
    if ($gameProcess -and !$gameProcess.WaitForExit(120000)) {
        throw 'The game did not close. Close it and retry the update.'
    }
    if (Get-Process ACUnreal -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq $gameExe }) {
        throw 'Another copy of this client is running. Close it before installing the update.'
    }
    $portable = !(Test-Path -LiteralPath (Join-Path $installRoot 'unins000.exe'))
    $arguments = @('/SP-', '/SILENT', '/NORESTART', '/NOCLOSEAPPLICATIONS', '/NORESTARTAPPLICATIONS',
        ('/DIR="' + $installRoot + '"'), ('/LOG="' + (Join-Path $PSScriptRoot 'installer.log') + '"'))
    if ($portable) { $arguments += '/ACPORTABLE=1' }
    $stage = 'Starting installer'
    $setup = Start-UpdateProcess $payload $arguments $PSScriptRoot $stage $false
    $stage = 'Installing update'
    $setup.WaitForExit()
    "Installer exit code: $($setup.ExitCode)" | Add-Content -LiteralPath $logPath
    if ($setup.ExitCode -ne 0) { throw "The installer returned code $($setup.ExitCode). See installer.log in this folder." }
    $installed = $true
    'Update installed successfully.' | Add-Content -LiteralPath $logPath
    $gameArgs = if ($Mode -eq 'VR') { '-vr' } else { '-nohmd' }
    # The updater is hidden, but the restarted game is an interactive window.
    # SW_HIDE is inherited by Unreal's first ShowWindow and leaves a live,
    # rendering client with no visible window after an otherwise successful update.
    $stage = 'Restarting updated game'
    $restarted = Start-UpdateProcess $gameExe @($gameArgs) $installRoot $stage $true
    "Restarted $Mode client (PID $($restarted.Id)) with a visible window." | Add-Content -LiteralPath $logPath
} catch {
    $prefix = if ($installed) { 'The update installed successfully, but the game could not reopen. Open the game from your normal shortcut.' }
        else { 'The update could not finish. Use the latest installer from thwargle.com if retrying fails.' }
    $message = "$prefix`n`n$stage failed. $($_.Exception.Message)`n`nIf Windows denies access, check folder permissions and Windows Security Protection history.`nDetails: $logPath"
    # Preserve the installation result even if only the restart failed.
    try { $message | Add-Content -LiteralPath $logPath } catch { }
    if (!$NoDialogs) {
        Add-Type -AssemblyName System.Windows.Forms
        $title = if ($installed) {'AC updated; restart needed'} else {'AC update could not finish'}
        [System.Windows.Forms.MessageBox]::Show($message, $title, 'OK', 'Warning') | Out-Null
    }
    exit 1
}
