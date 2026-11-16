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
#   2. Choose a Linux build from the shared Linux folder, whose subfolders are named
#      <yyyyMMddhhmm>_<hash>. Use the one for the same commit if it is complete;
#      otherwise use the most recent complete one. "Complete" means it has a package for
#      every Linux download on the page, and every package matches the folder's
#      checksumSHA256.txt (which also catches files still being synced).
#      If no Linux build is newer than the one already published, the Linux files are left
#      as they are.
#   3. Copy the files into the Dropbox Nightly folder under fixed names, overwriting the
#      previous files in place so their Dropbox shared links keep working.
#   4. Rewrite OSCAR-nightly-checksums.txt.
#   5. Set the build date and the Windows and Linux build ids in the page, then commit and push.
#
# Only files whose fixed name already exists in the Nightly folder are replaced, because
# only those have shared links in the page. A Linux package with a new name is reported
# in the log but not copied.
#
# Exit codes: 0 = published, 1 = error.
#
# Usage: powershell -NoProfile -ExecutionPolicy Bypass -File nightly-publish.ps1 [options]
#   -SkipLinux   publish the Windows files and page only (for testing)
#   -NoPush      commit the page locally but do not push it

param(
    [string]$InstallerDir = 'C:\OSCAR\OSCAR-code\build-oscar-win_64_bit\Installer',
    [string]$NightlyDir   = "$env:USERPROFILE\Dropbox\OSCAR Files\Nightly",
    [string]$LinuxRoot    = "$env:USERPROFILE\Dropbox\tranfert_deb",
    [string]$PageRepo     = 'C:\OSCAR\OSCAR-nightly',
    [switch]$SkipLinux,
    [switch]$NoPush
)

$ErrorActionPreference = 'Stop'
$utf8NoBom = New-Object System.Text.UTF8Encoding($false)

# Writes straight to the console (stdout), not the PowerShell output stream: anything a
# function writes to the output stream becomes part of its return value.
function Log([string]$msg) {
    [Console]::Out.WriteLine(("[{0}] {1}" -f (Get-Date -Format 'yyyy-MM-dd HH:mm:ss'), $msg))
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
    & git.exe -C $PageRepo @args 2>&1 | ForEach-Object { [Console]::Out.WriteLine("  git: $_") }
    $ErrorActionPreference = $old
}

# Runs git in the page repo and stops on failure.
function Git {
    GitTry @args
    if ($LASTEXITCODE -ne 0) { Fail "git $($args -join ' ') failed (exit $LASTEXITCODE)" }
}

# Returns the text of <span id="$id">...</span> in the page, or $null.
function Get-PageSpan([string]$html, [string]$id) {
    if ($html -match ('<span id="' + $id + '">([^<]*)</span>')) { return $Matches[1] }
    return $null
}

# Maps a Linux package name to its fixed name in the Nightly folder, or $null.
# oscar20-test_2.2.0-alpha-1-Debian12_amd64-Qt6.deb -> oscar20-nightly-Debian12_amd64-Qt6.deb
function Get-DebTarget([string]$name) {
    if ($name -match '-((?:Debian|Ubuntu|RasPiOS-?)\d+_[A-Za-z0-9]+-Qt6)\.deb$') {
        return "oscar20-nightly-$($Matches[1]).deb"
    }
    return $null
}

