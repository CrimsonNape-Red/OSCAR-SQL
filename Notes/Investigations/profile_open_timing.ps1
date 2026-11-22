# profile_open_timing.ps1 - times how long two OSCAR builds take to open a profile (Windows).
# Copyright (c) 2026 The OSCAR Team
#
# Starts each build with --profile and measures, from launch:
#   launchToTitleMs  the main window title shows "(Profile: <name>)"  (set near the end of OpenProfile)
#   launchToReadyMs  the GUI thread is idle after that: 10 WM_NULL round trips in a row, each under 100 ms
# Then closes the window with WM_CLOSE, times the exit, and copies debug.txt for a phase breakdown.
# The builds run alternately, one pair per round. Results are appended to -OutCsv.
#
# It acts only on the process it starts and never kills it. Close every OSCAR window first.
# Each run leaves the tested profile as the "last opened" profile in both data folders.
# See PROFILE_OPEN_TIMING_1.7.1_VS_2.2.md for results and caveats (file cache state above all).
#
# Example:
#   .\profile_open_timing.ps1 -ProfileName "My Profile" -Rounds 5 `
#       -NewExe "C:\path\to\Release\OSCAR.exe" -NewLog "$env:USERPROFILE\Documents\OSCAR21_Data\logs\debug.txt"
param(
    [Parameter(Mandatory = $true)][string]$ProfileName,
    [Parameter(Mandatory = $true)][string]$NewExe,
    [Parameter(Mandatory = $true)][string]$NewLog,
    [string]$OldExe = "C:\Program Files\OSCAR\OSCAR.exe",
    [string]$OldLog = "$env:USERPROFILE\Documents\OSCAR_Data\logs\debug.txt",
    [string]$OldLabel = "old",
    [string]$NewLabel = "new",
    [int]$Rounds = 5,
    [int]$FirstRun = 1,
    [string]$OutDir = "$env:TEMP\oscar_profile_open_timing",
    [string[]]$Only = @()
)

$builds = @(
    @{ Label = $OldLabel; Exe = $OldExe; Log = $OldLog },
    @{ Label = $NewLabel; Exe = $NewExe; Log = $NewLog }
)
New-Item -ItemType Directory -Force "$OutDir\logs" | Out-Null
$OutCsv = "$OutDir\results.csv"

Add-Type -TypeDefinition @"
using System; using System.Text; using System.Collections.Generic; using System.Runtime.InteropServices;
public class WinInfo { public IntPtr Hwnd; public string Title; public bool Visible; public bool Enabled; public string Cls; }
public static class W {
  delegate bool EnumProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc cb, IntPtr l);
  [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] static extern int GetWindowText(IntPtr h, StringBuilder sb, int n);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] static extern int GetClassName(IntPtr h, StringBuilder sb, int n);
  [DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] static extern bool IsWindowEnabled(IntPtr h);
  [DllImport("user32.dll")] static extern IntPtr SendMessageTimeout(IntPtr h, uint msg, IntPtr w, IntPtr l, uint flags, uint timeout, out IntPtr res);
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint msg, IntPtr w, IntPtr l);
  public static List<WinInfo> ForPid(uint pid) {
    var list = new List<WinInfo>();
    EnumWindows(delegate(IntPtr h, IntPtr l) {
      uint p; GetWindowThreadProcessId(h, out p);
      if (p == pid) {
        var sb = new StringBuilder(512); GetWindowText(h, sb, 512);
        var cb = new StringBuilder(256); GetClassName(h, cb, 256);
        list.Add(new WinInfo { Hwnd = h, Title = sb.ToString(), Visible = IsWindowVisible(h), Enabled = IsWindowEnabled(h), Cls = cb.ToString() });
      }
      return true;
    }, IntPtr.Zero);
    return list;
  }
  public static bool Ping(IntPtr h, uint ms) { IntPtr r; return SendMessageTimeout(h, 0, IntPtr.Zero, IntPtr.Zero, 0x0002, ms, out r) != IntPtr.Zero; }
}
"@

# Returns the millisecond stamp of the first (or last) log line containing $pattern, or "".
function Get-LogMs([string[]]$lines, [string]$pattern, [switch]$Last) {
    $m = $lines | Select-String -Pattern $pattern -SimpleMatch
    if (-not $m) { return "" }
    if ($Last) { $line = ($m | Select-Object -Last 1).Line } else { $line = ($m | Select-Object -First 1).Line }
    if ($line -match '^(\d+):') { return [int]$Matches[1] }
    return ""
}

