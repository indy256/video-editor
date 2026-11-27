#!/bin/sh
set -eu
helper=$1
root=$(mktemp -d)
trap 'rm -rf -- "$root"' EXIT
stage="$root/.video-editor-update-test"
mkdir "$stage"
target="$root/Video Editor.AppImage"
printf 'old application' >"$target"
cat >"$stage/download" <<'APP'
#!/bin/sh
test "$2" = --resume-after-update
printf '%s\n%s' "$3" "$1" >"$1"
APP
sleep 30 &
old_pid=$!
video="$root/video ' & [1].restarted"
sh "$helper" "$target" "$stage/download" "$old_pid" "$video" --resume-after-update 1250 &
helper_pid=$!
count=0
while [ ! -f "$stage/ready" ]; do
    count=$((count + 1))
    test "$count" -lt 10
    sleep 1
done
test "$(cat "$target")" = 'old application'
printf install >"$stage/commit"
sleep 1
test ! -f "$video"
kill "$old_pid"
wait "$old_pid" 2>/dev/null || true
wait "$helper_pid"
test ! -e "$stage"
count=0
while [ ! -f "$video" ]; do
    count=$((count + 1))
    test "$count" -lt 10
    sleep 1
done
test "$(cat "$video")" = "$(printf '1250\n%s' "$video")"
test -x "$target"
mkdir "$stage"
if sh "$helper" "$target" "$stage/download" 0; then
    echo 'An update with a missing download should fail.'
    exit 1
fi
test ! -e "$stage"
test -x "$target"
echo 'Unix update replacement and restart passed.'
