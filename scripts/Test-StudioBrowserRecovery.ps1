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
$bridge = Get-Content -Raw -LiteralPath $bridgePath
$recoveryMatch = [regex]::Match(
    $bridge,
    'void TempestStudioBridge::RecoverFailedBrowserSources\(\)[\s\S]*?void TempestStudioBridge::SetConnectionState'
)

Assert-Condition $recoveryMatch.Success 'Unable to isolate Studio Browser Source recovery.'
$recovery = $recoveryMatch.Value

Assert-Condition ($bridge -match '/v1/emote-wall') `
    'Recovery must query Studio Emote Wall connection status.'
Assert-Condition ($recovery -match 'connectedClients') `
    'Recovery must use Studio client presence instead of assuming a rendered browser document exists.'
Assert-Condition ($recovery -match 'toInt\(-1\) != 0') `
    'Recovery must run only when Studio explicitly reports zero connected clients.'
Assert-Condition ($bridge -match '!obs_source_active\(source\) && !obs_source_showing\(source\)') `
    'Recovery must leave inactive Browser Sources untouched.'
Assert-Condition ($recovery -match 'removeAllQueryItems\(QStringLiteral\("tempest_recovery"\)\)') `
    'Recovery must replace its cache-busting marker instead of growing the URL repeatedly.'
Assert-Condition ($recovery -match 'obs_source_update\(source, settings\)') `
    'Recovery must reinitialize the existing Browser Source so scene references and transforms are preserved.'
Assert-Condition (-not ($recovery -match 'refreshnocache|obs_property_button_clicked')) `
    'Recovery must not rely on the standard refresh action when the browser document never initialized.'
Assert-Condition ($recovery -match 'recoveredBrowserSources\.insert\(candidate\.uuid\)') `
    'Recovery must remain bounded to one reinitialization per source per Broadcast process.'

Write-Host 'Studio Browser Source recovery regression checks passed.'
