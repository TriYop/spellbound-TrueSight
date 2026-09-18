#!/usr/bin/env bash
# Remove TrueSight from all known install locations.
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

echo "Uninstalling TrueSight..."

# User locations
remove "${HOME}/.vst3/TrueSight.vst3"
remove "${HOME}/.clap/TrueSight.clap"
remove "${HOME}/.lv2/TrueSight.lv2"

# System locations (silently skip if no permission)
if [[ $EUID -eq 0 ]]; then
    remove "/usr/lib/vst3/TrueSight.vst3"
    remove "/usr/lib/clap/TrueSight.clap"
    remove "/usr/lib/lv2/TrueSight.lv2"
else
    for path in "/usr/lib/vst3/TrueSight.vst3" \
                "/usr/lib/clap/TrueSight.clap" \
                "/usr/lib/lv2/TrueSight.lv2"; do
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
