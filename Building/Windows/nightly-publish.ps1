# nightly-publish.ps1
#
# Publishes a nightly OSCAR build to the nightly download page
# (https://seeker4.gitlab.io/oscar-nightly, repo Seeker4/oscar-nightly).
#
# Copyright (c) 2026 The OSCAR Team
#
# Called by nightly-build.bat after a successful build. Steps:
#   1. Find the Windows installer and portable ZIP just built, and take the commit hash
#      from the installer name (OSCAR-<version>-Win64-<hash>.exe).
#   2. Find the Linux build folder for the same commit in SQL_Version
#      (folder name <yyyyMMddhhmm>_<hash>). If it is not there yet, wait once
#      (default 60 minutes) and look again. If it is still missing, change nothing.
#   3. Copy all files into the Dropbox Nightly folder under fixed names, overwriting the
#      previous files in place so their Dropbox shared links keep working.
#   4. Rewrite OSCAR-nightly-checksums.txt.
#   5. Set the build date, build id and expiry date in the page, then commit and push.
#
# Only files whose fixed name already exists in the Nightly folder are replaced, because
# only those have shared links in the page. A Linux package with a new name is reported
# in the log but not copied.
#
# Exit codes: 0 = published, 1 = error, 2 = Linux build not found (nothing changed).
#
# Usage: powershell -NoProfile -ExecutionPolicy Bypass -File nightly-publish.ps1 [options]
#   -SkipLinux   publish the Windows files and page only (for testing)
#   -NoPush      commit the page locally but do not push it
#   -RetryMinutes N   wait before the second look for the Linux build (default 60)

param(
    [string]$InstallerDir = 'C:\OSCAR\OSCAR-code\build-oscar-win_64_bit\Installer',
    [string]$NightlyDir   = "$env:USERPROFILE\Dropbox\OSCAR Files\Nightly",
    [string]$LinuxRoot    = "$env:USERPROFILE\Dropbox\SQL_Version",
    [string]$PageRepo     = 'C:\OSCAR\OSCAR-nightly',
    [int]$RetryMinutes    = 60,
    [switch]$SkipLinux,
    [switch]$NoPush
)

$ErrorActionPreference = 'Stop'
$utf8NoBom = New-Object System.Text.UTF8Encoding($false)

function Log([string]$msg) {
    Write-Output ("[{0}] {1}" -f (Get-Date -Format 'yyyy-MM-dd HH:mm:ss'), $msg)
}

function Fail([string]$msg) {
    Log "ERROR: $msg"
    exit 1
}

# Runs git in the page repo; $LASTEXITCODE holds its exit code afterwards.
# git writes progress to stderr; PowerShell 5.1 turns that into error records when output
# is redirected (as nightly-build.bat does), so stderr is merged into plain text here.
function GitTry {
    $old = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    # git.exe, not git: a bare "git" would resolve to the Git function below.
    & git.exe -C $PageRepo @args 2>&1 | ForEach-Object { Write-Output "  git: $_" }
    $ErrorActionPreference = $old
}

# Runs git in the page repo and stops on failure.
function Git {
    GitTry @args
    if ($LASTEXITCODE -ne 0) { Fail "git $($args -join ' ') failed (exit $LASTEXITCODE)" }
}

