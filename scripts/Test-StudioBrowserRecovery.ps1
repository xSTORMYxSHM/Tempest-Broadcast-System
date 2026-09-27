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
Assert-Condition ($recovery -match 'connectedClients != 0') `
    'Recovery must run only when Studio explicitly reports zero connected clients.'
Assert-Condition (-not ($recovery -match 'readyClients')) `
    'Recovery must not reload a connected Browser Source while its renderer is still initializing.'
Assert-Condition ($bridge -match '!obs_source_active\(source\) && !obs_source_showing\(source\)') `
    'Recovery must leave inactive Browser Sources untouched.'
Assert-Condition ($recovery -match 'removeAllQueryItems\(QStringLiteral\("tempest_recovery"\)\)') `
    'Recovery must replace its cache-busting marker instead of growing the URL repeatedly.'
Assert-Condition ($recovery -match 'obs_source_update\(source, settings\)') `
    'Recovery must reinitialize the existing Browser Source so scene references and transforms are preserved.'
Assert-Condition ($recovery -match 'SetCurrentSceneSourceVisible\(candidate\.name, false\)' -and `
                  $recovery -match 'SetCurrentSceneSourceVisible\(candidate\.name, true\)') `
    'Recovery must wake a suspended transparent Browser Source before refreshing it.'
Assert-Condition ($recovery -match 'refreshnocache' -and $recovery -match 'obs_property_button_clicked') `
    'Recovery must invoke the Browser Source no-cache refresh after updating its URL.'
Assert-Condition (-not ($recovery -match 'recoveredBrowserSources')) `
    'Recovery must remain health-gated on every Studio reconnect instead of being disabled after one attempt.'
Assert-Condition ($bridge -match 'BrowserRecoveryDelayMs = 5000' -and `
                  $bridge -match 'QTimer::singleShot\(BrowserRecoveryDelayMs' -and `
                  $bridge -match 'QTimer::singleShot\(20000') `
    'Recovery must check both the initial source startup and the slower CEF initialization window.'

Write-Host 'Studio Browser Source recovery regression checks passed.'
