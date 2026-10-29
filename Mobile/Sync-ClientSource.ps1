#requires -Version 7.0
param([switch]$CheckOnly)
$ErrorActionPreference = 'Stop'
& (Join-Path $PSScriptRoot '../Quest/Sync-ClientSource.ps1') -TargetRoot (Join-Path $PSScriptRoot 'Project') -CheckOnly:$CheckOnly
