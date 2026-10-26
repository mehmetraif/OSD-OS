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
                    Invoke-WebRequest -UseBasicParsing -Uri $asset.browser_download_url -OutFile $part
                } catch {
                    throw "the download of $name didn't finish. ($($_.Exception.Message))"
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
