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

function New-PublicBinaryArchive {
    param(
        [Parameter(Mandatory = $true)]
        [string] $Source,

        [Parameter(Mandatory = $true)]
        [string] $Destination,

        [Parameter(Mandatory = $true)]
        [string[]] $RequiredEntries
    )

    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $sourceArchive = [System.IO.Compression.ZipFile]::OpenRead($Source)
    $destinationArchive = $null
    $copiedEntries = [System.Collections.Generic.HashSet[string]]::new(
        [System.StringComparer]::OrdinalIgnoreCase
    )

    try {
        $destinationArchive = [System.IO.Compression.ZipFile]::Open(
            $Destination,
            [System.IO.Compression.ZipArchiveMode]::Create
        )

        foreach ($entry in $sourceArchive.Entries) {
            $entryName = $entry.FullName.Replace('\', '/')
            if ($entryName -match '(?i)\.pdb$') {
                continue
            }
            if ($entryName -match '(?i)(^|/)(basic|logs|crashes|plugin_config)(/|$)' -or
                $entryName -match '(?i)\.(dmp|log)$') {
                throw "The binary package contains a forbidden user or diagnostic artifact: ${entryName}"
            }

            $newEntry = $destinationArchive.CreateEntry(
                $entryName,
                [System.IO.Compression.CompressionLevel]::Optimal
            )
            if ($entry.LastWriteTime.Year -ge 1980) {
                $newEntry.LastWriteTime = $entry.LastWriteTime
            }
            $null = $copiedEntries.Add($entryName)

            if ($entryName.EndsWith('/')) {
                continue
            }
            $inputStream = $entry.Open()
            $outputStream = $newEntry.Open()
            try {
                $inputStream.CopyTo($outputStream)
            } finally {
                $outputStream.Dispose()
                $inputStream.Dispose()
            }
        }
    } finally {
        if ($destinationArchive) {
            $destinationArchive.Dispose()
        }
        $sourceArchive.Dispose()
    }

    foreach ($requiredEntry in $RequiredEntries) {
        if (-not $copiedEntries.Contains($requiredEntry)) {
            throw "The public binary package is missing required content: ${requiredEntry}"
        }
    }
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
$releaseNotesPath = Join-Path $projectRoot "RELEASE_NOTES_${version}.md"
if (-not (Test-Path -LiteralPath $releaseNotesPath)) {
    throw "Versioned release notes are required: ${releaseNotesPath}"
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

    Invoke-Checked $cmake -S $projectRoot -B $buildDirectory "-DTEMPEST_PRODUCT_VERSION=${version}" -DENABLE_WHATSNEW=OFF
    Invoke-Checked $cmake --build $buildDirectory --config $Configuration --parallel

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
New-PublicBinaryArchive -Source $builtBinary -Destination $binaryDestination -RequiredEntries @(
    'bin/64bit/tempest-broadcast-system.exe',
    'COPYING',
    'AUTHORS',
    'NOTICE.txt',
    'PUBLIC_RELEASE.md',
    "RELEASE_NOTES_${version}.md"
)

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
        Invoke-Checked -FilePath $cmake -Arguments @(
            '-E',
            'tar',
            'cf',
            $sourceDestination,
            '--format=zip',
            $sourceName
        )
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
Copy-Item -LiteralPath $releaseNotesPath -Destination $releaseDirectory

$manifest = [ordered]@{
    product = 'Tempest Broadcast System'
    version = $version
    obs_engine_version = (& $git -C $projectRoot describe --tags --match '[0-9]*' --always).Trim()
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
