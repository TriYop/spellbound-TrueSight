#!/usr/bin/env bash
# Remove MixAdvice from all known install locations.
set -euo pipefail

removed=0

remove() {
    local path="$1"
    if [[ -e "$path" ]]; then
        rm -rf "$path"
        echo "  Removed: $path"
        removed=1
    fi
}

echo "Uninstalling MixAdvice..."

# User locations
remove "${HOME}/.vst3/MixAdvice.vst3"
remove "${HOME}/.clap/MixAdvice.clap"
remove "${HOME}/.local/bin/MixAdvice"

# System locations (silently skip if no permission)
if [[ $EUID -eq 0 ]]; then
    remove "/usr/lib/vst3/MixAdvice.vst3"
    remove "/usr/lib/clap/MixAdvice.clap"
    remove "/usr/local/bin/MixAdvice"
else
    for path in "/usr/lib/vst3/MixAdvice.vst3" \
                "/usr/lib/clap/MixAdvice.clap" \
                "/usr/local/bin/MixAdvice"; do
        if [[ -e "$path" ]]; then
            echo "  Skipping $path (re-run with sudo to remove)"
        fi
    done
fi

if [[ $removed -eq 0 ]]; then
    echo "  Nothing to remove."
else
    echo "Done."
fi
