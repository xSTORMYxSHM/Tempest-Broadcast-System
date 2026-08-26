[CmdletBinding()]
param(
    [ValidateSet('x64')]
    [string] $Target = 'x64',

    [ValidateSet('Release', 'RelWithDebInfo')]
    [string] $Configuration = 'Release',

    [string] $OutputRoot,

    [switch] $SkipBuild
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0

function Invoke-Checked {
    param(
        [Parameter(Mandatory = $true)]
        [string] $FilePath,

        [Parameter(ValueFromRemainingArguments = $true)]
        [string[]] $Arguments
    )

    & $FilePath @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "Command failed with exit code ${LASTEXITCODE}: ${FilePath} $($Arguments -join ' ')"
    }
}

function Find-CMake {
    $command = Get-Command cmake.exe -ErrorAction SilentlyContinue
    if ($command) {
        return $command.Source
    }

    $candidates = @(
        'D:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe',
        'C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe',
        'C:\Program Files\CMake\bin\cmake.exe'
    )

    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath $candidate) {
            return $candidate
        }
    }

    throw 'CMake was not found. Install CMake or add cmake.exe to PATH.'
}

$projectRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$buildDirectory = Join-Path $projectRoot "build_${Target}"
$cmake = Find-CMake
$cpack = Join-Path (Split-Path -Parent $cmake) 'cpack.exe'
$git = (Get-Command git.exe -ErrorAction Stop).Source

if (-not (Test-Path -LiteralPath $cpack)) {
    throw "CPack was not found beside CMake: ${cpack}"
}

$bootstrap = Get-Content -LiteralPath (Join-Path $projectRoot 'cmake\common\bootstrap.cmake') -Raw
$versionMatch = [regex]::Match($bootstrap, 'set\(TEMPEST_PRODUCT_VERSION\s+"([^"]+)"')
if (-not $versionMatch.Success) {
    throw 'Could not read TEMPEST_PRODUCT_VERSION from cmake/common/bootstrap.cmake.'
}
$version = $versionMatch.Groups[1].Value
$obsVersionMatch = [regex]::Match($bootstrap, 'set\(TEMPEST_OBS_ENGINE_VERSION\s+"([^"]+)"')
if (-not $obsVersionMatch.Success) {
    throw 'Could not read TEMPEST_OBS_ENGINE_VERSION from cmake/common/bootstrap.cmake.'
}
$obsEngineVersion = $obsVersionMatch.Groups[1].Value
$releaseNotes = Join-Path $projectRoot "docs\releases\${version}.md"

if (-not (Test-Path -LiteralPath $releaseNotes)) {
    throw "Release notes are required before packaging: ${releaseNotes}"
}

$status = @(& $git -C $projectRoot status --porcelain=v1 --untracked-files=all)
if ($LASTEXITCODE -ne 0) {
    throw 'Could not inspect the Git worktree.'
}
if ($status.Count -ne 0) {
    throw "Public releases require a clean worktree. Commit or remove all changes first.`n$($status -join "`n")"
}

$commit = (& $git -C $projectRoot rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0) {
    throw 'Could not determine the source commit.'
}

$requiredTag = "tempest-v${version}"
$matchingTag = @(& $git -C $projectRoot tag --points-at HEAD --list $requiredTag)
if ($LASTEXITCODE -ne 0 -or $matchingTag.Count -eq 0) {
    throw "The release commit must be tagged ${requiredTag}."
}

$submoduleStatus = @(& $git -C $projectRoot submodule status --recursive)
if ($LASTEXITCODE -ne 0) {
    throw 'Could not inspect Git submodules.'
}
$invalidSubmodules = @($submoduleStatus | Where-Object { $_ -match '^[-+U]' })
if ($invalidSubmodules.Count -ne 0) {
    throw "Every submodule must be initialized at its pinned commit.`n$($invalidSubmodules -join "`n")"
}

$forbiddenValues = @(
    $env:USERPROFILE,
    $projectRoot
) | Where-Object { -not [string]::IsNullOrWhiteSpace($_) }

foreach ($forbidden in $forbiddenValues) {
    & $git -C $projectRoot grep -I -l -F -- $forbidden -- ':!scripts/Build-PublicRelease.ps1' 2>$null
    if ($LASTEXITCODE -eq 0) {
        throw "A tracked source file contains a machine-specific path: ${forbidden}"
    }
    if ($LASTEXITCODE -ne 1) {
        throw 'The machine-specific path scan failed.'
    }
}

if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
    $OutputRoot = if (Test-Path -LiteralPath 'G:\') {
        'G:\Tempest Broadcast Releases'
    } else {
        Join-Path $projectRoot 'release'
    }
}

$releaseDirectory = Join-Path $OutputRoot $version
$binaryName = "tempest-broadcast-system-${version}-windows-${Target}.zip"
$sourceName = "tempest-broadcast-system-${version}-source"
$sourceArchiveName = "${sourceName}.zip"
$binaryDestination = Join-Path $releaseDirectory $binaryName
$sourceDestination = Join-Path $releaseDirectory $sourceArchiveName

