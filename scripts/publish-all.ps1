#requires -Version 5.1
# Commit and push every repository this project owns, innermost first.
#
# The tree is a superproject over submodules that are themselves
# superprojects, and the pinned commit *is* the dependency (DEPENDENCIES.md).
# So the order is not a convenience, it is a correctness requirement:
#
#   1. cmake_modules, scene_rdl2, moonray, mcrt_denoise, moonshine
#   2. external/openmoonray        - records the commits from step 1
#   3. the superproject            - records the commit from step 2
#
# A parent must be pushed *after* its children, or it points at commits that
# only exist on this machine and nobody else can check the tree out.
#
# Every repository is handled on its own and confirmed separately, so an https
# remote that asks for credentials asks once per repository and the run can be
# interrupted and resumed at any point - rerunning skips whatever is already
# done.
#
# Usage:
#   .\scripts\publish-all.ps1 ["message"] [--dry-run] [--yes] [--no-push]
#   .\scripts\publish-all.ps1 -m "message" ...
#
#   Both spellings of every switch are accepted, so the same command line works
#   here and in publish-all.sh:  --dry-run / -DryRun, --yes / -Yes / -y,
#   --no-push / -NoPush.
#
#   message    one commit message, used for EVERY repository - the commits are
#              the same change set recorded at different levels of the tree, so
#              a message per level would only make the history harder to read.
#              Asked for once at the start when it is not given on the command
#              line (default: "update Windows").
#   --dry-run  show what would happen, touch nothing
#   --yes      do not ask before each commit/push (still stops on any error)
#   --no-push  commit only
#
# Not touched: external/anari-sdk and external/vcpkg are unmodified upstream
# checkouts, and external/pynari is a fork we do not currently change. They are
# reported if they are dirty, never committed.

# Arguments are parsed by hand rather than with a param() block so that the
# bash spellings (--dry-run, --yes, --no-push) reach the script verbatim.
Set-StrictMode -Version Latest
# Stop, not Continue: a scripting mistake here must never be a warning that
# scrolls past while the run carries on and quietly skips a push. The two
# functions that silence git's stderr lower this for themselves.
$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent $PSScriptRoot
Set-Location -LiteralPath $repoRoot

$message   = ''
$dryRun    = $false
$assumeYes = $false
$doPush    = $true

function Show-Help {
    # Print the leading comment block, so the help can never go stale against a
    # fixed line range.
    $lines = Get-Content -LiteralPath $PSCommandPath
    foreach ($line in $lines[1..($lines.Count - 1)]) {
        if ($line -notmatch '^#') { break }
        Write-Host ($line -replace '^# ?', '')
    }
}

$i = 0
while ($i -lt $args.Count) {
    $a = [string]$args[$i]
    switch -Regex ($a) {
        '^(-m|-[Mm]essage)$' {
            $i++
            if ($i -lt $args.Count) { $message = [string]$args[$i] }
            $i++
            break
        }
        '^(--dry-run|-DryRun)$'  { $dryRun = $true;    $i++; break }
        '^(--yes|-Yes|-y)$'      { $assumeYes = $true; $i++; break }
        '^(--no-push|-NoPush)$'  { $doPush = $false;   $i++; break }
        '^(-h|--help|-Help|-\?)$' { Show-Help; exit 0 }
        '^-' {
            [Console]::Error.WriteLine("unknown option: $a")
            exit 2
        }
        default {
            if ($message -ne '') {
                [Console]::Error.WriteLine("more than one commit message given: '$message' and '$a'")
                exit 2
            }
            $message = $a
            $i++
        }
    }
}

$defaultMessage = 'update Windows'

# path : branch  - in publication order, children before parents.
$repos = @(
    @{ Path = 'external/openmoonray/cmake_modules';         Branch = 'windows-port' }
    @{ Path = 'external/openmoonray/moonray/scene_rdl2';    Branch = 'windows-port' }
    @{ Path = 'external/openmoonray/moonray/moonray';       Branch = 'windows-port' }
    @{ Path = 'external/openmoonray/moonray/mcrt_denoise';  Branch = 'windows-port' }
    @{ Path = 'external/openmoonray/moonray/moonshine';     Branch = 'windows-port' }
    @{ Path = 'external/openmoonray';                       Branch = 'windows-port' }
    @{ Path = '.';                                          Branch = 'main' }
)

# Repositories we deliberately never commit into.
$readonlyRepos = @('external/anari-sdk', 'external/vcpkg', 'external/pynari')

# The paths above, for deciding whether a dirty submodule is one this script is
# responsible for finishing or one it has nothing to do with.
$script:managedPaths = @($repos | Where-Object { $_.Path -ne '.' } | ForEach-Object { $_.Path })

