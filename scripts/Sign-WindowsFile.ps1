[CmdletBinding()]
param(
    [Parameter(Mandatory = $true, Position = 0)]
    [string] $Path,

    [string] $TrustedSigningEndpoint = 'https://wus2.codesigning.azure.net/',

    [string] $TrustedSigningAccount = 'Tempest',

    [string] $TrustedSigningProfile = 'TempestSoftwarePublic'
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0

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

if ($PSVersionTable.PSVersion.Major -lt 7) {
    throw 'Windows code signing requires PowerShell 7 (pwsh) and the TrustedSigning module.'
}

$resolvedPath = (Resolve-Path -LiteralPath $Path).Path
$extension = [System.IO.Path]::GetExtension($resolvedPath)
$isNSISTemporaryExecutable = $false
if ($extension -eq '.tmp') {
    $stream = [System.IO.File]::OpenRead($resolvedPath)
    try {
        $isNSISTemporaryExecutable = ($stream.ReadByte() -eq 0x4D -and $stream.ReadByte() -eq 0x5A)
    } finally {
        $stream.Dispose()
    }
}
if ($extension -notin @('.exe', '.dll', '.pyd', '.msi') -and -not $isNSISTemporaryExecutable) {
    throw "Unsupported Authenticode file type: ${resolvedPath}"
}

$existingSignature = Get-AuthenticodeSignature -LiteralPath $resolvedPath
if ($existingSignature.Status -eq 'Valid' -and $null -ne $existingSignature.TimeStamperCertificate) {
    Write-Host "A valid timestamped signature is already present: ${resolvedPath}"
    exit 0
}
if ($existingSignature.Status -notin @('Valid', 'NotSigned')) {
    throw "The file has an invalid pre-existing signature ($($existingSignature.Status)): ${resolvedPath}"
}

Import-Module TrustedSigning -MinimumVersion 0.5.8 -ErrorAction Stop
$signingParameters = @{
    Endpoint = $TrustedSigningEndpoint
    CodeSigningAccountName = $TrustedSigningAccount
    CertificateProfileName = $TrustedSigningProfile
    Files = $resolvedPath
    FileDigest = 'SHA256'
    TimestampRfc3161 = 'http://timestamp.acs.microsoft.com'
    TimestampDigest = 'SHA256'
    Description = 'Tempest Broadcast System'
    DescriptionUrl = 'https://github.com/xSTORMYxSHM/Tempest-Broadcast-System'
    Timeout = 600
    ErrorAction = 'Stop'
}

foreach ($attempt in 1..4) {
    Write-Host "Signing attempt ${attempt}: ${resolvedPath}"
    $existingSignerProcessIds = @(Get-Process -Name signtool -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Id)
    try {
        Invoke-TrustedSigning @signingParameters | Out-Host
    } catch {
        Write-Warning "Trusted Signing attempt ${attempt} failed: $($_.Exception.Message)"
    } finally {
        Wait-TrustedSigningHelpers -ExistingProcessIds $existingSignerProcessIds
    }

    $signature = Get-AuthenticodeSignature -LiteralPath $resolvedPath
    if ($signature.Status -eq 'Valid' -and $null -ne $signature.TimeStamperCertificate) {
        Write-Host "Verified valid timestamped signature: ${resolvedPath}"
        exit 0
    }

    Write-Warning "Signature verification after attempt ${attempt}: status=$($signature.Status), timestamped=$($null -ne $signature.TimeStamperCertificate)"
}

throw "Could not produce a valid timestamped signature after four attempts: ${resolvedPath}"
