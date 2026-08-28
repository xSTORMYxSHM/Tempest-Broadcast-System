[CmdletBinding(SupportsShouldProcess = $true, ConfirmImpact = 'High')]
param(
    [ValidateSet('Check', 'Prepare', 'Status', 'Validate', 'Apply', 'Cleanup')]
    [string] $Action = 'Check',

    [ValidatePattern('^[0-9]+\.[0-9]+\.[0-9]+$')]
    [string] $TargetVersion,

    [string] $Remote = 'upstream',

    [string] $BaseBranch = 'tempest-main',

    [string] $UpgradeRoot,

    [ValidateSet('Release', 'RelWithDebInfo')]
    [string] $Configuration = 'Release',

    [string] $ReportPath,

    [switch] $NoFetch
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0

function Invoke-External {
    param(
        [Parameter(Mandatory = $true)]
        [string] $FilePath,

        [Parameter(Mandatory = $true)]
        [string[]] $Arguments,

        [switch] $AllowFailure
    )

    $output = @(& $FilePath @Arguments 2>&1)
    $exitCode = $LASTEXITCODE
    if (-not $AllowFailure -and $exitCode -ne 0) {
        throw "Command failed with exit code ${exitCode}: ${FilePath} $($Arguments -join ' ')`n$($output -join "`n")"
    }

    return [pscustomobject]@{
        ExitCode = $exitCode
        Output = $output
    }
}

function Get-GitText {
    param([string[]] $Arguments)

    $result = Invoke-External -FilePath $script:Git -Arguments $Arguments
    return ($result.Output -join "`n").Trim()
}

function Test-GitSuccess {
    param([string[]] $Arguments)

    $result = Invoke-External -FilePath $script:Git -Arguments $Arguments -AllowFailure
    return $result.ExitCode -eq 0
}

function Get-StableTags {
    $records = @()
    $tags = @((Get-GitText -Arguments @('-C', $script:ProjectRoot, 'tag', '--list')) -split "`r?`n")
    foreach ($tag in $tags) {
        if ($tag -match '^[0-9]+\.[0-9]+\.[0-9]+$') {
            $records += [pscustomobject]@{
                Tag = $tag
                Version = [version] $tag
            }
        }
    }

    return @($records | Sort-Object Version -Descending)
}

function Get-CurrentStableBase {
    param(
        [Parameter(Mandatory = $true)]
        [object[]] $StableTags
    )

    foreach ($record in $StableTags) {
        if (Test-GitSuccess -Arguments @(
                '-C',
                $script:ProjectRoot,
                'merge-base',
                '--is-ancestor',
                "refs/tags/$($record.Tag)",
                $BaseBranch
            )) {
            return $record.Tag
        }
    }

    throw "Could not identify a stable numeric OBS tag in the ancestry of ${BaseBranch}."
}

function Get-UpdateContext {
    $stableTags = @(Get-StableTags)
    if ($stableTags.Count -eq 0) {
        throw 'No stable numeric OBS release tags are available locally.'
    }

    $current = Get-CurrentStableBase -StableTags $stableTags
    $target = if ([string]::IsNullOrWhiteSpace($TargetVersion)) {
        $stableTags[0].Tag
    } else {
        $TargetVersion
    }

    if (-not ($stableTags.Tag -contains $target)) {
        throw "OBS ${target} is not available as a stable numeric tag from ${Remote}."
    }
    if (-not (Test-GitSuccess -Arguments @(
                '-C',
                $script:ProjectRoot,
                'merge-base',
                '--is-ancestor',
                "refs/tags/${current}",
                "refs/tags/${target}"
            ))) {
        throw "OBS ${target} does not descend from the current stable base ${current}."
    }

    $upstreamFiles = @(
        (Get-GitText -Arguments @(
            '-C',
            $script:ProjectRoot,
            'diff',
            '--name-only',
            "refs/tags/${current}..refs/tags/${target}"
        )) -split "`r?`n" | Where-Object { -not [string]::IsNullOrWhiteSpace($_) }
    )
    $tempestFiles = @(
        (Get-GitText -Arguments @(
            '-C',
            $script:ProjectRoot,
            'diff',
            '--name-only',
            "refs/tags/${current}..${BaseBranch}"
        )) -split "`r?`n" | Where-Object { -not [string]::IsNullOrWhiteSpace($_) }
    )
    $tempestLookup = [System.Collections.Generic.HashSet[string]]::new(
        [System.StringComparer]::OrdinalIgnoreCase
    )
    foreach ($path in $tempestFiles) {
        $null = $tempestLookup.Add($path)
    }
    $overlap = @($upstreamFiles | Where-Object { $tempestLookup.Contains($_) })

    $commitCount = [int] (Get-GitText -Arguments @(
            '-C',
            $script:ProjectRoot,
            'rev-list',
            '--count',
            "refs/tags/${current}..refs/tags/${target}"
        ))
    $targetCommit = Get-GitText -Arguments @(
        '-C',
        $script:ProjectRoot,
        'rev-parse',
        "refs/tags/${target}^{commit}"
    )

    return [ordered]@{
        current_obs_version = $current
        target_obs_version = $target
        update_available = ([version] $target -gt [version] $current)
        upstream_commit_count = $commitCount
        upstream_changed_files = $upstreamFiles.Count
        tempest_changed_files = $tempestFiles.Count
        overlapping_files = $overlap
        overlapping_file_count = $overlap.Count
        target_commit = $targetCommit
    }
}

function Save-State {
    param(
        [Parameter(Mandatory = $true)]
        [object] $State,

        [Parameter(Mandatory = $true)]
        [string] $Path
    )

    $State | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $Path -Encoding UTF8
}

function Get-StatePath {
    param([string] $Version)
    return Join-Path $UpgradeRoot "obs-${Version}.state.json"
}

function Load-State {
    param([string] $Version)

    $path = Get-StatePath -Version $Version
    if (-not (Test-Path -LiteralPath $path)) {
        throw "No OBS ${Version} integration state exists at ${path}. Run Prepare first."
    }
    return Get-Content -LiteralPath $path -Raw | ConvertFrom-Json
}

function Assert-CleanBaseBranch {
    $branch = Get-GitText -Arguments @('-C', $script:ProjectRoot, 'branch', '--show-current')
    if ($branch -ne $BaseBranch) {
        throw "Run this action from ${BaseBranch}; the current branch is ${branch}."
    }
    $status = Get-GitText -Arguments @(
        '-C',
        $script:ProjectRoot,
        'status',
        '--porcelain=v1',
        '--untracked-files=all'
    )
    if (-not [string]::IsNullOrWhiteSpace($status)) {
        throw "The ${BaseBranch} worktree must be clean before this action.`n${status}"
    }
}

function Find-CMake {
    $command = Get-Command cmake.exe -ErrorAction SilentlyContinue
    if ($command) {
        return $command.Source
    }

    $candidates = @(
        'D:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe',
        'C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe',
        'C:\Program Files\CMake\bin\cmake.exe'
    )
    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath $candidate) {
            return $candidate
        }
    }
    throw 'CMake was not found.'
}

