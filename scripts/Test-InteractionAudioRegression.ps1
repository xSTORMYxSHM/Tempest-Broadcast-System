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

$bridgePath = Join-Path $SourceRoot 'frontend/docks/TempestStudioBridge.cpp'
$contractPath = Join-Path $SourceRoot 'docs/TEMPEST_INTERACTION_RENDER_CONTRACT_V1.md'
$bridge = Get-Content -Raw -LiteralPath $bridgePath
$contract = Get-Content -Raw -LiteralPath $contractPath
$showMatch = [regex]::Match(
    $bridge,
    'void TempestStudioBridge::HandleInteractionShow[\s\S]*?void TempestStudioBridge::HandleInteractionUpdate'
)

Assert-Condition $showMatch.Success 'Unable to isolate the interaction show handler.'
$showHandler = $showMatch.Value

Assert-Condition (-not ($showHandler -match 'HandleAudioPlay\s*\(')) `
    'Interaction show must not dispatch the legacy audio-play path.'
Assert-Condition (-not ($showHandler -match 'obs_source_update\s*\(|RefreshBrowser|refreshBrowser')) `
    'Interaction show must not refresh its browser source.'
Assert-Condition (([regex]::Matches($showHandler, 'obs_source_media_restart\s*\(').Count) -eq 1) `
    'Interaction show must have exactly one deduplicated controllable-media restart site.'
Assert-Condition ($showHandler -match 'restartMedia') `
    'Controllable-media restart must remain explicitly opt-in.'
Assert-Condition ($showHandler -match 'QSet<quintptr>\s+restartSources') `
    'Controllable-media restarts must remain deduplicated by OBS source.'
Assert-Condition ($contract -match 'source-native and single-path') `
    'The render contract must preserve the source-native single-path audio rule.'
Assert-Condition ($contract -match 'must not also dispatch `broadcast\.audio\.play`') `
    'The render contract must tell Studio not to duplicate Web Audio with broadcast.audio.play.'

Write-Host 'Interaction audio regression checks passed.'
