$ErrorActionPreference = 'Stop'

$sourcePath = Join-Path $PSScriptRoot '..\frontend\docks\TempestAssetVault.cpp'
$source = Get-Content -LiteralPath $sourcePath -Raw

$checks = [ordered]@{
    'compact 27px asset rows' = 'QListWidget::item { min-height: 27px;'
    'single-line name-first rows' = 'new QListWidgetItem(asset.displayName, assetList)'
    'long names elide in narrow docks' = 'assetList->setTextElideMode(Qt::ElideRight);'
    'uniform list rows' = 'assetList->setUniformItemSizes(true);'
    'full path remains in row tooltip' = '.arg(asset.displayName, kindName, asset.bank, QDir::toNativeSeparators(asset.path))'
    'selected summary omits inline path' = '.arg(asset->displayName, KindName(kind, info), asset->bank, FormatBytes(asset->bytes))'
    'selected path remains available by tooltip' = 'detailLabel->setToolTip(QDir::toNativeSeparators(asset->path));'
    'compact overlay action' = 'QStringLiteral("ADD OVERLAY")'
    'compact sequence action' = 'QStringLiteral("ADD TO SEQUENCE")'
}

foreach ($check in $checks.GetEnumerator()) {
    if (-not $source.Contains($check.Value)) {
        throw "Asset Library compactness check failed: $($check.Key)"
    }
}

if ($source.Contains('QStringLiteral("%1 // %2 // %3\n%4")')) {
    throw 'Asset Library compactness check failed: legacy multi-line list row remains'
}

Write-Host 'Compact Asset Library checks passed.'
