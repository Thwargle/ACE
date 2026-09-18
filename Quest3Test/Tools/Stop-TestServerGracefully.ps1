#requires -Version 7.0
param([int]$ProcessId = 0)
$ErrorActionPreference = 'Stop'
$serverPath = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../Unreal/PackagedVR/Server/ACE.Server.exe'))
$servers = @(Get-CimInstance Win32_Process -Filter "name='ACE.Server.exe'" | Where-Object ExecutablePath -eq $serverPath)
if ($ProcessId -ne 0) {
    $servers = @($servers | Where-Object ProcessId -eq $ProcessId)
    if ($servers.Count -ne 1) { throw 'The specified process is not this test server. No process was stopped.' }
}
if ($servers.Count -eq 0) { Write-Host 'Test server is already stopped.'; return }
if ($servers.Count -ne 1) { throw 'Multiple test servers found. Resolve duplicates before maintenance.' }
$targetProcessId = [uint32]$servers[0].ProcessId
# Write to the server's console input, so its normal exit handler saves players
# and drains database work. Never terminate the process to replace a locked DLL.
Add-Type -TypeDefinition @'
using System;
using System.ComponentModel;
using System.Runtime.InteropServices;
public static class ACEServerConsoleStop {
 [StructLayout(LayoutKind.Sequential, CharSet=CharSet.Unicode)]
 public struct KeyEvent {
  [MarshalAs(UnmanagedType.Bool)] public bool Down;
  public ushort Repeat, VirtualKey, ScanCode;
  public char Character;
  public uint Control;
 }
 [StructLayout(LayoutKind.Explicit, CharSet=CharSet.Unicode)]
 public struct Input {
  [FieldOffset(0)] public ushort Type;
  [FieldOffset(4)] public KeyEvent Key;
 }
 [DllImport("kernel32.dll", SetLastError=true)] static extern bool FreeConsole();
 [DllImport("kernel32.dll", SetLastError=true)] static extern bool AttachConsole(uint id);
 [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)] static extern IntPtr CreateFile(string name,uint access,uint share,IntPtr security,uint creation,uint flags,IntPtr template);
 [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)] static extern bool WriteConsoleInputW(IntPtr handle,Input[] events,uint count,out uint written);
 [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr handle);
 public static void RequestExit(uint id) {
  FreeConsole();
  if (!AttachConsole(id)) throw new Win32Exception(Marshal.GetLastWin32Error(),"Could not attach to ACE's console; server was not stopped.");
  IntPtr input = new IntPtr(-1);
  try {
   input = CreateFile("CONIN$",0x40000000,3,IntPtr.Zero,3,0,IntPtr.Zero);
   if (input == new IntPtr(-1)) throw new Win32Exception(Marshal.GetLastWin32Error());
   string command = "exit\r";
   var records = new Input[command.Length];
   for(int i=0;i<command.Length;i++) records[i] = new Input {Type=1,Key=new KeyEvent {Down=true,Repeat=1,Character=command[i],VirtualKey=(ushort)(command[i]=='\r'?13:0)}};
   uint written;
   if(!WriteConsoleInputW(input,records,(uint)records.Length,out written) || written != records.Length) throw new Win32Exception(Marshal.GetLastWin32Error());
  } finally { if(input != new IntPtr(-1)) CloseHandle(input); FreeConsole(); }
 }
}
'@
[ACEServerConsoleStop]::RequestExit($targetProcessId)
Write-Host "Requested graceful ACE server exit (PID $targetProcessId)."
$target = Get-Process -Id $targetProcessId -ErrorAction SilentlyContinue
if ($target -and !$target.WaitForExit(45000)) { throw 'Server has not exited yet. It was not forcibly terminated; inspect ACE_Log.txt before updating files.' }
Write-Host 'ACE server exited; binaries can now be updated.'
