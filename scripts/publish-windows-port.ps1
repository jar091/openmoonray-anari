# Publish the Windows-port branches of the pinned upstream checkouts to a
# GitLab namespace (default: code.it4i.cz/blender).
#
# These are NOT GitLab forks: upstream lives on GitHub, so there is nothing
# for GitLab to fork from. This creates one GitLab project per repository and
# pushes the local `windows-port` branch into it. The GitHub upstream stays
# configured as `origin`, so upstream updates can still be fetched and the
# port branch rebased onto a newer pin.
#
# Two ways to create the projects, both from the command line:
#   1. REST API (default here) - needs a personal access token with the `api`
#      scope in $env:GITLAB_TOKEN. Creates the project explicitly, so the
#      failure mode is a clear error message.
#   2. push-to-create - GitLab creates the project on first push when the
#      pusher may create projects in the namespace. Use -PushToCreate to skip
#      the API entirely (no token needed with SSH auth).
#
# The clones are shallow (`--depth 1` in the fetch scripts); this script
# unshallows them first, otherwise the push is rejected or lands a truncated
# history.
#
# Usage:
#   $env:GITLAB_TOKEN = "<token>"
#   .\scripts\publish-windows-port.ps1                     # HTTPS + API
#   .\scripts\publish-windows-port.ps1 -Ssh -PushToCreate  # SSH, no token
#   .\scripts\publish-windows-port.ps1 -WhatIf             # show what it would do

