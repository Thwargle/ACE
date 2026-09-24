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
try {
    $payload = (Resolve-Path -LiteralPath $Installer).Path
    $installRoot = (Resolve-Path -LiteralPath $InstallDirectory).Path.TrimEnd('\','/')
    $gameExe = Join-Path $installRoot 'ACUnreal\Binaries\Win64\ACUnreal.exe'
    if (!(Test-Path -LiteralPath $gameExe) -or !(Test-Path -LiteralPath (Join-Path $installRoot 'ACUnreal.exe'))) {
        throw 'The selected game folder is incomplete. Please use the website installer.'
    }
    # Recheck after leaving the game, before executing any downloaded bytes.
    if ((Get-FileHash -LiteralPath $payload -Algorithm SHA256).Hash -ne $ExpectedSha256) {
        throw 'The update failed verification. Reopen the game and download it again.'
    }
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
    $setup = Start-Process -FilePath $payload -ArgumentList $arguments -Wait -PassThru -WindowStyle Hidden
    if ($setup.ExitCode -ne 0) { throw "The installer returned code $($setup.ExitCode). See installer.log in this folder." }
    'Update installed successfully.' | Set-Content -LiteralPath $logPath
    $gameArgs = if ($Mode -eq 'VR') { '-vr' } else { '-nohmd' }
    Start-Process -FilePath $gameExe -ArgumentList $gameArgs -WorkingDirectory $installRoot -WindowStyle Hidden
} catch {
    $message = $_.Exception.Message
    $message | Set-Content -LiteralPath $logPath
    if (!$NoDialogs) {
        Add-Type -AssemblyName System.Windows.Forms
        [System.Windows.Forms.MessageBox]::Show($message, 'AC update could not finish', 'OK', 'Warning') | Out-Null
    }
    exit 1
}
