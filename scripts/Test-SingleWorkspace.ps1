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

$barHeader = Get-Content -Raw -LiteralPath (Join-Path $SourceRoot 'frontend/widgets/TempestMainframeBar.hpp')
$barSource = Get-Content -Raw -LiteralPath (Join-Path $SourceRoot 'frontend/widgets/TempestMainframeBar.cpp')
$basicSource = Get-Content -Raw -LiteralPath (Join-Path $SourceRoot 'frontend/widgets/OBSBasic.cpp')
$dockSource = Get-Content -Raw -LiteralPath (Join-Path $SourceRoot 'frontend/widgets/OBSBasic_Docks.cpp')

Assert-Condition (-not ($barHeader -match 'WorkspaceRequested|SetCommandWorkspace|commandWorkspaceButton|engineeringWorkspaceButton')) `
    'The top control bar must not expose the retired workspace switch contract.'
Assert-Condition (-not ($barSource -match 'QStringLiteral\("COMMAND"\)|QStringLiteral\("ENGINEERING"\)')) `
    'The top control bar must not render Command or Engineering tabs.'
Assert-Condition ($basicSource -match 'SetTempestWorkspace\(false, true\)') `
    'Startup must always select the conventional single workspace.'
Assert-Condition (-not ($basicSource -match 'TempestWorkspace"\);\s*\n\s*const bool commandWorkspace')) `
    'Startup must not restore the retired Command-mode preference.'
Assert-Condition ($dockSource -match 'RECOVER DEFAULT LAYOUT') `
    'Workspace recovery must use the single-workspace label.'
Assert-Condition (-not ($dockSource -match 'RECOVER COMMAND LAYOUT')) `
    'Workspace recovery must not offer the retired Command layout.'
Assert-Condition (-not ($dockSource -match 'QStringLiteral\("LIVE DOCK REFLOW"\)')) `
    'Workspace Layout must not expose a reflow control that only applied to the retired Command mode.'
Assert-Condition ($dockSource -match 'config_set_string\([^\n]+"TempestWorkspace", "engineering"\)') `
    'Workspace recovery must persist the conventional workspace.'

Write-Host 'Single-workspace regression checks passed.'