param(
    [string]$GitLabHost = "code.it4i.cz",
    [string]$Namespace  = "blender",
    [string]$Branch     = "windows-port",
    [switch]$Ssh,           # use git@<host>:<ns>/<repo>.git instead of HTTPS
    [switch]$PushToCreate,  # skip the API; let the first push create the project
    [switch]$WhatIf
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot

# GitLab project name -> local checkout. Every project is prefixed with
# `openmoonray-anari-` so it is obvious in the namespace that it belongs to
# this project rather than being a plain upstream mirror.
#
# The four libraries carry the port itself; openmoonray carries the submodule
# pointers to them, so it is pushed LAST - the commits its gitlinks reference
# must already exist on the server.
#
# Its .gitmodules uses relative urls, rewritten to the prefixed names
# (`../openmoonray-anari-scene_rdl2`, ...): still namespace-relative, so the
# group can be moved or renamed without touching them again.
$repos = [ordered]@{
    "openmoonray-anari-scene_rdl2"    = "external\openmoonray\moonray\scene_rdl2"
    "openmoonray-anari-moonray"       = "external\openmoonray\moonray\moonray"
    "openmoonray-anari-mcrt_denoise"  = "external\openmoonray\moonray\mcrt_denoise"
    "openmoonray-anari-moonshine"     = "external\openmoonray\moonray\moonshine"
    "openmoonray-anari-cmake_modules" = "external\openmoonray\cmake_modules"
    "openmoonray-anari-openmoonray"   = "external\openmoonray"
}

function Get-RemoteUrl {
    param([string]$Name)
    if ($Ssh) { return "git@${GitLabHost}:$Namespace/$Name.git" }
    return "https://$GitLabHost/$Namespace/$Name.git"
}

# --- resolve the namespace id once (API mode only) --------------------------
$namespaceId = $null
if (-not $PushToCreate) {
    if (-not $env:GITLAB_TOKEN) {
        throw "GITLAB_TOKEN is not set. Create a personal access token with the 'api' scope, or re-run with -PushToCreate."
    }
    $headers = @{ "PRIVATE-TOKEN" = $env:GITLAB_TOKEN }
    $found = Invoke-RestMethod -Headers $headers -Method Get `
        -Uri "https://$GitLabHost/api/v4/namespaces?search=$Namespace"
    $ns = $found | Where-Object { $_.full_path -eq $Namespace } | Select-Object -First 1
    if (-not $ns) {
        throw "Namespace '$Namespace' not found (or the token cannot see it). Namespaces visible to this token: $(($found | ForEach-Object { $_.full_path }) -join ', ')"
    }
    $namespaceId = $ns.id
    Write-Host "[ok]   namespace $Namespace -> id $namespaceId"
}

foreach ($name in $repos.Keys) {
    $dir = Join-Path $repoRoot $repos[$name]
    if (-not (Test-Path (Join-Path $dir ".git"))) {
        Write-Host "[skip] $name - no checkout at $dir"
        continue
    }

    $head = git -C $dir rev-parse --abbrev-ref HEAD
    if ($head -ne $Branch) {
        Write-Host "[skip] $name - HEAD is '$head', expected '$Branch'"
        continue
    }

    $url = Get-RemoteUrl $name
    Write-Host ""
    Write-Host "== $name -> $url"

    if ($WhatIf) {
        Write-Host "       would: create project (unless -PushToCreate), unshallow, push $Branch"
        continue
    }

    # 1. create the project
    if (-not $PushToCreate) {
        $headers = @{ "PRIVATE-TOKEN" = $env:GITLAB_TOKEN }
        $exists = $null
        try {
            $encoded = [uri]::EscapeDataString("$Namespace/$name")
            $exists = Invoke-RestMethod -Headers $headers -Method Get `
                -Uri "https://$GitLabHost/api/v4/projects/$encoded"
        } catch { }

        if ($exists) {
            Write-Host "[ok]   project already exists"
        } else {
            $body = @{ name = $name; path = $name; namespace_id = $namespaceId; visibility = "private" }
            $created = Invoke-RestMethod -Headers $headers -Method Post `
                -Uri "https://$GitLabHost/api/v4/projects" -Body $body
            Write-Host "[new]  project created: $($created.web_url)"
        }
    }

    # 2. full history (the fetch scripts clone with --depth 1).
    #    --no-recurse-submodules: git's default is `on-demand`, so fetching
    #    new superproject commits makes it fetch submodules too - and those
    #    already track our GitLab remote, so it tries SSH without an agent
    #    and fails the whole fetch (openmoonray hit exactly this).
    $shallow = git -C $dir rev-parse --is-shallow-repository
    if ($shallow -eq "true") {
        $srcRemote = "origin"
        foreach ($r in (git -C $dir remote)) {
            if ((git -C $dir remote get-url $r) -notmatch [regex]::Escape($GitLabHost)) { $srcRemote = $r; break }
        }
        Write-Host "[run]  git fetch --unshallow $srcRemote (shallow clone cannot be pushed intact)"
        git -C $dir fetch --unshallow --no-recurse-submodules $srcRemote
        if ($LASTEXITCODE -ne 0) { throw "unshallow of $name failed" }
    }

    # 3. normalise the remotes so this checkout looks like a fresh clone of
    #    our project:
    #        origin   -> our GitLab project
    #        upstream -> the GitHub original (still there for rebases)
    #    This matters beyond tidiness: openmoonray's .gitmodules uses
    #    RELATIVE urls, and git resolves those against `origin`. Leaving
    #    origin pointing at GitHub would send `git submodule update` to
    #    github.com/OpenMoonRay/openmoonray-anari-* , which does not exist.
    $remotes = git -C $dir remote
    if ($remotes -contains "origin") {
        $originUrl = git -C $dir remote get-url origin
        if ($originUrl -notmatch [regex]::Escape($GitLabHost)) {
            if ($remotes -notcontains "upstream") {
                git -C $dir remote add upstream $originUrl
                Write-Host "[ok]   kept the original as 'upstream'"
            }
            git -C $dir remote set-url origin $url
        } else {
            git -C $dir remote set-url origin $url
        }
    } else {
        git -C $dir remote add origin $url
    }
    # remove the transitional remote an earlier run of this script created
    if ($remotes -contains "fork") { git -C $dir remote remove fork }

    # The fetch scripts clone with `--depth 1 --branch <tag|branch>`, which
    # makes the clone SINGLE-BRANCH: the configured refspec names exactly
    # that one upstream ref, e.g.
    #     +refs/heads/main:refs/remotes/origin/main
    #     +refs/tags/v2026.29.1:refs/tags/v2026.29.1
    # Our project has neither of those, so after repointing origin every
    # `git fetch origin` fails with "couldn't find remote ref ...". Restore
    # the normal all-branches refspec on both remotes.
    git -C $dir config --unset-all remote.origin.fetch 2>$null
    git -C $dir config --add remote.origin.fetch "+refs/heads/*:refs/remotes/origin/*"
    if ((git -C $dir remote) -contains "upstream") {
        git -C $dir config --unset-all remote.upstream.fetch 2>$null
        git -C $dir config --add remote.upstream.fetch "+refs/heads/*:refs/remotes/upstream/*"
    }

    # 4. push
    git -C $dir push -u origin $Branch
    if ($LASTEXITCODE -ne 0) { throw "push of $name failed" }
    Write-Host "[ok]   pushed $Branch"
}

Write-Host ""
Write-Host "Done. Upstream stays as 'origin' (GitHub); the port branch lives on 'fork'."
Write-Host "Next: point the fetch scripts at these URLs - see docs/WINDOWS_PORT.md."
