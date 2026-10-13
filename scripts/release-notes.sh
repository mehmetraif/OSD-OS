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
# describe it). A commit goes by its conventional-commit type: feat under
# What's New, fix under Bug Fixes, any other type under Changelog. One
# without a type goes by its first word: Fix… under Bug Fixes, Add… under
# What's New, anything else under Changelog. A section with nothing in it
# is left out.
set -euo pipefail

tag=${1:?usage: release-notes.sh <tag> [<previous tag>]}
previous=${2:-$(git describe --tags --abbrev=0 "${tag}^" 2>/dev/null || true)}
range=${previous:+${previous}..}${tag}

new=()
changes=()
fixes=()
seen=$'\n'
while IFS= read -r subject; do
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
done < <(git log --no-merges --reverse --date-order --format=%s "$range")

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
