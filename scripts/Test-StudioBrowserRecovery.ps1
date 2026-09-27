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
    'void TempestStudioBridge::RecoverFailedBrowserSources\(bool includeUnready\)[\s\S]*?void TempestStudioBridge::SetConnectionState'
)

Assert-Condition $recoveryMatch.Success 'Unable to isolate Studio Browser Source recovery.'
$recovery = $recoveryMatch.Value

Assert-Condition ($bridge -match '/v1/emote-wall') `
    'Recovery must query Studio Emote Wall connection status.'
Assert-Condition ($recovery -match 'connectedClients') `
    'Recovery must use Studio client presence instead of assuming a rendered browser document exists.'
Assert-Condition ($recovery -match 'const bool disconnected = connectedClients == 0 && \(includeUnready \|\| clientLoads == 0\)') `
    'The initial recovery condition must not interrupt a Browser Source whose client script has loaded.'
Assert-Condition ($recovery -match 'const bool stalled = includeUnready && connectedClients > 0 && readyClients == 0') `
    'The delayed recovery condition must identify a connected client that never became renderer-ready.'
Assert-Condition ($bridge -match '!obs_source_active\(source\) && !obs_source_showing\(source\)') `
    'Recovery must leave inactive Browser Sources untouched.'
Assert-Condition ($recovery -match 'removeAllQueryItems\(QStringLiteral\("tempest_recovery"\)\)') `
    'Recovery must replace its cache-busting marker instead of growing the URL repeatedly.'
Assert-Condition ($recovery -match 'candidate\.statusPath == QStringLiteral\("/v1/dice-overlay"\)' -and `
                  $recovery -match 'browserUrl\.host\(\) == QStringLiteral\("127\.0\.0\.1"\)' -and `
                  $recovery -match 'browserUrl\.setHost\(QStringLiteral\("localhost"\)\)') `
    'Recovery must isolate Dice from the persistent Studio overlay connection pool.'
Assert-Condition ($recovery -match 'obs_data_set_string\(settings, "url", recoveredUrl\.constData\(\)\)' -and `
                  $recovery -match 'obs_source_update\(source, settings\)') `
    'Recovery must persist the isolated cache-busting URL through the standard Browser Source update path.'
Assert-Condition ($recovery -match 'SetCurrentSceneSourceVisible\(candidate\.name, false\)' -and `
                  $recovery -match 'SetCurrentSceneSourceVisible\(candidate\.name, true\)') `
    'Recovery must wake a suspended transparent Browser Source before refreshing it.'
Assert-Condition ($recovery -match 'obs_source_inc_showing\(source\)' -and `
                  $bridge -match 'obs_source_dec_showing\(source\)') `
    'Recovery must hold a balanced showing reference so OBS cannot suspend the recovered Browser Source.'
Assert-Condition ($recovery -match 'refreshnocache' -and $recovery -match 'obs_property_button_clicked') `
    'Recovery must invoke the Browser Source no-cache refresh after updating its URL.'
Assert-Condition (-not ($recovery -match 'recoveredBrowserSources')) `
    'Recovery must remain health-gated on every Studio reconnect instead of being disabled after one attempt.'
Assert-Condition ($bridge -match 'BrowserRecoveryDelayMs = 5000' -and `
                  $bridge -match 'QTimer::singleShot\(BrowserRecoveryDelayMs' -and `
                  $bridge -match 'RecoverFailedBrowserSources\(false\)' -and `
                  $bridge -match 'QTimer::singleShot\(20000' -and `
                  $bridge -match 'RecoverFailedBrowserSources\(true\)') `
    'Recovery must reserve connected-but-unready replacement for the slower CEF initialization window.'

Write-Host 'Studio Browser Source recovery regression checks passed.'
