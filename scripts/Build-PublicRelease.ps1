[CmdletBinding()]
param(
    [ValidateSet('x64')]
    [string] $Target = 'x64',

    [ValidateSet('Release', 'RelWithDebInfo')]
    [string] $Configuration = 'Release',

    [string] $OutputRoot,

    [switch] $SkipBuild,

    [switch] $Sign,

    [string] $TrustedSigningEndpoint = 'https://wus2.codesigning.azure.net/',

    [string] $TrustedSigningAccount = 'Tempest',

    [string] $TrustedSigningProfile = 'TempestSoftwarePublic'
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0

if ($Sign -and $PSVersionTable.PSVersion.Major -lt 7) {
    throw 'Signed releases require PowerShell 7 (pwsh) and the TrustedSigning module.'
}

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

function Invoke-PublicCodeSigning {
    param(
        [Parameter(Mandatory = $true)]
        [string] $Source,

        [Parameter(Mandatory = $true)]
        [string] $Endpoint,

        [Parameter(Mandatory = $true)]
        [string] $Account,

        [Parameter(Mandatory = $true)]
        [string] $Profile
    )

    $signableFiles = @(
        Get-ChildItem -LiteralPath $Source -Recurse -File | Where-Object {
            $_.Extension -in @('.exe', '.dll', '.pyd')
        }
    )
    if ($signableFiles.Count -eq 0) {
        throw 'The public binary package does not contain any signable Windows files.'
    }

    $signatureState = @(
        foreach ($file in $signableFiles) {
            $signature = Get-AuthenticodeSignature -LiteralPath $file.FullName
            [pscustomobject]@{
                File = $file
                Status = $signature.Status.ToString()
            }
        }
    )
    $invalidSignatures = @($signatureState | Where-Object { $_.Status -notin @('Valid', 'NotSigned') })
    if ($invalidSignatures.Count -ne 0) {
        $details = $invalidSignatures | ForEach-Object {
            "{0}: {1}" -f $_.Status, $_.File.FullName
        }
        throw "The package contains invalid pre-existing signatures.`n$($details -join "`n")"
    }

    $unsignedFiles = @($signatureState | Where-Object { $_.Status -eq 'NotSigned' })
    if ($unsignedFiles.Count -ne 0) {
        Import-Module TrustedSigning -MinimumVersion 0.5.8 -ErrorAction Stop
        $fileNumber = 0
        foreach ($unsignedFile in $unsignedFiles) {
            $fileNumber++
            $signingParameters = @{
                Endpoint = $Endpoint
                CodeSigningAccountName = $Account
                CertificateProfileName = $Profile
                Files = $unsignedFile.File.FullName
                FileDigest = 'SHA256'
                TimestampRfc3161 = 'http://timestamp.acs.microsoft.com'
                TimestampDigest = 'SHA256'
                Description = 'Tempest Broadcast System'
                DescriptionUrl = 'https://github.com/xSTORMYxSHM/Tempest-Broadcast-System'
                Timeout = 600
                ErrorAction = 'Stop'
            }

            $signedAndTimestamped = $false
            foreach ($attempt in 1..4) {
                Write-Host "Signing file ${fileNumber} of $($unsignedFiles.Count), attempt ${attempt}: $($unsignedFile.File.Name)"
                try {
                    Invoke-TrustedSigning @signingParameters | Out-Host
                } catch {
                    Write-Warning "Trusted Signing attempt ${attempt} failed: $($_.Exception.Message)"
                }

                try {
                    $signature = Get-AuthenticodeSignature -LiteralPath $unsignedFile.File.FullName
                } catch {
                    Write-Warning "Could not read the signature after attempt ${attempt}: $($_.Exception.Message)"
                    Start-Sleep -Milliseconds 500
                    continue
                }
                if ($signature.Status -eq 'Valid' -and $null -ne $signature.TimeStamperCertificate) {
                    $signedAndTimestamped = $true
                    break
                }
                Write-Warning "Signature verification after attempt ${attempt}: status=$($signature.Status), timestamped=$($null -ne $signature.TimeStamperCertificate)"
            }

            if (-not $signedAndTimestamped) {
                throw "Could not produce a valid timestamped signature after four attempts: $($unsignedFile.File.FullName)"
            }
        }
    }

    $failedVerification = @(
        foreach ($file in $signableFiles) {
            $signature = Get-AuthenticodeSignature -LiteralPath $file.FullName
            if ($signature.Status -ne 'Valid' -or $null -eq $signature.TimeStamperCertificate) {
                "status={0}, timestamped={1}: {2}" -f $signature.Status, ($null -ne $signature.TimeStamperCertificate), $file.FullName
            }
        }
    )
    if ($failedVerification.Count -ne 0) {
        throw "Every shipped Windows executable must have a valid timestamped Authenticode signature.`n$($failedVerification -join "`n")"
    }

    [pscustomobject]@{
        enabled = $true
        newly_signed_files = $unsignedFiles.Count
        verified_executable_files = $signableFiles.Count
        service = 'Microsoft Trusted Signing'
        certificate_profile = $Profile
    }
}

function New-PublicBinaryArchive {
    param(
        [Parameter(Mandatory = $true)]
        [string] $Source,

        [Parameter(Mandatory = $true)]
        [string] $Destination,

        [Parameter(Mandatory = $true)]
        [string[]] $RequiredEntries,

        [switch] $Sign,

        [string] $TrustedSigningEndpoint,

        [string] $TrustedSigningAccount,

        [string] $TrustedSigningProfile
    )

    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $sourceArchive = [System.IO.Compression.ZipFile]::OpenRead($Source)
    $stageRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("tempest-binary-" + [guid]::NewGuid().ToString('N'))
    $stagePrefix = $stageRoot.TrimEnd([System.IO.Path]::DirectorySeparatorChar) + [System.IO.Path]::DirectorySeparatorChar
    $copiedEntries = [System.Collections.Generic.HashSet[string]]::new(
        [System.StringComparer]::OrdinalIgnoreCase
    )
    New-Item -ItemType Directory -Path $stageRoot -Force | Out-Null

    try {
        foreach ($entry in $sourceArchive.Entries) {
            $entryName = $entry.FullName.Replace('\', '/')
            if ($entryName -match '(?i)\.pdb$') {
                continue
            }
            if ($entryName -match '(?i)(^|/)(basic|logs|crashes|plugin_config)(/|$)' -or
                $entryName -match '(?i)\.(dmp|log)$') {
                throw "The binary package contains a forbidden user or diagnostic artifact: ${entryName}"
            }

            $null = $copiedEntries.Add($entryName)
            if ($entryName.EndsWith('/')) {
                continue
            }

            $stagedPath = [System.IO.Path]::GetFullPath((Join-Path $stageRoot $entryName))
            if (-not $stagedPath.StartsWith($stagePrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
                throw "The binary package contains an unsafe path: ${entryName}"
            }
            $stagedParent = Split-Path -Parent $stagedPath
            if (-not (Test-Path -LiteralPath $stagedParent)) {
                New-Item -ItemType Directory -Path $stagedParent -Force | Out-Null
            }
            [System.IO.Compression.ZipFileExtensions]::ExtractToFile($entry, $stagedPath, $false)
        }

        foreach ($requiredEntry in $RequiredEntries) {
            if (-not $copiedEntries.Contains($requiredEntry)) {
                throw "The public binary package is missing required content: ${requiredEntry}"
            }
        }

        $signatureSummary = if ($Sign) {
            Invoke-PublicCodeSigning -Source $stageRoot -Endpoint $TrustedSigningEndpoint -Account $TrustedSigningAccount -Profile $TrustedSigningProfile
        } else {
            [pscustomobject]@{
                enabled = $false
                newly_signed_files = 0
                verified_executable_files = 0
                service = $null
                certificate_profile = $null
            }
        }

        [System.IO.Compression.ZipFile]::CreateFromDirectory(
            $stageRoot,
            $Destination,
            [System.IO.Compression.CompressionLevel]::Optimal,
            $false
        )
        $signatureSummary
    } finally {
        $sourceArchive.Dispose()
        if (Test-Path -LiteralPath $stageRoot) {
            foreach ($attempt in 1..5) {
                try {
                    Remove-Item -LiteralPath $stageRoot -Recurse -Force
                    break
                } catch {
                    if ($attempt -eq 5) {
                        Write-Warning "Could not fully remove temporary package staging directory: ${stageRoot}"
                    } else {
                        Start-Sleep -Milliseconds 500
                    }
                }
            }
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

$machinePathProjectRoot = $projectRoot
if ([System.IO.Path]::GetPathRoot($projectRoot) -eq $projectRoot) {
    $driveName = [System.IO.Path]::GetPathRoot($projectRoot).Substring(0, 2)
    $substitution = @(& subst.exe 2>$null) | Where-Object { $_ -like "${driveName}\:*" } | Select-Object -First 1
    if ($substitution -and $substitution -match '=>\s*(.+)$') {
        $machinePathProjectRoot = $Matches[1].Trim()
    }
}

$forbiddenValues = @($env:USERPROFILE, $machinePathProjectRoot) |
    Where-Object { -not [string]::IsNullOrWhiteSpace($_) }

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

    Invoke-Checked $cmake -S $projectRoot -B $buildDirectory "-DTEMPEST_PRODUCT_VERSION=${version}" -DENABLE_WHATSNEW=OFF -DENABLE_BROWSER=ON -DENABLE_COREAUDIO_ENCODER=OFF
    # The x64 solution also builds and stages x86 capture helpers. Unbounded
    # MSBuild parallelism can make both helper paths copy the same file at once,
    # especially from a OneDrive-backed checkout, so favor release reliability.
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
$signatureSummary = New-PublicBinaryArchive -Source $builtBinary -Destination $binaryDestination -Sign:$Sign `
    -TrustedSigningEndpoint $TrustedSigningEndpoint -TrustedSigningAccount $TrustedSigningAccount `
    -TrustedSigningProfile $TrustedSigningProfile -RequiredEntries @(
    'bin/64bit/tempest-broadcast-system.exe',
    'obs-plugins/64bit/obs-browser.dll',
    'obs-plugins/64bit/obs-browser-page.exe',
    'obs-plugins/64bit/libcef.dll',
    'data/obs-plugins/obs-browser/locale/en-US.ini',
    'licenses/build-dependencies/cef/LICENSE.txt',
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
    signed = [bool] $signatureSummary.enabled
    signing_service = $signatureSummary.service
    signing_certificate_profile = $signatureSummary.certificate_profile
    newly_signed_files = $signatureSummary.newly_signed_files
    verified_executable_files = $signatureSummary.verified_executable_files
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