# One timed start/open/close of one build; returns the result row.
function Invoke-Run($b, [int]$run) {
    if (Get-Process OSCAR -ErrorAction SilentlyContinue) { throw "An OSCAR process is already running; not starting another." }
    $needle = "(Profile: $ProfileName)"
    $sw = [Diagnostics.Stopwatch]::StartNew()
    $p = Start-Process -FilePath $b.Exe -ArgumentList "--profile `"$ProfileName`"" -WorkingDirectory (Split-Path $b.Exe) -PassThru
    $procId = [uint32]$p.Id
    $hwnd = [IntPtr]::Zero; $tTitle = -1
    while ($sw.ElapsedMilliseconds -lt 600000 -and -not $p.HasExited) {
        $w = [W]::ForPid($procId) | Where-Object { $_.Title.Contains($needle) } | Select-Object -First 1
        if ($w) { $hwnd = $w.Hwnd; $tTitle = $sw.ElapsedMilliseconds; break }
        Start-Sleep -Milliseconds 15
    }
    if ($tTitle -lt 0) {
        $wins = ([W]::ForPid($procId) | Where-Object { $_.Visible } | ForEach-Object { "[$($_.Title)]" }) -join " "
        throw "$($b.Label): profile title never appeared (exited=$($p.HasExited)). Visible windows: $wins"
    }
    # GUI thread idle: 10 consecutive WM_NULL round trips, each under 100 ms, 50 ms apart.
    $streak = 0; $tReady = -1; $streakStart = 0
    while ($sw.ElapsedMilliseconds -lt 600000 -and -not $p.HasExited) {
        $t = $sw.ElapsedMilliseconds
        $ok = [W]::Ping($hwnd, 250)
        $lat = $sw.ElapsedMilliseconds - $t
        if ($ok -and $lat -lt 100) { if ($streak -eq 0) { $streakStart = $t }; $streak++ } else { $streak = 0 }
        if ($streak -ge 10) { $tReady = $streakStart; break }
        Start-Sleep -Milliseconds 50
    }
    $p.Refresh(); $cpuReady = $p.TotalProcessorTime.TotalSeconds
    Start-Sleep -Seconds 3      # CPU still climbing here would mean background work after the UI is idle
    $p.Refresh(); $cpuHold = $p.TotalProcessorTime.TotalSeconds
    $ws = [math]::Round($p.WorkingSet64 / 1MB); $priv = [math]::Round($p.PrivateMemorySize64 / 1MB); $peak = [math]::Round($p.PeakWorkingSet64 / 1MB)
    $extra = ([W]::ForPid($procId) | Where-Object { $_.Visible -and $_.Hwnd -ne $hwnd -and $_.Title -ne "" } | ForEach-Object { "[$($_.Title)]" }) -join " "

    $swc = [Diagnostics.Stopwatch]::StartNew()
    [void][W]::PostMessage($hwnd, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero)   # WM_CLOSE
    if (-not $p.WaitForExit(300000)) {
        $wins = ([W]::ForPid($procId) | Where-Object { $_.Visible } | ForEach-Object { "[$($_.Title)]" }) -join " "
        throw "$($b.Label): did not exit within 5 minutes of WM_CLOSE (left running, PID $procId). Visible windows: $wins"
    }
    $tClose = $swc.ElapsedMilliseconds
    Start-Sleep -Milliseconds 500

    $logCopy = "$OutDir\logs\$($b.Label)-run$run.txt"
    Copy-Item $b.Log $logCopy -Force
    $lines = Get-Content $logCopy
    $row = [pscustomobject]@{
        build = $b.Label; run = $run
        launchToTitleMs = $tTitle; launchToReadyMs = $tReady
        logOpeningProfileMs = (Get-LogMs $lines 'Opening profile "')
        logFinishingMs = (Get-LogMs $lines 'Finishing profile')
        cpuReadyS = [math]::Round($cpuReady, 2); cpuAfter3sS = [math]::Round($cpuHold, 2)
        wsMB = $ws; peakWsMB = $peak; privMB = $priv; closeMs = $tClose; extraWindows = $extra
    }
    $row | Export-Csv -Path $OutCsv -Append -NoTypeInformation
    $row
}

$out = @()
for ($r = $FirstRun; $r -lt $FirstRun + $Rounds; $r++) {
    foreach ($b in $builds) {
        if ($Only.Count -gt 0 -and $Only -notcontains $b.Label) { continue }
        $out += Invoke-Run $b $r
        Start-Sleep -Seconds 2
    }
}
$out | Format-Table build, run, launchToTitleMs, launchToReadyMs, logOpeningProfileMs, logFinishingMs, cpuReadyS, cpuAfter3sS, wsMB, privMB, closeMs, extraWindows -AutoSize | Out-String -Width 250
"Results: $OutCsv   Logs: $OutDir\logs"
