[CmdletBinding()]
param(
    [string]$SourceRoot = (Split-Path -Parent $PSScriptRoot)
)

$ErrorActionPreference = 'Stop'

function Assert-Condition {
    param(
        [bool]$Condition,
        [string]$Message
    )

    if (-not $Condition) {
        throw $Message
    }
}

$sourcePath = Join-Path $SourceRoot 'frontend/widgets/TempestMainframeBar.cpp'
$source = Get-Content -Raw -LiteralPath $sourcePath
$snapshotMatch = [regex]::Match(
    $source,
    'void TempestMainframeBar::WriteTelemetrySnapshot\(\)[\s\S]*?void TempestMainframeBar::SetTransmissionState'
)

Assert-Condition $snapshotMatch.Success 'Unable to isolate the runtime telemetry snapshot writer.'
$snapshot = $snapshotMatch.Value

Assert-Condition ($source -match 'tempest-broadcast-system/telemetry') `
    'Runtime telemetry must remain inside the Tempest Broadcast configuration root.'
Assert-Condition ($source -match 'runtime\.json') `
    'Runtime telemetry must use the stable runtime.json sidecar name.'
Assert-Condition ($snapshot -match 'QSaveFile') `
    'Runtime telemetry must be atomically replaced so readers never observe partial JSON.'

foreach ($field in @(
    'activeFps',
    'averageFrameRenderTime',
    'cpuUsage',
    'memoryUsage',
    'renderSkippedFrames',
    'renderTotalFrames',
    'outputSkippedFrames',
    'outputTotalFrames',
    'streamOutput',
    'recordOutput'
)) {
    Assert-Condition ($snapshot -match [regex]::Escape($field)) `
        "Runtime telemetry is missing the ${field} compatibility field."
}

Assert-Condition ($snapshot -match 'obs_frontend_get_streaming_output') `
    'Runtime telemetry must report the real primary stream output when available.'
Assert-Condition ($snapshot -match 'obs_output_get_frames_dropped') `
    'Runtime telemetry must expose network/output dropped frames.'
Assert-Condition ($snapshot -match 'telemetrySnapshotErrorLogged') `
    'Runtime telemetry failures must be log-rate-limited.'

Write-Host 'Runtime telemetry snapshot regression checks passed.'
