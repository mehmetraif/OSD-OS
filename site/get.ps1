# OSD/OS from PowerShell: downloads the latest release's zip for Windows into
# the current folder, checks it against the SHA-256 GitHub keeps for it, and
# unzips it there into an osdos folder, when there isn't one yet. With image,
# it downloads the OSD/OS image for a Raspberry Pi instead. It installs
# nothing and needs no administrator.
#
#   irm https://osdos.tv/get.ps1 | iex
#   iex "& { $(irm https://osdos.tv/get.ps1) } image"
#
# For Windows PowerShell 5.1, which comes with Windows 11, and PowerShell 7.
# The release is read from GitHub's API. $env:OSDOS_RELEASE_URL names another
# record to read: a release's own (.../releases/tags/<tag>), or a test's.
#
# All of it is one script block, run at the very end: a download of this
# script cut short runs nothing, and nothing it sets stays in the session.

& {
    param([string]$What = 'app')

    # As the script has it, whatever the session's own: both for this block alone.
    Set-StrictMode -Off
    $ErrorActionPreference = 'Stop'
    # Windows PowerShell's progress bar slows a download down many times over.
    if ($PSVersionTable.PSVersion.Major -lt 6) {
        $ProgressPreference = 'SilentlyContinue'
    }
    $repo = 'mehmetraif/OSD-OS'
    $install = "https://github.com/$repo/blob/main/INSTALL.md"

    # A size as the release page gives it, in KB, MB or GB of 1024.
    function Format-Size([double]$Size) {
        $inv = [Globalization.CultureInfo]::InvariantCulture
        if ($Size -lt 1MB) { return [string]::Format($inv, '{0:0} KB', $Size / 1KB) }
        if ($Size -lt 100MB) { return [string]::Format($inv, '{0:0.0} MB', $Size / 1MB) }
        if ($Size -lt 1GB) { return [string]::Format($inv, '{0:0} MB', $Size / 1MB) }
        return [string]::Format($inv, '{0:0.00} GB', $Size / 1GB)
    }

    function Get-Sha256([string]$Path) {
        return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
    }

    # $Url into the file $Path, $Total bytes, with get.sh's tape counter drawn
    # over itself in the console as it comes in,
    #   > PLAY [####....] 048%  REMAIN 0:46  537/1137 MB
    # in the deck's marks where the console's font has them (Windows
    # Terminal's, or another's that names itself in TERM_PROGRAM; the old
    # console's may not). Waited on a quarter of a second at a time, so that
    # Ctrl+C stops it at once.
    function Save-Download([string]$Url, [string]$Path, [long]$Total) {
        Add-Type -AssemblyName System.Net.Http
        $inv = [Globalization.CultureInfo]::InvariantCulture
        $shown = $Total -gt 0 -and $Host.Name -eq 'ConsoleHost' -and -not [Console]::IsOutputRedirected
        $marks = '>', '#', '.', '[', ']'
        if ($env:WT_SESSION -or $env:TERM_PROGRAM) {
            $marks = @(0x25B6, 0x25A0, 0x25A1, 0x2595, 0x258F | ForEach-Object { [string][char]$_ })
        }
        $cols = 80
        try {
            if ([Console]::WindowWidth -gt 0) {
                $cols = [Console]::WindowWidth
            }
        } catch {
            # No console to measure.
        }
        function Get-Megabytes([long]$Bytes) {
            $format = '{0:0}'
            if ($Total -lt 100MB) {
                $format = '{0:0.0}'
            }
            return [string]::Format($inv, $format, $Bytes / 1MB)
        }
        # As get.sh lays it out: the bar 10 to 30 marks long, the same all
        # through the download, in what the console leaves it short of its
        # last column; on a narrow one the sizes go first, then the time left.
        $draw = {
            param([long]$Got)
            if ($Got -gt $Total) {
                $Got = $Total
            }
            $seconds = $clock.Elapsed.TotalSeconds
            $left = '--:--'
            if ($Got -gt 0 -and $seconds -ge 1) {
                $s = [long][Math]::Floor($seconds * ($Total - $Got) / $Got + 0.5)
                if ($s -lt 3600) {
                    $left = [string]::Format($inv, '{0}:{1:00}', [long][Math]::Floor($s / 60), $s % 60)
                } elseif ($s -lt 360000) {
                    $left = [string]::Format($inv, '{0}:{1:00}:{2:00}', [long][Math]::Floor($s / 3600),
                                             [long][Math]::Floor($s / 60) % 60, $s % 60)
                }
            }
            $left = "  REMAIN $left"
            $sizes = "  $(Get-Megabytes $Got)/$(Get-Megabytes $Total) MB"
            $room = $cols - 18
            $widest = "  $(Get-Megabytes $Total)/$(Get-Megabytes $Total) MB".Length
            if ($room - 17 - $widest -ge 10) {
                $cells = $room - 17 - $widest
            } elseif ($room - 17 -ge 10) {
                $cells = $room - 17
                $sizes = ''
            } else {
                $cells = $room
                $left = ''
                $sizes = ''
            }
            $cells = [Math]::Max(10, [Math]::Min(30, $cells))
            $filled = [int][Math]::Floor($Got * $cells / $Total)
            $bar = $marks[1] * $filled + $marks[2] * ($cells - $filled)
            $percent = [string]::Format($inv, '{0:000}', [long][Math]::Floor($Got * 100 / $Total))
            $Host.UI.Write("`r$($marks[0]) PLAY $($marks[3])$bar$($marks[4]) $percent%$left$sizes   ")
        }

        $client = New-Object Net.Http.HttpClient
        $client.Timeout = [Threading.Timeout]::InfiniteTimeSpan
        [void]$client.DefaultRequestHeaders.UserAgent.TryParseAdd('osdos-get.ps1')
        $stop = New-Object Threading.CancellationTokenSource
        $response = $null
        $file = $null
        $copy = $null
        $drawn = $false
        try {
            $asked = $client.GetAsync($Url, [Net.Http.HttpCompletionOption]::ResponseHeadersRead, $stop.Token)
            while (-not $asked.Wait(250)) {
            }
            $response = $asked.Result
            [void]$response.EnsureSuccessStatusCode()
            $from = $response.Content.ReadAsStreamAsync().Result
            $file = [IO.File]::Create($Path)
            $clock = [Diagnostics.Stopwatch]::StartNew()
            $copy = $from.CopyToAsync($file, 81920, $stop.Token)
            while (-not $copy.Wait(250)) {
                if ($shown) {
                    & $draw $file.Position
                    $drawn = $true
                }
            }
            if ($shown) {
                & $draw $Total
                $drawn = $true
            }
        } finally {
            # Stopped, if it isn't done: Ctrl+C, or an error.
            $stop.Cancel()
            if ($response) {
                $response.Dispose()
            }
            if ($copy) {
                try {
                    [void]$copy.Wait(5000)
                } catch {
                    # It ended as it was stopped.
                }
            }
            if ($file) {
                $file.Dispose()
            }
            $client.Dispose()
            if ($drawn) {
                $Host.UI.WriteLine()
            }
        }
    }

    try {
        switch ($What) {
            'app' { $suffix = '-windows-x64.zip'; $kind = 'app for Windows' }
            'image' { $suffix = '-raspberry-pi.img.xz'; $kind = 'Raspberry Pi image' }
            default { throw "get.ps1 downloads the app for Windows, or with image the OSD/OS image for a Raspberry Pi; '$What' is neither." }
        }
        $location = Get-Location
        if ($location.Provider.Name -ne 'FileSystem') {
            throw "$location isn't a folder: run it in a folder of yours, such as your home."
        }
        $here = $location.ProviderPath
        # PowerShell run as administrator starts in Windows' own folder.
        if ($here -eq [Environment]::SystemDirectory -or $here -eq [Environment]::GetFolderPath('Windows')) {
            throw "$here is Windows' own folder: run it in a folder of yours, such as your home (cd ~)."
        }

        Write-Host "Reading OSD/OS's latest release on GitHub..."
        $releaseUrl = "https://api.github.com/repos/$repo/releases/latest"
        if ($env:OSDOS_RELEASE_URL) {
            $releaseUrl = $env:OSDOS_RELEASE_URL
        }
        try {
            $release = Invoke-RestMethod -UseBasicParsing -Uri $releaseUrl
        } catch {
            throw "GitHub didn't answer with OSD/OS's latest release; try again in a while. ($($_.Exception.Message))"
        }
        if (-not $release.tag_name) {
            throw "GitHub's answer isn't a release's record."
        }
        $tag = $release.tag_name
        $asset = @($release.assets | Where-Object { "$($_.name)".EndsWith($suffix, [StringComparison]::Ordinal) })[0]
        if (-not $asset) {
            throw "OSD/OS $tag has no $kind."
        }
        # Saved in this folder alone, under a plain name.
        $name = "$($asset.name)"
        if ($name -cnotmatch '^[A-Za-z0-9_+][A-Za-z0-9._+-]*$') {
            throw "the release's file has a name get.ps1 doesn't save: $name"
        }
        # Checked when GitHub keeps a SHA-256 for it, as it does for every
        # file a release has had since June 2025.
        $hex = ''
        if ("$($asset.digest)" -cmatch '^sha256:([0-9a-f]{64})$') {
            $hex = $Matches[1]
        }
        $file = Join-Path $here $name

        if ($hex -and (Test-Path -LiteralPath $file -PathType Leaf) -and (Get-Sha256 $file) -eq $hex) {
            Write-Host "$name is here already, and matches the SHA-256 GitHub keeps for it."
        } else {
            Write-Host "Downloading $name ($(Format-Size $asset.size)), OSD/OS $tag..."
            # Into a file of its own until it is checked: a download cut short
            # or gone wrong never stands under the file's name.
            $part = "$file.part"
            try {
                try {
                    Save-Download $asset.browser_download_url $part $asset.size
                } catch {
                    # What went wrong, from under .NET's wrappers.
                    $why = $_.Exception
                    while ($why.InnerException) {
                        $why = $why.InnerException
                    }
                    throw "the download of $name didn't finish. ($($why.Message))"
                }
                if ($hex) {
                    if ((Get-Sha256 $part) -ne $hex) {
                        throw "$name doesn't match the SHA-256 GitHub keeps for it, so it is deleted again."
                    }
                    Write-Host 'Checked: it matches the SHA-256 GitHub keeps for it.'
                } else {
                    Write-Host "GitHub keeps no SHA-256 for $name, so it isn't checked."
                }
                Move-Item -LiteralPath $part -Destination $file -Force
            } finally {
                if (Test-Path -LiteralPath $part) {
                    Remove-Item -LiteralPath $part -Force
                }
            }
        }

        Write-Host ''
        if ($What -eq 'image') {
            Write-Host 'Next: write it to an SD card with Raspberry Pi Imager (Choose OS, Use Custom),'
            Write-Host 'and choose what the card starts with on OSD/OS Setup: https://osdos.tv/setup/'
            Write-Host "More: https://github.com/$repo/blob/main/os/README.md#flashing"
            return
        }
        # The zip holds one folder, osdos. One here already is left as it
        # is: an update replaces it whole (INSTALL.md), and only once
        # OSD/OS is closed.
        $folder = Join-Path $here 'osdos'
        if (Test-Path -LiteralPath $folder) {
            Write-Host "$folder is here already, so the zip stays as it is. To update OSD/OS, close it,"
            Write-Host 'delete the old osdos folder (keep its mpv, if mpv is in it), and unzip the new one:'
            Write-Host "    Expand-Archive $name ."
        } else {
            Write-Host "Unzipping it into $folder..."
            Expand-Archive -LiteralPath $file -DestinationPath $here
            Write-Host 'Next: put mpv, which plays the videos, in the osdos folder'
            Write-Host '(https://mpv.io/installation/), and start OSD/OS:'
            Write-Host '    .\osdos\osdos.exe'
        }
        Write-Host "More: $install#on-windows-x64"
    } catch {
        Write-Host "Error: $($_.Exception.Message)" -ForegroundColor Red
    }
} @args
