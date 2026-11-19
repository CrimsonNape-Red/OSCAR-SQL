# nightly-publish.ps1
#
# Publishes a nightly OSCAR build to the nightly download page
# (https://seeker4.gitlab.io/oscar-nightly, repo Seeker4/oscar-nightly).
#
# Copyright (c) 2026 The OSCAR Team
#
# Called by nightly-build.bat after a successful build. The download files are kept in the
# oscar-nightly project's GitLab package registry under their original names, one package
# per platform and build: oscar-windows/<hash>, oscar-linux/<hash>, oscar-macos/<hash>, plus
# oscar-checksums/<timestamp>. Steps:
#   1. Find the Windows installer and portable ZIP just built, and take the commit hash
#      from the installer name (OSCAR-<version>-Win64-<hash>.exe).
#   2. Choose a Linux build from the shared Linux folder, whose subfolders are named
#      <yyyyMMddhhmm>_<hash>. Use the one for the same commit if it is complete;
#      otherwise use the most recent complete one. "Complete" means it has a package for
#      every Linux download on the page, and every package matches the folder's
#      checksumSHA256.txt (which also catches files still being synced).
#      If no Linux build is newer than the one already published, Linux is left as it is.
#   3. Take the macOS build from the Mac folder, which holds a single universal .dmg
#      (OSCAR-<version>-<hash>-macOS-universal.dmg) built before this script runs. It is
#      published if its hash differs from the page's and the file is whole (it ends with the
#      UDIF "koly" trailer, so a file still syncing is skipped).
#   4. Upload the new files, then a checksums file built from the registry's own SHA256 of
#      every file the page will link to.
#   5. Set the build date, build ids and download links in the page; commit and push.
#   6. Delete the packages the page no longer links to.
#
# Download links in the page are <a data-dl="KEY" href="...">file name</a>; this script
# rewrites the whole element. Keys: Win64, Win64-Portable, macOS-universal, checksums, and
# <Distro>_<arch>-Qt6 for each Linux package (e.g. Debian12_amd64-Qt6).
#
# Needs a GitLab token with api scope in the GITLAB_TOKEN environment variable.
#
# Exit codes: 0 = published (or nothing new), 1 = error.
#
# Usage: powershell -NoProfile -ExecutionPolicy Bypass -File nightly-publish.ps1 [options]
#   -SkipWindows no new Windows build tonight: publish newer Linux/macOS builds only
#                (-LinuxOnly is accepted as an older name for it)
#   -SkipLinux   leave Linux as it is (for testing)
#   -SkipMac     leave macOS as it is (for testing)
#   -NoPush      commit the page locally, do not push it or delete old packages (for testing)

param(
    [string]$InstallerDir = 'C:\OSCAR\OSCAR-code\build-oscar-win_64_bit\Installer',
    [string]$LinuxRoot    = "$env:USERPROFILE\Dropbox\tranfert_deb",
    [string]$MacRoot      = "$env:USERPROFILE\Dropbox\Nightly",
    [string]$PageRepo     = 'C:\OSCAR\OSCAR-nightly',
    [string]$ProjectId    = '88962889',     # Seeker4/oscar-nightly
    [Alias('LinuxOnly')]
    [switch]$SkipWindows,
    [switch]$SkipLinux,
    [switch]$SkipMac,
    [switch]$NoPush
)

$ErrorActionPreference = 'Stop'
$ProgressPreference    = 'SilentlyContinue'     # the progress bar makes uploads very slow
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
$utf8NoBom = New-Object System.Text.UTF8Encoding($false)
$Api = "https://gitlab.com/api/v4/projects/$ProjectId"

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

# Maps a Linux package name to its page key, or $null.
# oscar20-test_2.2.0-alpha-1-Debian12_amd64-Qt6.deb -> Debian12_amd64-Qt6
function Get-DebKey([string]$name) {
    if ($name -match '-((?:Debian|Ubuntu|RasPiOS-?)\d+_[A-Za-z0-9]+-Qt6)\.deb$') { return $Matches[1] }
    return $null
}

# Checks one Linux build folder. Returns an ordered map (source path -> page key) if the
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
            $key = Get-DebKey $deb.Name
            if (-not $key) {
                Log "  WARNING: Unrecognised Linux package name, not published: $($deb.Name)"
                continue
            }
            if ($expected -notcontains $key) {
                Log "  WARNING: $($deb.Name) has no download link on the page ($key); not published."
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
            $map[$deb.FullName] = $key
        }
        if (-not $ok) { continue }

        $missing = @($expected | Where-Object { @($map.Values) -notcontains $_ })
        if ($missing.Count -eq 0) { return $map }
        Log "  $($dir.Name): missing $($missing -join ', ')"
    }
    return $null
}

