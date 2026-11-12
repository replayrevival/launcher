# Shared by sync_publish.ps1 and package.ps1.

# Lists text that must not appear in a public copy: the current user's name and profile path, and any per-user profile path.
# (third_party and .git are skipped.)
function Find-PersonalText {
    param([Parameter(Mandatory)][string]$Root)
    $patterns = @([regex]::Escape($env:USERNAME), [regex]::Escape($env:USERPROFILE), '[A-Za-z]:\\Users\\[^\\ ]+')
    Get-ChildItem $Root -Recurse -File |
        Where-Object { $_.FullName -notmatch '\\third_party\\|\\\.git\\' -and $_.Extension -notin '.exe', '.dll', '.png', '.ttf', '.zip' } |
        Select-String -Pattern $patterns -List
}
