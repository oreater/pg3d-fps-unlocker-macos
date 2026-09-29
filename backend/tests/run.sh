#!/bin/zsh
set -euo pipefail
project_dir=${0:A:h:h}
launcher="$project_dir/testificateunlocker"
"$project_dir/build.sh"
clang -Wall -Wextra -Werror "$project_dir/tests/preload_test.c" -o "$project_dir/build/preload-test"
"$project_dir/build/preload-test"
clang -Wall -Wextra -Werror "$project_dir/tests/optimizer_test.c" -o "$project_dir/build/optimizer-test"
"$project_dir/build/optimizer-test"
clang -Wall -Wextra -Werror "$project_dir/tests/effects_test.c" -o "$project_dir/build/effects-test"
"$project_dir/build/effects-test"
clang -Wall -Wextra -Werror "$project_dir/tests/camera_extras_test.c" -o "$project_dir/build/camera-extras-test"
"$project_dir/build/camera-extras-test"
clang -Wall -Wextra -Werror "$project_dir/tests/frame_guard_test.c" -o "$project_dir/build/frame-guard-test"
"$project_dir/build/frame-guard-test"
# x86_64 like the game (Objective-C BOOL encodings differ on arm64); runs under Rosetta.
clang -arch x86_64 -fobjc-arc -Wall -Wextra -Werror -framework AppKit -framework QuartzCore \
  "$project_dir/tests/input_test.m" -o "$project_dir/build/input-test"
"$project_dir/build/input-test" 2>/dev/null
zsh -n "$launcher"
zsh -n "$project_dir/build.sh"
"$launcher" --help >/dev/null
"$launcher" --dry-run >/dev/null
"$launcher" --fps 240 --dry-run >/dev/null
"$launcher" --observe --dry-run >/dev/null
"$launcher" --gfx-jobs --profile performance --dry-run >/dev/null
"$launcher" --mouse fast --dry-run >/dev/null
"$launcher" --mouse game --no-stage-profile --dry-run >/dev/null
"$launcher" --job-workers 5 --dry-run >/dev/null
"$launcher" --job-workers auto --dry-run >/dev/null
expect_rejected() {
  local result=0
  "$launcher" "$@" >/dev/null 2>&1 || result=$?
  if (( result != 2 )); then
    print -u2 -- "FAIL: expected argument rejection for $*; exit=$result"
    exit 1
  fi
}
expect_rejected --fps 6000
expect_rejected --fps 29
expect_rejected --fps invalid
expect_rejected --fps
expect_rejected --steam-dir
expect_rejected --unknown
expect_rejected --profile
expect_rejected --profile extreme
expect_rejected --postfx
expect_rejected --postfx invalid
expect_rejected --set-occlusion invalid
expect_rejected --set-occlusion
expect_rejected --set-occlusion off --set-profile original
expect_rejected --set-postfx off --set-profile original
expect_rejected --set-profile invalid
expect_rejected --set-camera-extras
expect_rejected --set-camera-extras maybe
expect_rejected --set-camera-extras off --set-profile original
expect_rejected --job-workers
expect_rejected --job-workers 0
expect_rejected --job-workers 64
expect_rejected --job-workers many
expect_rejected --mouse
expect_rejected --mouse raw
expect_rejected --set-mouse
expect_rejected --set-mouse off
expect_rejected --set-mouse fast --set-profile original
expect_status() {
  local wanted=$1 result=0; shift
  "$launcher" "$@" >/dev/null 2>&1 || result=$?
  if (( result != wanted )); then
    print -u2 -- "FAIL: expected exit $wanted for $*; exit=$result"
    exit 1
  fi
}
[[ "$("$launcher" --supported-builds)" == *26.11.3/155325* ]] || { print -u2 'FAIL: --supported-builds'; exit 1; }
# Fake game bundles in a temporary folder; the installed game is never used here.
fakes=$(mktemp -d /private/tmp/optimizer-fake-games.XXXXXX)
trap 'rm -rf -- "$fakes"' EXIT
print 'int main(void){return 0;}' > "$fakes/main.c"
make_fake_game() { # name version build archs... ; optional HARDENED=1
  local app="$fakes/$1/Pixel Gun 3D.app" version=$2 build=$3; shift 3
  mkdir -p "$app/Contents/MacOS"
  /usr/libexec/PlistBuddy -c "Add :CFBundleShortVersionString string $version" \
    -c "Add :CFBundleVersion string $build" -c "Add :CFBundleExecutable string Pixel Gun 3D" \
    -c "Add :CFBundleIdentifier string local.optimizer.fakegame" "$app/Contents/Info.plist" >/dev/null
  local flags=() arch
  for arch in "$@"; do flags+=(-arch "$arch"); done
  clang $flags "$fakes/main.c" -o "$app/Contents/MacOS/Pixel Gun 3D"
  if (( ${HARDENED:-0} )); then codesign --force --sign - -o runtime "$app" 2>/dev/null
  else codesign --force --sign - "$app" 2>/dev/null; fi
  print -- "$app"
}
expect_status 3 --dry-run --game "$(make_fake_game old 1.0 1 x86_64)"
expect_status 4 --dry-run --game "$(HARDENED=1 make_fake_game hardened 26.11.3 155325 x86_64)"
if [[ "$(sysctl -n hw.optional.arm64 2>/dev/null)" == 1 ]]; then
  expect_status 4 --dry-run --game "$(make_fake_game native 26.11.3 155325 x86_64 arm64)"
fi
"$launcher" --profile original --dry-run >/dev/null
"$launcher" --profile balanced --dry-run >/dev/null
"$launcher" --profile performance --dry-run >/dev/null
print -- "PASS: shell syntax, preflight, FPS/observation/mouse options, invalid argument rejection, unsupported/native/hardened game refusal."
print -- "No game was launched by these static/unit tests. Live verification is recorded in TEST_RESULTS.md."