# True if the .dmg ends with its UDIF trailer ("koly" 512 bytes from the end), i.e. it is
# not truncated or still being synced.
function Test-DmgComplete([string]$path) {
    $stream = [System.IO.File]::OpenRead($path)
    try {
        if ($stream.Length -lt 512) { return $false }
        [void]$stream.Seek(-512, [System.IO.SeekOrigin]::End)
        $magic = New-Object byte[] 4
        [void]$stream.Read($magic, 0, 4)
        return ([System.Text.Encoding]::ASCII.GetString($magic) -eq 'koly')
    } finally {
        $stream.Close()
    }
}

# ---- GitLab package registry ----

function Get-Token {
    $t = $env:GITLAB_TOKEN
    if (-not $t) { $t = [Environment]::GetEnvironmentVariable('GITLAB_TOKEN', 'User') }
    if (-not $t) { $t = [Environment]::GetEnvironmentVariable('GITLAB_TOKEN', 'Machine') }
    if (-not $t) { Fail 'GITLAB_TOKEN is not set; it is needed to upload to the package registry.' }
    return $t
}

function Invoke-Api([string]$method, [string]$path, [string]$inFile) {
    $p = @{ Method = $method; Uri = "$Api/$path"; Headers = @{ 'PRIVATE-TOKEN' = $script:Token } }
    if ($inFile) { $p.InFile = $inFile; $p.ContentType = 'application/octet-stream' }
    # PowerShell 5.1's Invoke-RestMethod emits a JSON array as one object; assigning it and
    # writing the variable out unrolls it, so callers get one item per array element.
    $r = Invoke-RestMethod @p
    $r
}

# All generic packages called $name (the API's name filter is a partial match).
function Get-Packages([string]$name) {
    $all = @(); $page = 1
    do {
        $batch = @(Invoke-Api GET "packages?package_type=generic&package_name=$name&per_page=100&page=$page")
        $all += $batch; $page++
    } while ($batch.Count -eq 100)
    return @($all | Where-Object { $_.name -eq $name })
}

# file name -> sha256 for the files in package $name/$version (empty if it does not exist).
function Get-PackageFiles([string]$name, [string]$version) {
    $files = @{}
    foreach ($p in (Get-Packages $name | Where-Object { $_.version -eq $version })) {
        foreach ($f in @(Invoke-Api GET "packages/$($p.id)/package_files?per_page=100")) {
            $files[$f.file_name] = $f.file_sha256
        }
    }
    return $files
}

function Get-DownloadUrl([string]$name, [string]$version, [string]$file) {
    return "$Api/packages/generic/$name/$version/$file"
}

# Uploads $path into package $name/$version unless the same file is already there.
function Publish-File([string]$name, [string]$version, [string]$path, $existing) {
    $file = Split-Path $path -Leaf
    $sha = (Get-FileHash -Algorithm SHA256 -LiteralPath $path).Hash.ToLower()
    if ($existing[$file] -eq $sha) {
        Log "  $file is already uploaded."
        return
    }
    # Upload a temporary copy: Invoke-RestMethod -InFile opens the file for writing, which
    # fails on the read-only files in a shared Dropbox folder.
    $tmpDir = Join-Path $env:TEMP 'oscar-nightly-upload'
    [void](New-Item -ItemType Directory -Force -Path $tmpDir)
    $tmp = Join-Path $tmpDir $file
    Copy-Item -LiteralPath $path -Destination $tmp -Force
    try {
        Set-ItemProperty -LiteralPath $tmp -Name IsReadOnly -Value $false
        $r = Invoke-Api PUT "packages/generic/$name/$version/$file`?select=package_file" $tmp
    } finally {
        Remove-Item -LiteralPath $tmp -Force -ErrorAction SilentlyContinue
    }
    if ($r.file_sha256 -ne $sha) { Fail "Upload of $file did not verify (SHA256 $($r.file_sha256), expected $sha)" }
    Log "  Uploaded $file -> $name/$version"
}

