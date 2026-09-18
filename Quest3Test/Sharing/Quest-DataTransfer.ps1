#requires -Version 5.1
# Shared by the developer and friend installers. Invoke-Adb, Wait-QuestConnection,
# $AdbPath, $deviceArgs, $Serial, and $package are supplied by the installer.
function ConvertTo-QuestNativeArgument {
    param([string]$Value)
    if ($Value -and $Value -notmatch '[\s"]') { return $Value }
    $escaped = [regex]::Replace($Value, '(\\*)"', '$1$1\"')
    $escaped = [regex]::Replace($escaped, '(\\+)$', '$1$1')
    return '"' + $escaped + '"'
}

function Invoke-AdbFileInput {
    param([string]$Source, [string[]]$Arguments)
    # Do not pipe DAT bytes through PowerShell: Windows PowerShell 5.1 decodes
    # native pipelines as text. A no-PTY Android shell receives the raw stream.
    $process = New-Object System.Diagnostics.Process
    $process.StartInfo.FileName = $AdbPath
    $process.StartInfo.Arguments = ($Arguments | ForEach-Object { ConvertTo-QuestNativeArgument $_ }) -join ' '
    $process.StartInfo.UseShellExecute = $false
    $process.StartInfo.CreateNoWindow = $true
    $process.StartInfo.RedirectStandardInput = $true
    $process.StartInfo.RedirectStandardOutput = $true
    $process.StartInfo.RedirectStandardError = $true
    $file = $null
    $started = $false
    try {
        if (!$process.Start()) { throw 'Could not start ADB for data transfer.' }
        $started = $true
        $stdout = $process.StandardOutput.ReadToEndAsync()
        $stderr = $process.StandardError.ReadToEndAsync()
        $file = [System.IO.File]::OpenRead($Source)
        $copyError = $null
        try {
            $copy = $file.CopyToAsync($process.StandardInput.BaseStream)
            if (!$copy.Wait([TimeSpan]::FromMinutes(10))) {
                $process.Kill()
                throw 'Data transfer timed out after ten minutes.'
            }
        } catch { $copyError = $_.Exception.Message }
        $process.StandardInput.Close()
        if (!$process.WaitForExit(30000)) {
            $process.Kill()
            $process.WaitForExit()
            throw 'ADB did not finish the data transfer. Reconnect the headset and retry.'
        }
        $output = $stdout.Result + $stderr.Result
        if ($process.ExitCode -ne 0 -or $copyError) {
            throw "ADB data transfer failed (exit $($process.ExitCode)): $copyError $output"
        }
        if ($output.Trim()) { Write-Host $output.Trim() }
    } finally {
        if ($file) { $file.Dispose() }
        if ($started -and !$process.HasExited) { $process.Kill() }
        $process.Dispose()
    }
}

function Install-QuestData {
    param([string]$DatDirectory, [string]$DiagnosticDirectory)
    $saved = 'files/ACEViewer/Saved/DAT'
    $required = @('client_portal.dat', 'client_cell_1.dat', 'client_local_English.dat')
    foreach ($name in $required) {
        $source = Join-Path $DatDirectory $name
        if (!(Test-Path -LiteralPath $source -PathType Leaf) -or (Get-Item -LiteralPath $source).Length -eq 0) {
            throw "Missing or empty required data file: $source"
        }
    }
    # Close the game before replacing databases it may have open. This preserves
    # settings and logins, and the installer only relaunches after verification.
    Invoke-Adb -Arguments ($deviceArgs + @('shell', 'am', 'force-stop', $package)) | Out-Host
    Invoke-Adb -Arguments ($deviceArgs + @('shell', 'run-as', $package, 'mkdir', '-p', $saved)) | Out-Host
    foreach ($name in ($required + @('client_highres.dat'))) {
        $source = Join-Path $DatDirectory $name
        if (!(Test-Path -LiteralPath $source -PathType Leaf)) { continue }
        $localSize = [string](Get-Item -LiteralPath $source).Length
        $localHash = (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash.ToLowerInvariant()
        $failures = @()
        for ($attempt = 1; $attempt -le 3; $attempt++) {
            try {
                $probe = "run-as $package sh -c 'if [ -f $saved/$name ]; then sha256sum $saved/$name; fi'"
                $existing = (Invoke-Adb -Arguments ($deviceArgs + @('shell', $probe)) | Out-String).Trim()
                if ($existing -and ($existing -split '\s+')[0] -eq $localHash) {
                    Write-Host "$name is already installed and verified."
                    break
                }
                Write-Host "Transferring $name directly into app storage (attempt $attempt of 3)..."
                # Only constant paths and allowlisted basenames enter Android's
                # shell. Never truncate the installed DAT on a failed transfer.
                $partial = "$saved/$name.installing"
                $receive = "run-as $package sh -c 'cat > $partial'"
                Invoke-AdbFileInput -Source $source -Arguments ($deviceArgs + @('shell', '-T', $receive))
                $size = (Invoke-Adb -Arguments ($deviceArgs + @('shell', 'run-as', $package, 'stat', '-c', '%s', $partial)) | Out-String).Trim()
                $hash = ((Invoke-Adb -Arguments ($deviceArgs + @('shell', 'run-as', $package, 'sha256sum', $partial)) | Out-String).Trim() -split '\s+')[0]
                if ($size -ne $localSize -or $hash -ne $localHash) {
                    throw "Data verification failed: $name. Received $size of $localSize bytes; SHA256 did not match."
                }
                Invoke-Adb -Arguments ($deviceArgs + @('shell', 'run-as', $package, 'mv', $partial, "$saved/$name")) | Out-Host
                Write-Host "Verified and installed $name."
                break
            } catch {
                $failure = $_.Exception.Message
                $failures += "File: $name; attempt ${attempt}: $failure"
                $diagnosticPath = Join-Path $DiagnosticDirectory 'Quest-Data.txt'
                try {
                    @("ACE Quest data repair; $([DateTime]::Now.ToString('o'))", "Device: $Serial") + $failures |
                        Set-Content -LiteralPath $diagnosticPath -Encoding UTF8
                } catch { Write-Host 'Could not save the data diagnostic file.' }
                if ($attempt -eq 3 -or $failure -match 'No space left|Permission denied|not debuggable') {
                    throw "Game data installation did not complete. $failure See Quest-Data.txt. Run Repair-Quest.cmd after correcting the problem."
                }
                Write-Host $failure
                Write-Host 'Checking the same Quest before retrying this file. Previously verified files are preserved.'
                $connection = Wait-QuestConnection -RequestedSerial $Serial
            }
        }
    }
}
