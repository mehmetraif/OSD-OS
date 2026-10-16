#!/bin/bash
# The changes a release brings, for the top of its notes on GitHub: the
# commits since the previous release tag, each as its subject line, under
# What's New, Changelog and Bug Fixes. The release workflow
# (.github/workflows/release.yml) runs it from a full clone.
#
#   scripts/release-notes.sh <tag> [<previous tag>]
#
# <previous tag> defaults to the tag before <tag>; with none, the whole
# history counts. Merge commits are left out (a pull request's own commits
# describe it), and so is a commit reverted in the same release, with its
# revert (git revert's "This reverts commit <hash>."): neither changes what
# the release brings. A commit goes by its conventional-commit type: feat under
# What's New, fix under Bug Fixes, any other type under Changelog. One
# without a type goes by its first word: Fix… under Bug Fixes, Add… under
# What's New, anything else under Changelog. A section with nothing in it
# is left out.
set -euo pipefail

tag=${1:?usage: release-notes.sh <tag> [<previous tag>]}
previous=${2:-$(git describe --tags --abbrev=0 "${tag}^" 2>/dev/null || true)}
range=${previous:+${previous}..}${tag}

# The commits, oldest first: each one's hash, subject, and the commit it
# reverts, if it is a revert.
hashes=()
subjects=()
reverts=()
while IFS=$'\x1f' read -r -d $'\x1e' hash subject body; do
    hash=${hash#$'\n'}
    [ -n "$hash" ] || continue
    hashes+=("$hash")
    subjects+=("$subject")
    if [[ $body =~ This\ reverts\ commit\ ([0-9a-f]{7,40}) ]]; then
        reverts+=("${BASH_REMATCH[1]}")
    else
        reverts+=("")
    fi
done < <(git log --no-merges --reverse --date-order --format='%H%x1f%s%x1f%b%x1e' "$range")

# A revert and the commit it reverts, both in the release, cancel out. The
# newest first, so that a revert reverted in its turn cancels with that
# revert, and the commit they both followed stays.
declare -A dropped=()
for (( i = ${#hashes[@]} - 1; i >= 0; i-- )); do
    target=${reverts[$i]}
    [ -n "$target" ] && [ -z "${dropped[$i]:-}" ] || continue
    for (( j = i - 1; j >= 0; j-- )); do
        if [[ ${hashes[$j]} == "$target"* ]]; then
            [ -n "${dropped[$j]:-}" ] || { dropped[$i]=1; dropped[$j]=1; }
            break
        fi
    done
done

new=()
changes=()
fixes=()
seen=$'\n'
for i in "${!subjects[@]}"; do
    [ -z "${dropped[$i]:-}" ] || continue
    subject=${subjects[$i]}
    [ -n "$subject" ] || continue
    if [[ $subject =~ ^([A-Za-z]+)(\([^\)]*\))?!?:[[:space:]]*(.*)$ ]]; then
        type=${BASH_REMATCH[1],,}
        text=${BASH_REMATCH[3]}
    else
        first=${subject%% *}
        case ${first,,} in
            fix|fixes|fixed) type=fix ;;
            add|adds|added) type=feat ;;
            *) type=other ;;
        esac
        text=$subject
    fi
    [ -n "$text" ] || continue
    text=${text^}
    # GitHub would take <output> for a tag and show nothing of it. The
    # replacement is quoted, so that bash 5.2 doesn't read its & as the match.
    text=${text//</"&lt;"}
    text=${text//>/"&gt;"}
    case $seen in *$'\n'"$text"$'\n'*) continue ;; esac
    seen+="$text"$'\n'
    case $type in
        feat) new+=("$text") ;;
        fix) fixes+=("$text") ;;
        *) changes+=("$text") ;;
    esac
done

section() {
    local title=$1
    shift
    [ $# -gt 0 ] || return 0
    printf '## %s\n\n' "$title"
    printf -- '- %s\n' "$@"
    printf '\n'
}

section "What's New" ${new[@]+"${new[@]}"}
section "Changelog" ${changes[@]+"${changes[@]}"}
section "Bug Fixes" ${fixes[@]+"${fixes[@]}"}