# Replaces every <a data-dl="$key" ...>...</a> in $html.
function Set-PageLink([string]$html, [string]$key, [string]$url, [string]$text) {
    $pattern = '<a data-dl="' + [regex]::Escape($key) + '"[^>]*>[^<]*</a>'
    if ($html -notmatch $pattern) { Fail "Page has no <a data-dl=`"$key`">" }
    $a = '<a data-dl="' + $key + '" href="' + $url + '">' + $text + '</a>'
    return [regex]::Replace($html, $pattern, { param($m) $a })
}

try {
    $script:Token = Get-Token

    # The page records which builds are currently published.
    Git pull --quiet --ff-only origin master
    $page = Join-Path $PageRepo 'public\index.html'
    $html = [System.IO.File]::ReadAllText($page, $utf8NoBom)
    $currentWindows = Get-PageSpan $html 'buildIdWindows'
    $currentLinux   = Get-PageSpan $html 'buildIdLinux'
    $currentMac     = Get-PageSpan $html 'buildIdMac'
    if ($null -eq $currentWindows) { Fail 'Page has no <span id="buildIdWindows">' }
    if ($null -eq $currentLinux)   { Fail 'Page has no <span id="buildIdLinux">' }
    if ($null -eq $currentMac)     { Fail 'Page has no <span id="buildIdMac">' }
    $winHash = $currentWindows; $linuxHash = $currentLinux; $macHash = $currentMac

    # New files to publish: package name -> @{ version; files = [ordered]@{ path -> page key } }
    $uploads = [ordered]@{}

    # ---------------------------------------------------------------------
    # Step 1: Windows files and the commit hash.
    # ---------------------------------------------------------------------
    if ($SkipWindows) {
        Log "No new Windows build; Windows build $winHash kept."
    } else {
        $exe = Get-ChildItem -Path $InstallerDir -Filter 'OSCAR-*-Win64-*.exe' -File -ErrorAction SilentlyContinue |
               Sort-Object LastWriteTime -Descending | Select-Object -First 1
        if (-not $exe) { Fail "No installer found in $InstallerDir" }
        if ($exe.BaseName -notmatch '-Win64-([0-9a-fA-F]{7,40})$') {
            Fail "Cannot take a commit hash from $($exe.Name) (a '-plus' build has uncommitted changes)"
        }
        $exeHash = $Matches[1].ToLower()
        $zip = Join-Path $InstallerDir ($exe.BaseName + '-Portable.zip')
        if (-not (Test-Path $zip)) { Fail "Portable ZIP not found: $zip" }

        if ($exeHash -eq $currentWindows) {
            Log "Windows build $exeHash is already published."
        } else {
            $winHash = $exeHash
            Log "Windows build: $($exe.Name)"
            $files = [ordered]@{}
            $files[$exe.FullName] = 'Win64'
            $files[$zip]          = 'Win64-Portable'
            $uploads['oscar-windows'] = @{ version = $winHash; files = $files }
        }
    }

    # ---------------------------------------------------------------------
    # Step 2: Choose the Linux build.
    # ---------------------------------------------------------------------
    if ($SkipLinux) {
        Log "Linux skipped (-SkipLinux); Linux build $currentLinux kept."
    } elseif (-not (Test-Path $LinuxRoot)) {
        Log "WARNING: Linux build folder not found: $LinuxRoot. Linux build $currentLinux kept."
    } else {
        $expected = @([regex]::Matches($html, '<a data-dl="([^"]+-Qt6)"') | ForEach-Object { $_.Groups[1].Value } |
                      Select-Object -Unique)
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
        } elseif ((& $folderHash $chosen) -eq $currentLinux) {
            Log "Linux build $($chosen.Name) is already published."
        } else {
            $linuxHash = & $folderHash $chosen
            Log "Linux build: $($chosen.Name)"
            $uploads['oscar-linux'] = @{ version = $linuxHash; files = $chosenMap }
        }
    }

    # ---------------------------------------------------------------------
    # Step 3: The macOS build. The Mac folder holds a single universal .dmg;
    #         its build id comes from the file name.
    # ---------------------------------------------------------------------
    if ($SkipMac) {
        Log "macOS skipped (-SkipMac); macOS build $currentMac kept."
    } elseif (-not (Test-Path $MacRoot)) {
        Log "WARNING: macOS build folder not found: $MacRoot. macOS build $currentMac kept."
    } else {
        $dmgs = @(Get-ChildItem -Path $MacRoot -Filter '*-macOS-universal.dmg' -File |
                  Sort-Object LastWriteTime -Descending)
        if ($dmgs.Count -eq 0) {
            Log "No macOS build in $MacRoot; macOS build $currentMac kept."
        } else {
            $dmg = $dmgs[0]
            if ($dmgs.Count -gt 1) {
                Log "WARNING: $($dmgs.Count) macOS builds in $MacRoot; using the newest, $($dmg.Name)."
            }
            $dmgHash = $null
            if ($dmg.Name -match '-([0-9a-fA-F]{7,40})-macOS-universal\.dmg$') { $dmgHash = $Matches[1].ToLower() }
            if (-not $dmgHash) {
                Log "WARNING: Cannot take a build id from $($dmg.Name); macOS build $currentMac kept."
            } elseif ($dmgHash -eq $currentMac) {
                Log "macOS build $currentMac is already published."
            } elseif (-not (Test-DmgComplete $dmg.FullName)) {
                Log "WARNING: $($dmg.Name) is incomplete (still syncing?); macOS build $currentMac kept."
            } else {
                $macHash = $dmgHash
                Log "macOS build: $($dmg.Name)"
                $files = [ordered]@{}
                $files[$dmg.FullName] = 'macOS-universal'
                $uploads['oscar-macos'] = @{ version = $macHash; files = $files }
            }
        }
    }

    if ($uploads.Count -eq 0) {
        Log 'Nothing new to publish; page left unchanged.'
        exit 0
    }

    # ---------------------------------------------------------------------
    # Step 4: Upload the new files, then the checksums of everything the
    #         page will link to.
    # ---------------------------------------------------------------------
    foreach ($name in $uploads.Keys) {
        $u = $uploads[$name]
        $existing = Get-PackageFiles $name $u.version
        foreach ($path in $u.files.Keys) {
            Publish-File $name $u.version $path $existing
            $file = Split-Path $path -Leaf
            $html = Set-PageLink $html $u.files[$path] (Get-DownloadUrl $name $u.version $file) $file
        }
    }

    $ids = "Windows $winHash, Linux $linuxHash, macOS $macHash"
    $lines = @("SHA256 checksums for OSCAR nightly builds: $ids", '')
    foreach ($pair in @(@('oscar-windows', $winHash), @('oscar-linux', $linuxHash), @('oscar-macos', $macHash))) {
        $files = Get-PackageFiles $pair[0] $pair[1]
        if ($files.Count -eq 0) { Log "WARNING: no $($pair[0])/$($pair[1]) package in the registry for the checksums." }
        foreach ($f in ($files.Keys | Sort-Object)) { $lines += "$($files[$f])  $f" }
    }
    $sumsName = 'OSCAR-nightly-checksums.txt'
    $sumsVersion = Get-Date -Format 'yyyyMMddHHmmss'
    $sumsPath = Join-Path $env:TEMP $sumsName
    [System.IO.File]::WriteAllText($sumsPath, ($lines -join "`n") + "`n", $utf8NoBom)
    Publish-File 'oscar-checksums' $sumsVersion $sumsPath @{}
    Remove-Item $sumsPath
    $html = Set-PageLink $html 'checksums' (Get-DownloadUrl 'oscar-checksums' $sumsVersion $sumsName) 'SHA256 checksums for all downloads'

    # ---------------------------------------------------------------------
    # Step 5: Update the page, commit and push.
    # ---------------------------------------------------------------------
    $en = [System.Globalization.CultureInfo]::GetCultureInfo('en-US')
    $buildDate = (Get-Date).ToString('MMMM d, yyyy', $en)

    foreach ($pair in @(@('buildDate', $buildDate), @('buildIdWindows', $winHash),
                        @('buildIdLinux', $linuxHash), @('buildIdMac', $macHash))) {
        $pattern = '(<span id="' + $pair[0] + '">)[^<]*(</span>)'
        $html = [regex]::Replace($html, $pattern, '${1}' + $pair[1] + '${2}')
    }
    [System.IO.File]::WriteAllText($page, $html, $utf8NoBom)
    Log "Page set to $buildDate, $ids"

    $msgFile = Join-Path $env:TEMP 'oscar-nightly-commit.txt'
    [System.IO.File]::WriteAllText($msgFile, "Nightly build: $ids`n", $utf8NoBom)
    Git add public/index.html
    Git commit --quiet -F $msgFile
    Remove-Item $msgFile
    if ($NoPush) {
        Log 'Page committed; not pushed (-NoPush), old packages kept.'
        Log "Published: $ids."
        exit 0
    }
    Git push --quiet origin master
    Log 'Page pushed.'

    # ---------------------------------------------------------------------
    # Step 6: Delete the packages the page no longer links to.
    # ---------------------------------------------------------------------
    $keep = @{ 'oscar-windows' = $winHash; 'oscar-linux' = $linuxHash; 'oscar-macos' = $macHash;
               'oscar-checksums' = $sumsVersion }
    foreach ($name in $keep.Keys) {
        foreach ($p in (Get-Packages $name | Where-Object { $_.version -ne $keep[$name] })) {
            Invoke-Api DELETE "packages/$($p.id)" | Out-Null
            Log "  Deleted old package $name/$($p.version)"
        }
    }

    Log "Published: $ids."
    exit 0
}
catch {
    Fail $_.Exception.Message
}
