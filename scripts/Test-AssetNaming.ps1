$ErrorActionPreference = 'Stop'

$sourcePath = Join-Path $PSScriptRoot '..\frontend\docks\TempestAssetVault.cpp'
$source = Get-Content -LiteralPath $sourcePath -Raw

$checks = [ordered]@{
    'chat HUD has a stable product name' = 'return QStringLiteral("Chat Panel");'
    'now playing has a stable product name' = 'return QStringLiteral("Now Playing");'
    'stream frame has a stable product name' = 'return QStringLiteral("Stream Frame");'
    'info plate has a stable product name' = 'return QStringLiteral("Info Plate");'
    'custom UUID HUDs may use their authored heading' = 'const bool anonymousHud = hudAsset'
    'search splits input into independent words' = 'const QStringList searchTerms = search.split'
    'search includes original filename' = '.arg(asset.name, asset.displayName, asset.path, kindName, asset.bank);'
    'every search word must match' = 'std::all_of(searchTerms.cbegin(), searchTerms.cend()'
}

foreach ($check in $checks.GetEnumerator()) {
    if (-not $source.Contains($check.Value)) {
        throw "Asset naming check failed: $($check.Key)"
    }
}

Write-Host 'Asset naming and search checks passed.'