New-Item -ItemType Directory -Path $releaseDirectory -Force | Out-Null
foreach ($artifact in @($binaryDestination, $sourceDestination, (Join-Path $releaseDirectory 'SHA256SUMS.txt'))) {
    if (Test-Path -LiteralPath $artifact) {
        throw "Release artifact already exists and will not be overwritten: ${artifact}"
    }
}

if (-not $SkipBuild) {
    $running = Get-Process -Name 'tempest-broadcast-system' -ErrorAction SilentlyContinue
    if ($running) {
        throw 'Close Tempest Broadcast System before making a public build.'
    }

    Invoke-Checked $cmake --preset windows-x64 "-DTEMPEST_PRODUCT_VERSION=${version}" -DENABLE_WHATSNEW=OFF
    # Several x64 targets invoke the shared x86 helper build. Serializing the
    # outer MSBuild graph prevents those nested builds from racing on the same
    # ZERO_CHECK state file under Visual Studio 2026.
    Invoke-Checked $cmake --build $buildDirectory --config $Configuration --parallel 1

    Push-Location $buildDirectory
    try {
        Invoke-Checked $cpack --config (Join-Path $buildDirectory 'CPackConfig.cmake') -C $Configuration -G ZIP
    } finally {
        Pop-Location
    }
}

$builtBinary = Join-Path $buildDirectory $binaryName
if (-not (Test-Path -LiteralPath $builtBinary)) {
    throw "The expected binary package was not produced: ${builtBinary}"
}
Copy-Item -LiteralPath $builtBinary -Destination $binaryDestination

$temporaryRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("tempest-release-" + [guid]::NewGuid().ToString('N'))
$sourceStage = Join-Path $temporaryRoot $sourceName
New-Item -ItemType Directory -Path $sourceStage -Force | Out-Null

try {
    $rootArchive = Join-Path $temporaryRoot 'root.zip'
    Invoke-Checked $git -C $projectRoot archive --format=zip --output=$rootArchive HEAD
    Expand-Archive -LiteralPath $rootArchive -DestinationPath $sourceStage

    foreach ($line in $submoduleStatus) {
        $normalized = $line.TrimStart(' ', '-', '+', 'U')
        $parts = $normalized -split '\s+'
        if ($parts.Count -lt 2) {
            throw "Could not parse submodule status: ${line}"
        }

        $submoduleCommit = $parts[0]
        $submodulePath = $parts[1]
        $submoduleRoot = Join-Path $projectRoot $submodulePath
        $submoduleDestination = Join-Path $sourceStage $submodulePath
        $submoduleArchive = Join-Path $temporaryRoot (([guid]::NewGuid().ToString('N')) + '.zip')

        Invoke-Checked $git -C $submoduleRoot archive --format=zip --output=$submoduleArchive $submoduleCommit
        New-Item -ItemType Directory -Path $submoduleDestination -Force | Out-Null
        Expand-Archive -LiteralPath $submoduleArchive -DestinationPath $submoduleDestination
    }

    $sourceDescription = @(
        "Tempest Broadcast System ${version}",
        "Release tag: ${requiredTag}",
        "Source commit: ${commit}",
        '',
        'Pinned submodules:',
        $submoduleStatus
    )
    Set-Content -LiteralPath (Join-Path $sourceStage 'SOURCE_COMMIT.txt') -Value $sourceDescription -Encoding UTF8

    Push-Location $temporaryRoot
    try {
        Invoke-Checked -FilePath $cmake -Arguments @('-E', 'tar', 'cf', $sourceDestination, '--format=zip', $sourceName)
    } finally {
        Pop-Location
    }
} finally {
    if (Test-Path -LiteralPath $temporaryRoot) {
        Remove-Item -LiteralPath $temporaryRoot -Recurse -Force
    }
}

Copy-Item -LiteralPath (Join-Path $projectRoot 'NOTICE.txt') -Destination $releaseDirectory
Copy-Item -LiteralPath (Join-Path $projectRoot 'PUBLIC_RELEASE.md') -Destination $releaseDirectory
Copy-Item -LiteralPath $releaseNotes -Destination (Join-Path $releaseDirectory 'RELEASE_NOTES.md')

$manifest = [ordered]@{
    product = 'Tempest Broadcast System'
    version = $version
    obs_engine_version = $obsEngineVersion
    source_commit = $commit
    source_tag = $requiredTag
    platform = "windows-${Target}"
    configuration = $Configuration
    signed = $false
    generated_utc = [DateTime]::UtcNow.ToString('o')
}
$manifest | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $releaseDirectory 'release-manifest.json') -Encoding UTF8

$checksummedFiles = @($binaryDestination, $sourceDestination)
$checksumLines = foreach ($file in $checksummedFiles) {
    $hash = Get-FileHash -LiteralPath $file -Algorithm SHA256
    "{0} *{1}" -f $hash.Hash.ToLowerInvariant(), (Split-Path -Leaf $file)
}
$checksumLines | Set-Content -LiteralPath (Join-Path $releaseDirectory 'SHA256SUMS.txt') -Encoding ASCII

Write-Host "Public release artifacts created in: ${releaseDirectory}"
Get-ChildItem -LiteralPath $releaseDirectory | Select-Object Name, Length, LastWriteTime