$script:failed = $false

function Say  { param([string]$Text) Write-Host $Text }
function Note { param([string]$Text) Write-Host $Text -ForegroundColor DarkGray }
function Warn { param([string]$Text) Write-Host $Text -ForegroundColor Yellow }
function Fail { param([string]$Text) Write-Host $Text -ForegroundColor Red }
function Ok   { param([string]$Text) Write-Host $Text -ForegroundColor Green }

function Write-Indented {
    param([string[]]$Lines)
    foreach ($l in $Lines) { Write-Host ("    " + $l) }
}

function Confirm-Step {
    param([string]$Prompt)
    if ($assumeYes) { return $true }
    if (-not [Environment]::UserInteractive) {
        Fail "no controlling terminal to ask on - rerun interactively, or pass --yes"
        return $false
    }
    try {
        $answer = Read-Host "$Prompt [y/N]"
    } catch {
        Fail "no controlling terminal to ask on - rerun interactively, or pass --yes"
        return $false
    }
    return ($answer -match '^(y|yes)$')
}

# The git arguments are always passed as one explicit array: tokens like -C,
# -A and --porcelain would otherwise be parsed as parameters of the function
# itself rather than handed to git.

# Run a git command for real, or describe it in a dry run. Returns $true on
# success. stderr is left alone so git's own diagnostics reach the terminal.
function Invoke-Git {
    param([string]$Path, [string[]]$GitArgs)
    if ($dryRun) {
        Note ("  would run: git -C $Path " + ($GitArgs -join ' '))
        return $true
    }
    # git's stdout has to be taken out of the output stream, or it is returned
    # to the caller *alongside* the boolean below. `if (-not (Invoke-Git ...))`
    # would then test a multi-element array, which is always $true - so every
    # failure would read as success. That is how a failed `git commit` once got
    # reported as "committed".
    & git -C $Path @GitArgs | ForEach-Object { Write-Host ("    " + $_) }
    return ($LASTEXITCODE -eq 0)
}

# Capture a git command's stdout as lines, quietly. Returns @() on failure.
# The leading comma keeps an empty result an empty array: a bare `return @()`
# is unrolled by the pipeline and reaches the caller as $null.
function Get-GitOut {
    param([string]$Path, [string[]]$GitArgs)
    # Function-scoped: 2>$null on a native command is an error record in
    # Windows PowerShell 5.1 and would throw under the script's 'Stop'.
    $ErrorActionPreference = 'Continue'
    $out = & git -C $Path @GitArgs 2>$null
    if ($LASTEXITCODE -ne 0) { return , @() }
    if ($null -eq $out) { return , @() }
    return , @($out)
}

function Test-GitOk {
    param([string]$Path, [string[]]$GitArgs)
    $ErrorActionPreference = 'Continue'
    & git -C $Path @GitArgs 2>$null | Out-Null
    return ($LASTEXITCODE -eq 0)
}

# git lists a submodule whose *worktree* is dirty as well as one whose recorded
# commit moved. Only the latter is something the parent can commit, and a parent
# must not be committed while a child is still dirty - that is what produces a
# pin nobody can resolve.
#
# The two cases are only told apart by `status --short`, which writes " m path"
# for a dirty worktree and " M path" for a moved commit. `status --porcelain`
# collapses both to " M ", so reading it here (as publish-all.sh does) can never
# detect anything.
#
# Only the submodules this script publishes can block it. The tree also carries
# submodules nobody here commits into (moonray_gui, rats, ...); those are often
# dirty for good reasons, their recorded pin never moves, and `git add -A` in
# the parent records nothing for them - so they are reported, not fatal.
#
# And a managed child only blocks its parent if it has work of its own. Left
# unqualified, external/openmoonray blocks the superproject forever: it is dirty
# purely because of the unmanaged submodules inside it, which nothing here will
# ever clean. Hence the mutual recursion with Test-RepoHasWork below.
#
# Sorts one repository's `status --short` into three:
#   Commitable  lines `git add -A` here would actually record
#   Blocking    managed submodules that must be finished before this one
#   Unmanaged   dirty submodules this script never publishes
#   Settled     managed submodules already finished, dirty only below
function Get-RepoStatus {
    param([string]$Path)
    $commitable = @()
    $blocking   = @()
    $unmanaged  = @()
    $settled    = @()
    foreach ($line in (Get-GitOut $Path @('status', '--short'))) {
        # -cmatch, not -match: the default comparison is case-insensitive and
        # would treat the harmless " M " (pin moved) as a dirty worktree.
        if ($line -cmatch '^.m (.+)$') {
            $sub = $Matches[1].Trim() -replace '\\', '/'
            if ($Path -eq '.') { $full = $sub } else { $full = "$Path/$sub" }
            if ($script:managedPaths -notcontains $full) {
                $unmanaged += $full
            } elseif (Test-RepoHasWork -Path $full) {
                $blocking += $full
            } else {
                $settled += $full
            }
            continue
        }
        $commitable += $line
    }
    return [pscustomobject]@{
        Commitable = @($commitable)
        Blocking   = @($blocking)
        Unmanaged  = @($unmanaged)
        Settled    = @($settled)
    }
}

