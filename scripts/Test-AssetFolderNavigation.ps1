$ErrorActionPreference = 'Stop'

$sourcePath = Join-Path $PSScriptRoot '..\frontend\docks\TempestAssetVault.cpp'
$headerPath = Join-Path $PSScriptRoot '..\frontend\docks\TempestAssetVault.hpp'
$source = Get-Content -LiteralPath $sourcePath -Raw
$header = Get-Content -LiteralPath $headerPath -Raw

$checks = [ordered]@{
    'assets remember their indexed root' = 'QString root;'
    'all-folders navigation exists' = 'QStringLiteral("ALL FOLDERS")'
    'folder selection rebuilds the list' = 'connect(rootsSelector, &QComboBox::currentIndexChanged, this, &TempestAssetVault::RebuildAssetList);'
    'selected folder filters assets' = 'asset.root.compare(root, Qt::CaseInsensitive) != 0'
    'tag filter is explicitly optional' = 'QStringLiteral("ALL TAGS")'
    'unassigned is presented as no tag' = 'QStringLiteral("NO TAG")'
    'new folder clears search' = 'searchField->clear();'
    'new folder clears tag filter' = 'bankFilter->setCurrentIndex(0);'
    'user assets default to unassigned' = 'QString defaultBank = QStringLiteral("UNASSIGNED");'
    'automatic tags are limited to managed assets' = 'roots[scanRootIndex].compare(managedRoot, Qt::CaseInsensitive) == 0'
    'assets sort by folder before name' = 'const int rootOrder = a.root.compare(b.root, Qt::CaseInsensitive);'
    'visible result count is reported' = 'QStringLiteral("SHOWING %1 OF %2 ASSETS")'
    'empty folder has an explicit state' = 'QStringLiteral("NO SUPPORTED ASSETS FOUND IN THIS FOLDER")'
}

foreach ($check in $checks.GetEnumerator()) {
    $haystack = if ($check.Key -eq 'assets remember their indexed root') { $header } else { $source }
    if (-not $haystack.Contains($check.Value)) {
        throw "Asset folder navigation check failed: $($check.Key)"
    }
}

Write-Host 'Asset folder navigation checks passed.'
