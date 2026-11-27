#!/bin/sh
set -eu
target=$1
source=$2
editor_pid=$3
shift 3
stage=$(dirname "$source")
test "$(dirname "$stage")" = "$(dirname "$target")"
case "$stage" in /*/.video-editor-update-*) ;; *) exit 1 ;; esac
test ! -L "$stage"
cleanup() {
    # Preserve the old app only if restoring it after a failed move also failed.
    if [ -d "$stage/previous.app" ] && [ ! -e "$target" ]; then
        mv "$stage/previous.app" "$target" || return
    fi
    rm -rf -- "$stage"
}
trap cleanup EXIT
cd "$(dirname "$target")"
test -e "$target"
test -e "$source"
test ! -L "$target"
if [ -d "$target" ]; then
    case "$target" in /*.app) ;; *) exit 1 ;; esac
    test -x "$target/Contents/MacOS/VideoEditor"
    test -x "$source/Contents/MacOS/VideoEditor"
fi
printf ready >"$stage/ready"
count=0
while [ ! -f "$stage/commit" ]; do
    count=$((count + 1))
    if [ "$count" -gt 30 ]; then echo 'Installation was not committed.'; exit 1; fi
    sleep 1
done
count=0
while kill -0 "$editor_pid" 2>/dev/null; do
    count=$((count + 1))
    if [ "$count" -gt 120 ]; then echo 'Video Editor did not close in time.'; exit 1; fi
    sleep 1
done
if [ -d "$target" ]; then
    # Retain the old bundle until the prepared bundle is in place.
    mv "$target" "$stage/previous.app"
    if ! mv "$source" "$target"; then
        mv "$stage/previous.app" "$target"
        exit 1
    fi
    if [ "$#" -gt 0 ]; then /usr/bin/open -n "$target" --args "$@";
    else /usr/bin/open -n "$target"; fi
else
    chmod 755 "$source"
    mv -f "$source" "$target"
    "$target" "$@" </dev/null >/dev/null 2>&1 &
fi
echo 'Update installed.'