function Test-RepoHasWork {
    param([string]$Path)
    $s = Get-RepoStatus -Path $Path
    return (($s.Commitable.Count -gt 0) -or ($s.Blocking.Count -gt 0))
}

function Publish-Repo {
    param([string]$Path, [string]$Branch)

    $name = $Path
    if ($name -eq '.') { $name = 'openmoonray-anari (superproject)' }

    Write-Host ''
    Write-Host "=== $name ===" -ForegroundColor White

    if (-not (Test-Path -LiteralPath (Join-Path $Path '.git'))) {
        Warn "  not a checkout - skipped"
        return $true
    }

    $remote = (Get-GitOut $Path @('remote', 'get-url', 'origin')) -join ''
    if ($remote -eq '') { $remote = '(none)' }
    Note "  origin: $remote"
    Note "  branch: $Branch"

    # --- refuse to publish a parent whose children are not finished ---------
    $st = Get-RepoStatus -Path $Path
    if ($st.Unmanaged.Count -gt 0) {
        Note ("  dirty, not published by this script (pin unchanged): " +
              ($st.Unmanaged -join ', '))
    }
    if ($st.Settled.Count -gt 0) {
        Note ("  already finished, still dirty only below it: " +
              ($st.Settled -join ', '))
    }
    if ($st.Blocking.Count -gt 0) {
        if ($dryRun) {
            # A dry run committed nothing, so every child below is still dirty
            # and this would stop at the first parent and show nothing further.
            # Report the whole tree instead - that is the point of a dry run.
            Note "  (dry run: children are still uncommitted; guard not applied)"
        } else {
            Fail "  a submodule this script publishes still has uncommitted changes:"
            Write-Indented $st.Blocking
            Fail "  finish that repository first (this script goes children-first)."
            $script:failed = $true
            return $false
        }
    }

    # --- commit -------------------------------------------------------------
    # Only Commitable, not the raw status: a repository whose sole "change" is a
    # dirty unmanaged submodule has nothing to stage, and `git commit` would
    # fail with "nothing to commit".
    if ($st.Commitable.Count -gt 0) {
        Say "  changes to commit:"
        Write-Indented $st.Commitable

        if (Confirm-Step "  commit these changes?") {
            # -A so a changed .gitmodules and moved submodule pins are staged
            # together with the file edits; they describe one state.
            if (-not (Invoke-Git $Path @('add', '-A'))) { $script:failed = $true; return $false }
            if (-not (Invoke-Git $Path @('commit', '-m', $script:message))) { $script:failed = $true; return $false }
            if ($dryRun) {
                # Nothing was committed, so the parents below are still
                # reported against the current pins rather than the ones this
                # commit would create.
                Note "  (dry run: parent pins below are shown unchanged)"
            } else {
                Ok "  committed"
            }
        } else {
            Warn "  commit skipped - nothing will be pushed for this repository"
            return $true
        }
    } elseif (($st.Unmanaged.Count + $st.Settled.Count) -gt 0) {
        Note "  nothing this script can record here"
    } else {
        Note "  working tree clean"
    }

    if (-not $doPush) {
        Note "  --no-push: not pushing"
        return $true
    }

    # --- push ---------------------------------------------------------------
    # Submodules are checked out detached, which is normal; push the commit at
    # HEAD to the named branch rather than trying to attach a local branch.
    $head = (Get-GitOut $Path @('rev-parse', 'HEAD')) -join ''
    $remoteRef = "refs/remotes/origin/$Branch"

    if (Test-GitOk $Path @('rev-parse', '--verify', '--quiet', $remoteRef)) {
        if (((Get-GitOut $Path @('rev-parse', $remoteRef)) -join '') -eq $head) {
            Note "  origin/$Branch is already at this commit - nothing to push"
            return $true
        }
        # A non-fast-forward here means the branch moved elsewhere; pushing
        # would either be rejected or (with --force) drop someone's work.
        if (-not (Test-GitOk $Path @('merge-base', '--is-ancestor', $remoteRef, 'HEAD'))) {
            Fail "  origin/$Branch is NOT an ancestor of HEAD - this would not"
            Fail "  fast-forward. Fetch and rebase/merge before publishing."
            $script:failed = $true
            return $false
        }
        $ahead = (Get-GitOut $Path @('rev-list', '--count', "$remoteRef..HEAD")) -join ''
        Say "  $ahead commit(s) to push:"
        Write-Indented (Get-GitOut $Path @('log', '--oneline', "$remoteRef..HEAD"))
    } else {
        Warn "  no origin/$Branch known locally (never fetched?) - will push HEAD to it"
    }

    # ${Branch}, not $Branch: '?' is a legal character in a PowerShell variable
    # name, so "$Branch?" reads as a variable called 'Branch?'.
    if (Confirm-Step "  push to origin/${Branch}?") {
        Note "  (an https remote will ask for credentials now)"
        # git's credential prompt opens the console on its own; nothing is
        # redirected here so it can.
        if (-not (Invoke-Git $Path @('push', 'origin', "HEAD:refs/heads/$Branch"))) {
            Fail "  push failed"
            $script:failed = $true
            return $false
        }
        if (-not $dryRun) { Ok "  pushed" }
    } else {
        Warn "  push skipped"
    }
    return $true
}

