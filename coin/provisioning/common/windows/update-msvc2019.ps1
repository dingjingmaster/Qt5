############################################################################
##
## Copyright (C) 2020 The Qt Company Ltd.
## Contact: http://www.qt.io/licensing/
##
## This file is part of the provisioning scripts of the Qt Toolkit.
##
## $QT_BEGIN_LICENSE:LGPL21$
## Commercial License Usage
## Licensees holding valid commercial Qt licenses may use this file in
## accordance with the commercial license agreement provided with the
## Software or, alternatively, in accordance with the terms contained in
## a written agreement between you and The Qt Company. For licensing terms
## and conditions see http://www.qt.io/terms-conditions. For further
## information use the contact form at http://www.qt.io/contact-us.
##
## GNU Lesser General Public License Usage
## Alternatively, this file may be used under the terms of the GNU Lesser
## General Public License version 2.1 or version 3 as published by the Free
## Software Foundation and appearing in the file LICENSE.LGPLv21 and
## LICENSE.LGPLv3 included in the packaging of this file. Please review the
## following information to ensure the GNU Lesser General Public License
## requirements will be met: https://www.gnu.org/licenses/lgpl.html and
## http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
##
## As a special exception, The Qt Company gives you certain additional
## rights. These rights are described in The Qt Company LGPL Exception
## version 1.1, included in the file LGPL_EXCEPTION.txt in this package.
##
## $QT_END_LICENSE$
##
#############################################################################

. "$PSScriptRoot\helpers.ps1"

# This script will update MSVC 2019.
# NOTE! Visual Studio is pre-installed to tier 1 image so this script won't install the whole Visual Studio. See ../../../pre-provisioning/qtci-windows-10-x86_64/msvc2019.txt
# MSVC 2019 online installers can be found from here https://docs.microsoft.com/en-us/visualstudio/releases/2019/history#installing-an-earlier-release

# NOTE! Currenlty Buildtools are not updated. There seems to be an issue with installer. When it's run twice it get stuck and can't be run again. 

$version = "16.11.10"
$urlCache_vsInstaller = "http://ci-files01-hki.ci.qt.io/input/windows/msvc/VS_Professional_2019_$version.exe"
$urlOfficial_vsInstaller = "https://download.visualstudio.microsoft.com/download/pr/791f3d28-7e20-45d9-9373-5dcfbdd1f6db/cd440cf67c0cf1519131d1d51a396e44c5b4f7b68b541c9f35c05a310d692f0a/vs_Professional.exe"
$sha1_vsInstaller = "d4f3b3b7dc28dcc3f25474cd1ca1e39fca7dcf3f"
$urlCache_buildToolsInstaller = "\\ci-files01-hki.intra.qt.io\provisioning\windows\msvc\vs2019_BuildTools_$version.exe"
# $urlOfficial_buildToolsInstaller = "https://download.visualstudio.microsoft.com/download/pr/791f3d28-7e20-45d9-9373-5dcfbdd1f6db/d5eabc3f4472d5ab18662648c8b6a08ea0553699819b88f89d84ec42d12f6ad7/vs_BuildTools.exe"
# $sha1_buildToolsInstaller = "69889f45d229de8e0e76b6d9e05964477eee2e78"
$installerPath = "C:\Windows\Temp\installer.exe"

function Install {

    Param (
        [string] $urlOfficial = $(BadParam("Official url path")),
        [string] $urlCache = $(BadParam("Cached url path")),
        [string] $sha1 = $(BadParam("SHA1 checksum of the file"))

    )

    Write-Host "Installing msvc 2019 $version"
    Download $urlOfficial $urlCache $installerPath
    Verify-Checksum $installerPath $sha1
    # We have to update the installer bootstrapper before calling the actual installer.
    # Otherwise installation might fail silently
    Run-Executable "$installerPath" "--quiet --update"
    Run-Executable "$installerPath" "update --passive --wait"
    Remove-Item -Force -Path $installerPath
}

Install $urlOfficial_vsInstaller $urlCache_vsInstaller $sha1_vsInstaller
# Install $urlOfficial_buildToolsInstaller $urlCache_buildToolsInstaller $sha1_buildToolsInstaller

$msvc2019Version = (cmd /c "C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe" `
    -version [16.0,17.0`) -latest -property catalog_productDisplayVersion 2`>`&1)
$msvc2019Complete = (cmd /c "C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe" `
    -version [16.0,17.0`) -latest -property isComplete 2`>`&1)
$msvc2019Launchable = (cmd /c "C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe" `
    -version [16.0,17.0`) -latest -property isLaunchable 2`>`&1)

if($msvc2019Version -ne $version -or [int]$msvc2019Complete -ne 1 `
    -or [int]$msvc2019Launchable -ne 1) {
    throw "MSVC 2019 update failed. msvc2019Version: $($msvc2019Version) `
        msvc2019Complete: $($msvc2019Complete) msvc2019Launchable: $($msvc2019Launchable)"
}

Write-Output "Visual Studio 2019 = $msvc2019Version" >> ~\versions.txt
Write-Output "Visual Studio 2019 Build Tools = $version" >> ~\versions.txt
