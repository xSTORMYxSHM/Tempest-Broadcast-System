[CmdletBinding()]
param(
    [ValidateSet('x64')]
    [string] $Target = 'x64',

    [ValidateSet('Release', 'RelWithDebInfo')]
    [string] $Configuration = 'Release',

    [string] $OutputRoot,

    [switch] $SkipBuild,

    [switch] $SkipInstaller,

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

function Find-NSIS {
    $command = Get-Command makensis.exe -ErrorAction SilentlyContinue
    if ($command) {
        return $command.Source
    }

    $candidates = @(
        'C:\Program Files (x86)\NSIS\makensis.exe',
        'C:\Program Files\NSIS\makensis.exe'
    )

    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath $candidate) {
            return $candidate
        }
    }

    throw 'NSIS was not found. Install NSIS 3 or run the release builder with -SkipInstaller.'
}

function Wait-TrustedSigningHelpers {
    param(
        [int[]] $ExistingProcessIds = @()
    )

    $trustedSigningRoot = Join-Path $env:LOCALAPPDATA 'TrustedSigning'
    $deadline = [DateTime]::UtcNow.AddSeconds(20)
    do {
        $helpers = @(
            Get-Process -Name signtool -ErrorAction SilentlyContinue | Where-Object {
                if ($_.Id -in $ExistingProcessIds) {
                    return $false
                }
                try {
                    return $_.Path -like "${trustedSigningRoot}\*"
                } catch {
                    return $false
                }
            }
        )
        if ($helpers.Count -eq 0) {
            return
        }
        Start-Sleep -Milliseconds 500
    } while ([DateTime]::UtcNow -lt $deadline)

    foreach ($helper in $helpers) {
        Write-Warning "Stopping an orphaned Trusted Signing helper process after its timeout: $($helper.Id)"
        Stop-Process -Id $helper.Id -Force -ErrorAction SilentlyContinue
    }
    Start-Sleep -Milliseconds 500
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
                $existingSignerProcessIds = @(Get-Process -Name signtool -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Id)
                try {
                    Invoke-TrustedSigning @signingParameters | Out-Host
                } catch {
                    Write-Warning "Trusted Signing attempt ${attempt} failed: $($_.Exception.Message)"
                } finally {
                    Wait-TrustedSigningHelpers -ExistingProcessIds $existingSignerProcessIds
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

function New-PublicInstaller {
    param(
        [Parameter(Mandatory = $true)]
        [string] $SourceArchive,

        [Parameter(Mandatory = $true)]
        [string] $Destination,

        [Parameter(Mandatory = $true)]
        [string] $Version,

        [Parameter(Mandatory = $true)]
        [string] $ProjectRoot,

        [Parameter(Mandatory = $true)]
        [string] $NSIS,

        [switch] $Sign,

        [string] $TrustedSigningEndpoint,

        [string] $TrustedSigningAccount,

        [string] $TrustedSigningProfile
    )

    $installerStage = Join-Path ([System.IO.Path]::GetTempPath()) ("tempest-installer-" + [guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $installerStage -Force | Out-Null

    try {
        Expand-Archive -LiteralPath $SourceArchive -DestinationPath $installerStage
        $mainExecutable = Join-Path $installerStage 'bin\64bit\tempest-broadcast-system.exe'
        if (-not (Test-Path -LiteralPath $mainExecutable)) {
            throw 'The signed installer payload is missing the main executable.'
        }
        $updaterExecutable = Join-Path $installerStage 'bin\64bit\tempest-broadcast-updater.exe'
        if (-not (Test-Path -LiteralPath $updaterExecutable)) {
            throw 'The signed installer payload is missing the Tempest updater.'
        }
        $updaterSignature = Get-AuthenticodeSignature -LiteralPath $updaterExecutable
        if ($Sign -and ($updaterSignature.Status -ne 'Valid' -or $null -eq $updaterSignature.TimeStamperCertificate)) {
            throw 'The Tempest updater does not have a valid timestamped Authenticode signature.'
        }

        $versionParts = @($Version -split '\.')
        if ($versionParts.Count -ne 3 -or @($versionParts | Where-Object { $_ -notmatch '^\d+$' }).Count -ne 0) {
            throw "Installer versions must use numeric major.minor.patch form: ${Version}"
        }
        $numericFileVersion = "{0}.{1}.{2}.0" -f $versionParts[0], $versionParts[1], $versionParts[2]
        $installSizeBytes = (Get-ChildItem -LiteralPath $installerStage -Recurse -File | Measure-Object -Property Length -Sum).Sum
        $installSizeKB = [Math]::Ceiling($installSizeBytes / 1KB)

        $installerScript = Join-Path $ProjectRoot 'installer\TempestBroadcastSystem.nsi'
        $arguments = @(
            '/V3',
            "/DPRODUCT_VERSION=${Version}",
            "/DNUMERIC_FILE_VERSION=${numericFileVersion}",
            "/DINSTALL_SIZE_KB=${installSizeKB}",
            "/DPAYLOAD_DIR=${installerStage}",
            "/DOUTPUT_FILE=${Destination}",
            "/DPROJECT_ROOT=${ProjectRoot}"
        )
        if ($Sign) {
            $arguments += @(
                "/DSIGN_SCRIPT=$(Join-Path $ProjectRoot 'scripts\Sign-WindowsFile.ps1')",
                "/DTRUSTED_SIGNING_ENDPOINT=${TrustedSigningEndpoint}",
                "/DTRUSTED_SIGNING_ACCOUNT=${TrustedSigningAccount}",
                "/DTRUSTED_SIGNING_PROFILE=${TrustedSigningProfile}"
            )
        }
        $arguments += $installerScript
        Invoke-Checked -FilePath $NSIS -Arguments $arguments | Out-Host

        if (-not (Test-Path -LiteralPath $Destination)) {
            throw "NSIS did not create the expected installer: ${Destination}"
        }

        $signature = Get-AuthenticodeSignature -LiteralPath $Destination
        if ($Sign -and ($signature.Status -ne 'Valid' -or $null -eq $signature.TimeStamperCertificate)) {
            throw "The installer does not have a valid timestamped Authenticode signature: ${Destination}"
        }

        [pscustomobject]@{
            enabled = $true
            signed = [bool] $Sign
            signature_status = $signature.Status.ToString()
            timestamped = ($null -ne $signature.TimeStamperCertificate)
            updater_file = 'bin/64bit/tempest-broadcast-updater.exe'
            updater_signed = ($updaterSignature.Status -eq 'Valid')
            updater_signature_status = $updaterSignature.Status.ToString()
            updater_timestamped = ($null -ne $updaterSignature.TimeStamperCertificate)
        }
    } finally {
        if (Test-Path -LiteralPath $installerStage) {
            foreach ($attempt in 1..5) {
                try {
                    Remove-Item -LiteralPath $installerStage -Recurse -Force
                    break
                } catch {
                    if ($attempt -eq 5) {
                        Write-Warning "Could not fully remove temporary installer staging directory: ${installerStage}"
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
$makensis = if ($SkipInstaller) { $null } else { Find-NSIS }

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
$installerName = "tempest-broadcast-system-${version}-windows-${Target}-installer.exe"
$binaryDestination = Join-Path $releaseDirectory $binaryName
$sourceDestination = Join-Path $releaseDirectory $sourceArchiveName
$installerDestination = Join-Path $releaseDirectory $installerName

New-Item -ItemType Directory -Path $releaseDirectory -Force | Out-Null
foreach ($artifact in @($binaryDestination, $sourceDestination, $installerDestination, (Join-Path $releaseDirectory 'SHA256SUMS.txt'))) {
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
    'bin/64bit/tempest-broadcast-updater.exe',
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

$installerSummary = if ($SkipInstaller) {
    [pscustomobject]@{
        enabled = $false
        signed = $false
        signature_status = $null
        timestamped = $false
        updater_file = 'bin/64bit/tempest-broadcast-updater.exe'
        updater_signed = $false
        updater_signature_status = $null
        updater_timestamped = $false
    }
} else {
    New-PublicInstaller -SourceArchive $binaryDestination -Destination $installerDestination -Version $version `
        -ProjectRoot $projectRoot -NSIS $makensis -Sign:$Sign -TrustedSigningEndpoint $TrustedSigningEndpoint `
        -TrustedSigningAccount $TrustedSigningAccount -TrustedSigningProfile $TrustedSigningProfile
}

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
    newly_signed_files = $signatureSummary.newly_signed_files
    verified_executable_files = $signatureSummary.verified_executable_files
    installer = [ordered]@{
        included = [bool] $installerSummary.enabled
        file = if ($installerSummary.enabled) { $installerName } else { $null }
        signed = [bool] $installerSummary.signed
        signature_status = $installerSummary.signature_status
        timestamped = [bool] $installerSummary.timestamped
    }
    updater = [ordered]@{
        included = $true
        file = $installerSummary.updater_file
        signed = [bool] $installerSummary.updater_signed
        signature_status = $installerSummary.updater_signature_status
        timestamped = [bool] $installerSummary.updater_timestamped
        release_source = 'https://github.com/xSTORMYxSHM/Tempest-Broadcast-System/releases/latest'
    }
    generated_utc = [DateTime]::UtcNow.ToString('o')
}
$manifest | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $releaseDirectory 'release-manifest.json') -Encoding UTF8

$checksummedFiles = @($binaryDestination, $sourceDestination)
if ($installerSummary.enabled) {
    $checksummedFiles += $installerDestination
}
$checksumLines = foreach ($file in $checksummedFiles) {
    $hash = Get-FileHash -LiteralPath $file -Algorithm SHA256
    "{0} *{1}" -f $hash.Hash.ToLowerInvariant(), (Split-Path -Leaf $file)
}
$checksumLines | Set-Content -LiteralPath (Join-Path $releaseDirectory 'SHA256SUMS.txt') -Encoding ASCII

Write-Host "Public release artifacts created in: ${releaseDirectory}"
Get-ChildItem -LiteralPath $releaseDirectory | Select-Object Name, Length, LastWriteTime