Write-Host "publish-all" -ForegroundColor White -NoNewline
Write-Host "  root: $repoRoot"
if ($dryRun) { Warn "dry run - nothing will be changed" }

# Ask for the message once, and only when there is in fact something to
# commit somewhere in the tree.
if ($message -eq '') {
    $anythingDirty = $false
    foreach ($r in $repos) {
        if (-not (Test-Path -LiteralPath (Join-Path $r.Path '.git'))) { continue }
        if (Test-RepoHasWork -Path $r.Path) {
            $anythingDirty = $true
            break
        }
    }
    if ($anythingDirty -and (-not $assumeYes) -and [Environment]::UserInteractive) {
        try {
            $message = Read-Host "commit message for every repository [$defaultMessage]"
        } catch {
            $message = ''
        }
    }
    if ($message -eq '') { $message = $defaultMessage }
}
Note "commit message: $message"

foreach ($r in $repos) {
    try {
        if (-not (Publish-Repo -Path $r.Path -Branch $r.Branch)) { break }
    } catch {
        Fail "  unexpected error in $($r.Path): $($_.Exception.Message)"
        Fail "  $($_.InvocationInfo.PositionMessage)"
        $script:failed = $true
        break
    }
}

# --- report the repositories this script never writes to --------------------
foreach ($path in $readonlyRepos) {
    if (-not (Test-Path -LiteralPath (Join-Path $path '.git'))) { continue }
    $st = Get-GitOut $path @('status', '--porcelain')
    if ($st.Count -gt 0) {
        Write-Host ''
        Warn "$path has local changes and is NOT published by this script"
        Write-Indented (Get-GitOut $path @('status', '--short'))
    }
}

Write-Host ''
if ($script:failed) {
    Fail "publish-all stopped with errors - nothing after the failing repository was touched."
    exit 1
}

# --- final check: no dangling pins ------------------------------------------
# Every recorded submodule commit has to exist on its remote, or a fresh clone
# cannot be checked out. Verifying that is cheap and it is exactly the mistake
# this ordering exists to prevent.
if ((-not $dryRun) -and $doPush) {
    $dangling = $false
    foreach ($r in $repos) {
        if ($r.Path -eq '.') { continue }
        if (-not (Test-Path -LiteralPath (Join-Path $r.Path '.git'))) { continue }
        $head = (Get-GitOut $r.Path @('rev-parse', 'HEAD')) -join ''
        $ref  = "refs/remotes/origin/$($r.Branch)"
        if ((Test-GitOk $r.Path @('rev-parse', '--verify', '--quiet', $ref)) -and
            (-not (Test-GitOk $r.Path @('merge-base', '--is-ancestor', $head, $ref)))) {
            $short = (Get-GitOut $r.Path @('rev-parse', '--short', 'HEAD')) -join ''
            Fail "$($r.Path): HEAD ($short) is not on origin/$($r.Branch)"
            $dangling = $true
        }
    }
    if ($dangling) {
        Fail "some recorded pins are not published - a fresh clone would fail."
        exit 1
    }
}

Ok "publish-all done."
# '+' is a checkout that does not match the recorded pin and 'U' a conflict -
# both are real problems. '-' only means "not initialized", which on Windows is
# the intended state for the submodules fetch-dependencies.ps1 leaves alone.
Say "Verify with:  git submodule status --recursive | Select-String '^[+U]'"
