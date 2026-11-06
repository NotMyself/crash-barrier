#Requires -Version 7
# Prints the semantic version of this checkout, derived from git tags of the form vX.Y.Z:
#   exactly on a tag            -> X.Y.Z            (release)
#   N commits after a tag       -> X.Y.Z-dev.N+sha  (prerelease)
#   no tag reachable            -> 0.0.0-dev.N+sha
# A dirty working tree appends ".dirty" to the build metadata.
$ErrorActionPreference = 'Stop'
$d = git describe --tags --match 'v*' --long --dirty 2>$null
if (-not $d) {
    "0.0.0-dev.$(git rev-list --count HEAD)+$(git rev-parse --short HEAD)"
    exit 0
}
if ($d -notmatch '^v(\d+\.\d+\.\d+)(-[0-9A-Za-z.]+)?-(\d+)-g([0-9a-f]+)(-dirty)?$') { throw "unexpected git describe output: $d" }
$base = $Matches[1]; $pre = $Matches[2]; $n = [int]$Matches[3]; $sha = $Matches[4]; $dirty = [bool]$Matches[5]
if ($n -eq 0 -and -not $dirty) { "$base$pre" } else { "$base-dev.$n+$sha$($dirty ? '.dirty' : '')" }
