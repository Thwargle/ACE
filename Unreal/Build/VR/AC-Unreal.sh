#!/bin/sh
set -eu
package_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
executable="$package_dir/ACUnreal/Binaries/Linux/ACUnreal"
if [ ! -f "$executable" ]; then
    printf '%s\n' 'The Linux package is incomplete. Keep AC-Unreal.sh, ACUnreal, and Engine together.' >&2
    exit 1
fi
if [ ! -x "$executable" ]; then
    chmod u+x "$executable"
fi
cd "$package_dir"
exec "$executable" -nohmd -SaveToUserDir "$@"