$script:ProjectRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$script:Git = (Get-Command git.exe -ErrorAction Stop).Source

if ([string]::IsNullOrWhiteSpace($UpgradeRoot)) {
    $UpgradeRoot = if (Test-Path -LiteralPath 'G:\') {
        'G:\Tempest Broadcast Upgrades'
    } else {
        Join-Path (Split-Path -Parent $script:ProjectRoot) 'Tempest Broadcast Upgrades'
    }
}
$UpgradeRoot = [System.IO.Path]::GetFullPath($UpgradeRoot)

if (-not $NoFetch -and $Action -in @('Check', 'Prepare')) {
    if ($PSCmdlet.ShouldProcess("${Remote} tags", 'Fetch OBS release tags')) {
        $null = Invoke-External -FilePath $script:Git -Arguments @(
            '-C',
            $script:ProjectRoot,
            'fetch',
            '--tags',
            '--prune',
            $Remote
        )
    }
}

$context = Get-UpdateContext
$resolvedTarget = [string] $context.target_obs_version
$statePath = Get-StatePath -Version $resolvedTarget
$worktreePath = Join-Path $UpgradeRoot "obs-${resolvedTarget}"
$integrationBranch = "obs-update/${resolvedTarget}"

switch ($Action) {
    'Check' {
        $result = [ordered]@{
            action = 'check'
            remote = $Remote
            base_branch = $BaseBranch
            official_release_url = "https://github.com/obsproject/obs-studio/releases/tag/${resolvedTarget}"
            current_obs_version = $context.current_obs_version
            latest_stable_obs_version = $resolvedTarget
            update_available = $context.update_available
            upstream_commit_count = $context.upstream_commit_count
            upstream_changed_files = $context.upstream_changed_files
            tempest_changed_files = $context.tempest_changed_files
            overlapping_file_count = $context.overlapping_file_count
            overlapping_files = $context.overlapping_files
            target_commit = $context.target_commit
        }
        $json = $result | ConvertTo-Json -Depth 8
        if (-not [string]::IsNullOrWhiteSpace($ReportPath)) {
            $reportFullPath = [System.IO.Path]::GetFullPath($ReportPath)
            $reportDirectory = Split-Path -Parent $reportFullPath
            if (-not (Test-Path -LiteralPath $reportDirectory)) {
                New-Item -ItemType Directory -Path $reportDirectory -Force | Out-Null
            }
            Set-Content -LiteralPath $reportFullPath -Value $json -Encoding UTF8
        }
        Write-Output $json
    }

    'Prepare' {
        Assert-CleanBaseBranch
        if (-not $context.update_available) {
            throw "${BaseBranch} already contains OBS $($context.current_obs_version); ${resolvedTarget} is not newer."
        }
        if (Test-Path -LiteralPath $statePath) {
            throw "An integration state already exists: ${statePath}"
        }
        if (Test-Path -LiteralPath $worktreePath) {
            throw "The integration worktree already exists: ${worktreePath}"
        }
        if (Test-GitSuccess -Arguments @(
                '-C',
                $script:ProjectRoot,
                'show-ref',
                '--verify',
                '--quiet',
                "refs/heads/${integrationBranch}"
            )) {
            throw "The integration branch already exists: ${integrationBranch}"
        }

        $baseCommit = Get-GitText -Arguments @('-C', $script:ProjectRoot, 'rev-parse', $BaseBranch)
        if (-not $PSCmdlet.ShouldProcess(
                $worktreePath,
                "Create ${integrationBranch} and merge stable OBS ${resolvedTarget}"
            )) {
            Write-Output ([ordered]@{
                    action = 'prepare'
                    what_if = $true
                    target = $resolvedTarget
                    branch = $integrationBranch
                    worktree = $worktreePath
                    predicted_overlap_count = $context.overlapping_file_count
                    predicted_overlap_files = $context.overlapping_files
                } | ConvertTo-Json -Depth 8)
            break
        }

        New-Item -ItemType Directory -Path $UpgradeRoot -Force | Out-Null
        $null = Invoke-External -FilePath $script:Git -Arguments @(
            '-C',
            $script:ProjectRoot,
            'worktree',
            'add',
            '-b',
            $integrationBranch,
            $worktreePath,
            $baseCommit
        )

        $merge = Invoke-External -FilePath $script:Git -Arguments @(
            '-C',
            $worktreePath,
            'merge',
            '--no-ff',
            '-m',
            "Merge OBS Studio ${resolvedTarget} into Tempest Broadcast",
            "refs/tags/${resolvedTarget}"
        ) -AllowFailure
        $unmerged = @(
            (Get-GitText -Arguments @(
                '-C',
                $worktreePath,
                'diff',
                '--name-only',
                '--diff-filter=U'
            )) -split "`r?`n" | Where-Object { -not [string]::IsNullOrWhiteSpace($_) }
        )

        $state = [ordered]@{
            schema_version = 1
            status = if ($unmerged.Count -gt 0) { 'conflicts' } else { 'prepared' }
            remote = $Remote
            base_branch = $BaseBranch
            base_commit = $baseCommit
            previous_obs_version = $context.current_obs_version
            target_obs_version = $resolvedTarget
            target_commit = $context.target_commit
            integration_branch = $integrationBranch
            worktree_path = $worktreePath
            predicted_overlap_files = $context.overlapping_files
            conflicted_files = $unmerged
            created_utc = [DateTime]::UtcNow.ToString('o')
        }
        Save-State -State $state -Path $statePath

        if ($unmerged.Count -gt 0) {
            Write-Output ($state | ConvertTo-Json -Depth 8)
            throw "OBS ${resolvedTarget} has merge conflicts. Resolve them in ${worktreePath}, commit the merge, then run Validate."
        }
        if ($merge.ExitCode -ne 0) {
            throw "The OBS merge failed without reporting normal file conflicts.`n$($merge.Output -join "`n")"
        }

        $null = Invoke-External -FilePath $script:Git -Arguments @(
            '-C',
            $worktreePath,
            'submodule',
            'update',
            '--init',
            '--recursive'
        )
        Write-Output ($state | ConvertTo-Json -Depth 8)
    }

    'Status' {
        $state = Load-State -Version $resolvedTarget
        $branchCommit = if (Test-GitSuccess -Arguments @(
                '-C',
                $script:ProjectRoot,
                'show-ref',
                '--verify',
                '--quiet',
                "refs/heads/$($state.integration_branch)"
            )) {
            Get-GitText -Arguments @(
                '-C',
                $script:ProjectRoot,
                'rev-parse',
                $state.integration_branch
            )
        } else {
            $null
        }
        Write-Output ([ordered]@{
                state = $state
                state_file = $statePath
                worktree_exists = (Test-Path -LiteralPath $state.worktree_path)
                integration_branch_commit = $branchCommit
                base_branch_commit = Get-GitText -Arguments @(
                    '-C',
                    $script:ProjectRoot,
                    'rev-parse',
                    $BaseBranch
                )
            } | ConvertTo-Json -Depth 10)
    }

    'Validate' {
        $state = Load-State -Version $resolvedTarget
        if (-not (Test-Path -LiteralPath $state.worktree_path)) {
            throw "The integration worktree is missing: $($state.worktree_path)"
        }
        $unmerged = Get-GitText -Arguments @(
            '-C',
            $state.worktree_path,
            'diff',
            '--name-only',
            '--diff-filter=U'
        )
        if (-not [string]::IsNullOrWhiteSpace($unmerged)) {
            throw "Resolve and commit every merge conflict before validation.`n${unmerged}"
        }
        $status = Get-GitText -Arguments @(
            '-C',
            $state.worktree_path,
            'status',
            '--porcelain=v1',
            '--untracked-files=all'
        )
        if (-not [string]::IsNullOrWhiteSpace($status)) {
            throw "Commit the resolved integration before validation.`n${status}"
        }
        if (-not (Test-GitSuccess -Arguments @(
                    '-C',
                    $state.worktree_path,
                    'merge-base',
                    '--is-ancestor',
                    "refs/tags/${resolvedTarget}",
                    'HEAD'
                ))) {
            throw "The integration branch does not contain OBS ${resolvedTarget}."
        }

        $cmake = Find-CMake
        $ctest = Join-Path (Split-Path -Parent $cmake) 'ctest.exe'
        $buildDirectory = Join-Path $state.worktree_path 'build_x64'
        if (-not $PSCmdlet.ShouldProcess(
                $state.worktree_path,
                "Configure, build, and test OBS ${resolvedTarget} integration"
            )) {
            Write-Output ([ordered]@{
                    action = 'validate'
                    what_if = $true
                    target = $resolvedTarget
                    worktree = $state.worktree_path
                    build_directory = $buildDirectory
                } | ConvertTo-Json -Depth 6)
            break
        }

        $null = Invoke-External -FilePath $cmake -Arguments @(
            '-S',
            $state.worktree_path,
            '-B',
            $buildDirectory,
            '-DENABLE_WHATSNEW=OFF'
        )
        $null = Invoke-External -FilePath $cmake -Arguments @(
            '--build',
            $buildDirectory,
            '--config',
            $Configuration,
            '--parallel',
            '1'
        )
        if (-not (Test-Path -LiteralPath $ctest)) {
            throw "CTest was not found beside CMake: ${ctest}"
        }
        $testResult = Invoke-External -FilePath $ctest -Arguments @(
            '--test-dir',
            $buildDirectory,
            '-C',
            $Configuration,
            '--output-on-failure'
        )

        $binary = Join-Path $buildDirectory "rundir\${Configuration}\bin\64bit\tempest-broadcast-system.exe"
        if (-not (Test-Path -LiteralPath $binary)) {
            throw "Validation did not produce the expected executable: ${binary}"
        }
        $binaryInfo = Get-Item -LiteralPath $binary
        $validationCommit = Get-GitText -Arguments @('-C', $state.worktree_path, 'rev-parse', 'HEAD')
        $state.status = 'validated'
        $state | Add-Member -NotePropertyName validation_commit -NotePropertyValue $validationCommit -Force
        $state | Add-Member -NotePropertyName validated_utc -NotePropertyValue ([DateTime]::UtcNow.ToString('o')) -Force
        $state | Add-Member -NotePropertyName configuration -NotePropertyValue $Configuration -Force
        $state | Add-Member -NotePropertyName binary_path -NotePropertyValue $binary -Force
        $state | Add-Member -NotePropertyName binary_sha256 -NotePropertyValue ((Get-FileHash -LiteralPath $binary -Algorithm SHA256).Hash.ToLowerInvariant()) -Force
        $state | Add-Member -NotePropertyName product_version -NotePropertyValue $binaryInfo.VersionInfo.ProductVersion -Force
        $state | Add-Member -NotePropertyName ctest_output -NotePropertyValue ($testResult.Output -join "`n") -Force
        Save-State -State $state -Path $statePath
        Write-Output ($state | ConvertTo-Json -Depth 10)
    }

    'Apply' {
        Assert-CleanBaseBranch
        $state = Load-State -Version $resolvedTarget
        if ($state.status -ne 'validated') {
            throw "OBS ${resolvedTarget} must pass Validate before Apply. Current status: $($state.status)"
        }
        $baseCommit = Get-GitText -Arguments @('-C', $script:ProjectRoot, 'rev-parse', $BaseBranch)
        if ($baseCommit -ne $state.base_commit) {
            throw "${BaseBranch} changed after Prepare. Recreate or rebase the integration before applying it."
        }
        $integrationCommit = Get-GitText -Arguments @(
            '-C',
            $script:ProjectRoot,
            'rev-parse',
            $state.integration_branch
        )
        if ($integrationCommit -ne $state.validation_commit) {
            throw 'The integration branch changed after validation. Run Validate again.'
        }
        if (-not $PSCmdlet.ShouldProcess(
                $BaseBranch,
                "Fast-forward to validated OBS ${resolvedTarget} integration ${integrationCommit}"
            )) {
            Write-Output ([ordered]@{
                    action = 'apply'
                    what_if = $true
                    base_branch = $BaseBranch
                    integration_branch = $state.integration_branch
                    integration_commit = $integrationCommit
                } | ConvertTo-Json -Depth 6)
            break
        }

        $null = Invoke-External -FilePath $script:Git -Arguments @(
            '-C',
            $script:ProjectRoot,
            'merge',
            '--ff-only',
            $state.integration_branch
        )
        $null = Invoke-External -FilePath $script:Git -Arguments @(
            '-C',
            $script:ProjectRoot,
            'submodule',
            'sync',
            '--recursive'
        )
        $null = Invoke-External -FilePath $script:Git -Arguments @(
            '-C',
            $script:ProjectRoot,
            'submodule',
            'update',
            '--init',
            '--recursive'
        )
        $state.status = 'applied'
        $state | Add-Member -NotePropertyName applied_utc -NotePropertyValue ([DateTime]::UtcNow.ToString('o')) -Force
        $state | Add-Member -NotePropertyName applied_commit -NotePropertyValue $integrationCommit -Force
        Save-State -State $state -Path $statePath
        Write-Output ($state | ConvertTo-Json -Depth 10)
    }

    'Cleanup' {
        Assert-CleanBaseBranch
        $state = Load-State -Version $resolvedTarget
        if ($state.status -ne 'applied') {
            throw "Cleanup is allowed only after Apply. Current status: $($state.status)"
        }
        if (-not (Test-GitSuccess -Arguments @(
                    '-C',
                    $script:ProjectRoot,
                    'merge-base',
                    '--is-ancestor',
                    $state.integration_branch,
                    $BaseBranch
                ))) {
            throw 'The integration branch is not contained in the base branch.'
        }
        if (-not $PSCmdlet.ShouldProcess(
                $state.worktree_path,
                "Remove completed worktree and branch $($state.integration_branch)"
            )) {
            break
        }

        if (Test-Path -LiteralPath $state.worktree_path) {
            $null = Invoke-External -FilePath $script:Git -Arguments @(
                '-C',
                $script:ProjectRoot,
                'worktree',
                'remove',
                $state.worktree_path
            )
        }
        $null = Invoke-External -FilePath $script:Git -Arguments @(
            '-C',
            $script:ProjectRoot,
            'branch',
            '--delete',
            $state.integration_branch
        )
        Remove-Item -LiteralPath $statePath -Force
        Write-Output "Removed completed OBS ${resolvedTarget} integration workspace."
    }
}