try {
    # ---------------------------------------------------------------------
    # Step 1: Windows files and the commit hash.
    # ---------------------------------------------------------------------
    $exe = Get-ChildItem -Path $InstallerDir -Filter 'OSCAR-*-Win64-*.exe' -File -ErrorAction SilentlyContinue |
           Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if (-not $exe) { Fail "No installer found in $InstallerDir" }

    if ($exe.BaseName -notmatch '-Win64-([0-9a-fA-F]{7,40})$') {
        Fail "Cannot take a commit hash from $($exe.Name) (a '-plus' build has uncommitted changes)"
    }
    $hash = $Matches[1].ToLower()

    $zip = Join-Path $InstallerDir ($exe.BaseName + '-Portable.zip')
    if (-not (Test-Path $zip)) { Fail "Portable ZIP not found: $zip" }

    Log "Windows build: $($exe.Name) (commit $hash)"

    if (-not (Test-Path $NightlyDir)) { Fail "Nightly folder not found: $NightlyDir" }

    # Source -> fixed name in the Nightly folder.
    $copies = [ordered]@{}
    $copies[$exe.FullName] = 'OSCAR-nightly-Win64.exe'
    $copies[$zip]          = 'OSCAR-nightly-Win64-Portable.zip'

    # ---------------------------------------------------------------------
    # Step 2: Linux build folder for the same commit.
    # ---------------------------------------------------------------------
    if (-not $SkipLinux) {
        if (-not (Test-Path $LinuxRoot)) { Fail "Linux build folder not found: $LinuxRoot" }

        $findLinux = {
            Get-ChildItem -Path $LinuxRoot -Directory |
                Where-Object { $_.Name -match "^\d{12}_$hash$" } |
                Sort-Object Name -Descending | Select-Object -First 1
        }

        $linuxDir = & $findLinux
        if (-not $linuxDir) {
            Log "No Linux build for $hash yet; looking again in $RetryMinutes minutes."
            Start-Sleep -Seconds ($RetryMinutes * 60)
            $linuxDir = & $findLinux
        }
        if (-not $linuxDir) {
            Log "WARNING: No Linux build for $hash in $LinuxRoot. Nightly files and page left unchanged."
            exit 2
        }
        Log "Linux build: $($linuxDir.Name)"

        $expected = Get-ChildItem -Path $NightlyDir -Filter 'oscar20-nightly-*.deb' -File |
                    ForEach-Object Name
        $found = @()
        foreach ($deb in Get-ChildItem -Path $linuxDir.FullName -Filter '*.deb' -File) {
            if ($deb.Name -notmatch '-((?:Debian|Ubuntu|RasPiOS-?)\d+_[A-Za-z0-9]+-Qt6)\.deb$') {
                Log "WARNING: Unrecognised Linux package name, not copied: $($deb.Name)"
                continue
            }
            $target = "oscar20-nightly-$($Matches[1]).deb"
            if ($expected -notcontains $target) {
                Log "WARNING: $($deb.Name) has no download link yet ($target); not copied."
                continue
            }
            $copies[$deb.FullName] = $target
            $found += $target
        }
        foreach ($name in $expected) {
            if ($found -notcontains $name) {
                Log "WARNING: The Linux build has no package for $name; previous file kept."
            }
        }
    }

    # ---------------------------------------------------------------------
    # Step 3: Overwrite the fixed-name files in place.
    #         Copy over the existing file; never delete or rename it, so its
    #         Dropbox shared link stays valid.
    # ---------------------------------------------------------------------
    foreach ($src in $copies.Keys) {
        $dest = Join-Path $NightlyDir $copies[$src]
        Copy-Item -LiteralPath $src -Destination $dest -Force
        Log "Copied $(Split-Path $src -Leaf) -> $($copies[$src])"
    }

    # ---------------------------------------------------------------------
    # Step 4: Checksums for everything in the Nightly folder except itself.
    # ---------------------------------------------------------------------
    $sumsName = 'OSCAR-nightly-checksums.txt'
    $lines = @("SHA256 checksums for OSCAR nightly build $hash", '')
    Get-ChildItem -Path $NightlyDir -File |
        Where-Object { $_.Name -ne $sumsName -and $_.Name -ne 'desktop.ini' } |
        Sort-Object Name |
        ForEach-Object {
            $h = (Get-FileHash -Algorithm SHA256 -LiteralPath $_.FullName).Hash.ToLower()
            $lines += "$h  $($_.Name)"
        }
    [System.IO.File]::WriteAllText((Join-Path $NightlyDir $sumsName),
                                   ($lines -join "`n") + "`n", $utf8NoBom)
    Log "Wrote $sumsName"

    # ---------------------------------------------------------------------
    # Step 5: Update the page, commit and push.
    # ---------------------------------------------------------------------
    Git pull --quiet --ff-only origin master

    $en = [System.Globalization.CultureInfo]::GetCultureInfo('en-US')
    $today = Get-Date
    $buildDate  = $today.ToString('MMMM d, yyyy', $en)
    $expireDate = $today.AddMonths(2).ToString('MMMM d, yyyy', $en)

    $page = Join-Path $PageRepo 'public\index.html'
    $html = [System.IO.File]::ReadAllText($page, $utf8NoBom)
    foreach ($pair in @(@('buildDate', $buildDate), @('buildId', $hash), @('expireDate', $expireDate))) {
        $pattern = '(<span id="' + $pair[0] + '">)[^<]*(</span>)'
        if ($html -notmatch $pattern) { Fail "Page has no <span id=`"$($pair[0])`">" }
        $html = [regex]::Replace($html, $pattern, '${1}' + $pair[1] + '${2}')
    }
    [System.IO.File]::WriteAllText($page, $html, $utf8NoBom)
    Log "Page set to $buildDate, build $hash, expires $expireDate"

    GitTry diff --quiet '--' public/index.html
    if ($LASTEXITCODE -eq 0) {
        Log 'Page unchanged; nothing to commit.'
    } else {
        $msgFile = Join-Path $env:TEMP 'oscar-nightly-commit.txt'
        [System.IO.File]::WriteAllText($msgFile, "Nightly build $hash`n", $utf8NoBom)
        Git add public/index.html
        Git commit --quiet -F $msgFile
        Remove-Item $msgFile
        if ($NoPush) {
            Log 'Page committed; not pushed (-NoPush).'
        } else {
            Git push --quiet origin master
            Log 'Page pushed.'
        }
    }

    Log "Nightly build $hash published."
    exit 0
}
catch {
    Fail $_.Exception.Message
}