# Checks one Linux build folder. Returns an ordered map (source path -> fixed name) if the
# build is complete, or $null. The packages are sometimes also in a subfolder of the same
# name, so that is tried as well.
function Get-CompleteLinuxBuild($dir, [string[]]$expected) {
    foreach ($candidate in @($dir.FullName, (Join-Path $dir.FullName $dir.Name))) {
        if (-not (Test-Path -LiteralPath $candidate -PathType Container)) { continue }

        $sums = @{}
        $sumsFile = Join-Path $candidate 'checksumSHA256.txt'
        if (Test-Path -LiteralPath $sumsFile) {
            foreach ($line in Get-Content -LiteralPath $sumsFile) {
                if ($line -match '^([0-9a-fA-F]{64})\s+\*?(.+?)\s*$') { $sums[$Matches[2]] = $Matches[1].ToLower() }
            }
        }

        $map = [ordered]@{}
        $ok = $true
        foreach ($deb in Get-ChildItem -LiteralPath $candidate -Filter '*.deb' -File) {
            $target = Get-DebTarget $deb.Name
            if (-not $target) {
                Log "  WARNING: Unrecognised Linux package name, not copied: $($deb.Name)"
                continue
            }
            if ($expected -notcontains $target) {
                Log "  WARNING: $($deb.Name) has no download link yet ($target); not copied."
                continue
            }
            if ($sums.Count -gt 0) {
                if (-not $sums.ContainsKey($deb.Name)) {
                    Log "  $($dir.Name): $($deb.Name) is not in checksumSHA256.txt"
                    $ok = $false; break
                }
                $h = (Get-FileHash -Algorithm SHA256 -LiteralPath $deb.FullName).Hash.ToLower()
                if ($h -ne $sums[$deb.Name]) {
                    Log "  $($dir.Name): $($deb.Name) does not match its checksum (still syncing?)"
                    $ok = $false; break
                }
            }
            $map[$deb.FullName] = $target
        }
        if (-not $ok) { continue }

        $missing = @($expected | Where-Object { @($map.Values) -notcontains $_ })
        if ($missing.Count -eq 0) { return $map }
        Log "  $($dir.Name): missing $($missing -join ', ')"
    }
    return $null
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
    $winHash = $Matches[1].ToLower()

    $zip = Join-Path $InstallerDir ($exe.BaseName + '-Portable.zip')
    if (-not (Test-Path $zip)) { Fail "Portable ZIP not found: $zip" }

    Log "Windows build: $($exe.Name) (commit $winHash)"

    if (-not (Test-Path $NightlyDir)) { Fail "Nightly folder not found: $NightlyDir" }

    # Source -> fixed name in the Nightly folder.
    $copies = [ordered]@{}
    $copies[$exe.FullName] = 'OSCAR-nightly-Win64.exe'
    $copies[$zip]          = 'OSCAR-nightly-Win64-Portable.zip'

    # The page records which Linux build is currently published.
    Git pull --quiet --ff-only origin master
    $page = Join-Path $PageRepo 'public\index.html'
    $html = [System.IO.File]::ReadAllText($page, $utf8NoBom)
    $currentLinux = Get-PageSpan $html 'buildIdLinux'
    if ($null -eq $currentLinux) { Fail 'Page has no <span id="buildIdLinux">' }
    $linuxHash = $currentLinux

    # ---------------------------------------------------------------------
    # Step 2: Choose the Linux build.
    # ---------------------------------------------------------------------
    if ($SkipLinux) {
        Log "Linux skipped (-SkipLinux); page keeps Linux build $currentLinux."
    } elseif (-not (Test-Path $LinuxRoot)) {
        Log "WARNING: Linux build folder not found: $LinuxRoot. Linux build $currentLinux kept."
    } else {
        $expected = @(Get-ChildItem -Path $NightlyDir -Filter 'oscar20-nightly-*.deb' -File | ForEach-Object Name)
        $folders = @(Get-ChildItem -Path $LinuxRoot -Directory |
                     Where-Object { $_.Name -match '^\d{12}_[0-9a-fA-F]{7,40}$' } |
                     Sort-Object Name -Descending)
        $folderHash = { param($d) ($d.Name -split '_')[1].ToLower() }

        $chosen = $null; $chosenMap = $null

        # Same commit as Windows, if complete.
        $same = $folders | Where-Object { (& $folderHash $_) -eq $winHash } | Select-Object -First 1
        if ($same) {
            $chosenMap = Get-CompleteLinuxBuild $same $expected
            if ($chosenMap) { $chosen = $same }
        } else {
            Log "No Linux build for $winHash."
        }

        # Otherwise the most recent complete build newer than the one already published.
        if (-not $chosen) {
            foreach ($f in $folders) {
                if ((& $folderHash $f) -eq $currentLinux) { break }
                $chosenMap = Get-CompleteLinuxBuild $f $expected
                if ($chosenMap) { $chosen = $f; break }
            }
        }

        if (-not $chosen) {
            Log "No newer complete Linux build; Linux build $currentLinux kept."
        } else {
            $linuxHash = & $folderHash $chosen
            if ($linuxHash -eq $currentLinux) {
                Log "Linux build $($chosen.Name) is already published."
            } else {
                Log "Linux build: $($chosen.Name)"
                foreach ($src in $chosenMap.Keys) { $copies[$src] = $chosenMap[$src] }
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
    $lines = @("SHA256 checksums for OSCAR nightly builds: Windows $winHash, Linux $linuxHash", '')
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
    $en = [System.Globalization.CultureInfo]::GetCultureInfo('en-US')
    $buildDate = (Get-Date).ToString('MMMM d, yyyy', $en)

    foreach ($pair in @(@('buildDate', $buildDate), @('buildIdWindows', $winHash), @('buildIdLinux', $linuxHash))) {
        $pattern = '(<span id="' + $pair[0] + '">)[^<]*(</span>)'
        if ($html -notmatch $pattern) { Fail "Page has no <span id=`"$($pair[0])`">" }
        $html = [regex]::Replace($html, $pattern, '${1}' + $pair[1] + '${2}')
    }
    [System.IO.File]::WriteAllText($page, $html, $utf8NoBom)
    Log "Page set to $buildDate, Windows build $winHash, Linux build $linuxHash"

    GitTry diff --quiet '--' public/index.html
    if ($LASTEXITCODE -eq 0) {
        Log 'Page unchanged; nothing to commit.'
    } else {
        $msgFile = Join-Path $env:TEMP 'oscar-nightly-commit.txt'
        [System.IO.File]::WriteAllText($msgFile, "Nightly build: Windows $winHash, Linux $linuxHash`n", $utf8NoBom)
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

    Log "Published: Windows $winHash, Linux $linuxHash."
    exit 0
}
catch {
    Fail $_.Exception.Message
}
