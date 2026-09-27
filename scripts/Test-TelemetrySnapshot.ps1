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
Assert-Condition ($snapshot -match 'obs_video_active\(\)\s*\?\s*obs_get_video\(\)\s*:\s*nullptr') `
    'Runtime telemetry must not request the main video output before the startup video reset completes.'
Assert-Condition ($snapshot -match 'obs_output_get_frames_dropped') `
    'Runtime telemetry must expose network/output dropped frames.'
Assert-Condition ($snapshot -match 'telemetrySnapshotErrorLogged') `
    'Runtime telemetry failures must be log-rate-limited.'

$frontendApiPath = Join-Path $SourceRoot 'frontend/OBSStudioAPI.cpp'
$frontendApi = Get-Content -Raw -LiteralPath $frontendApiPath
foreach ($method in @('obs_frontend_get_recording_output', 'obs_frontend_get_replay_buffer_output')) {
    $methodMatch = [regex]::Match(
        $frontendApi,
        "obs_output_t \*OBSStudioAPI::${method}\(\)[\s\S]*?\n}"
    )
    Assert-Condition $methodMatch.Success "Unable to isolate ${method}."
    Assert-Condition ($methodMatch.Value -match '!main\s*\|\|\s*!main->outputHandler') `
        "${method} must tolerate telemetry requests before the output handler is initialized."
}

Write-Host 'Runtime telemetry snapshot regression checks passed.'
