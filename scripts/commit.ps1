param(
    [Parameter( Mandatory = $true )]
    [string] $Message ,

    [string[]] $Body = @() ,

    [switch] $Push ,

    [switch] $StageAll ,

    [string] $Author = "Lucas Zhang <lucaszhang1118@gmail.com>"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$GitExe = "C:\Program Files\Git\bin\git.exe"
if ( -not ( Test-Path -LiteralPath $GitExe ) ) {
    $GitExe = "git"
}

function Invoke-Git {
    param( [Parameter( ValueFromRemainingArguments = $true )] [string[]] $GitArgs )
    & $GitExe @GitArgs
    if ( $LASTEXITCODE -ne 0 ) {
        throw "git $($GitArgs -join ' ') failed with exit code $LASTEXITCODE"
    }
}

$repoRoot = Split-Path -Parent $PSScriptRoot
Push-Location -LiteralPath $repoRoot

try {
    if ( $StageAll ) {
        Invoke-Git add -A
    }

    $null = & $GitExe diff --cached --quiet 2>$null
    $hasStaged = $LASTEXITCODE -ne 0

    if ( -not $hasStaged ) {
        $untracked = & $GitExe ls-files --others --exclude-standard
        if ( -not $untracked ) {
            Write-Host "Nothing to commit."
            exit 0
        }

        Write-Host "No staged changes. Re-run with -StageAll or run: git add -A"
        exit 1
    }

    $tree = ( & $GitExe write-tree ).Trim()
    if ( -not $tree ) {
        throw "git write-tree returned empty tree"
    }

    $parents = @()
    $head = & $GitExe rev-parse --verify HEAD 2>$null
    if ( $LASTEXITCODE -eq 0 -and $head ) {
        $parents = @( "-p" , $head.Trim() )
    }

    $fullMessage = $Message
    foreach ( $line in $Body ) {
        $fullMessage += "`n`n$line"
    }

    if ( $Author -match '^(.+?) <(.+)>$' ) {
        $authorName = $Matches[1]
        $authorEmail = $Matches[2]
    }
    else {
        throw "Author must be in the form: Name <email@example.com>"
    }

    $previousAuthorName = $env:GIT_AUTHOR_NAME
    $previousAuthorEmail = $env:GIT_AUTHOR_EMAIL
    $previousCommitterName = $env:GIT_COMMITTER_NAME
    $previousCommitterEmail = $env:GIT_COMMITTER_EMAIL

    $env:GIT_AUTHOR_NAME = $authorName
    $env:GIT_AUTHOR_EMAIL = $authorEmail
    $env:GIT_COMMITTER_NAME = $authorName
    $env:GIT_COMMITTER_EMAIL = $authorEmail

    try {
        $commitArgs = @( "commit-tree" , $tree ) + $parents + @( "-m" , $fullMessage )
        $commit = ( & $GitExe @commitArgs ).Trim()
    }
    finally {
        $env:GIT_AUTHOR_NAME = $previousAuthorName
        $env:GIT_AUTHOR_EMAIL = $previousAuthorEmail
        $env:GIT_COMMITTER_NAME = $previousCommitterName
        $env:GIT_COMMITTER_EMAIL = $previousCommitterEmail
    }

    if ( -not $commit ) {
        throw "git commit-tree failed"
    }

    $branch = ( & $GitExe rev-parse --abbrev-ref HEAD ).Trim()
    if ( $branch -eq "HEAD" ) {
        throw "Detached HEAD - checkout a branch before committing."
    }

    Invoke-Git update-ref "refs/heads/$branch" $commit

    Write-Host "Created commit $commit on $branch"
    & $GitExe log -1 --format=fuller

    if ( $Push ) {
        Invoke-Git push origin $branch
        Write-Host "Pushed to origin/$branch"
    }
}
finally {
    Pop-Location
}
