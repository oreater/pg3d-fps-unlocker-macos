#!/bin/zsh
set -euo pipefail
project_dir=${0:A:h:h}
launcher="$project_dir/testificateunlocker"
"$project_dir/build.sh"
clang -Wall -Wextra -Werror "$project_dir/tests/wrapper_scan_test.c" -o "$project_dir/build/wrapper-scan-test"
"$project_dir/build/wrapper-scan-test"
clang -Wall -Wextra -Werror "$project_dir/tests/preload_test.c" -o "$project_dir/build/preload-test"
"$project_dir/build/preload-test"
clang -Wall -Wextra -Werror "$project_dir/tests/optimizer_test.c" -o "$project_dir/build/optimizer-test"
"$project_dir/build/optimizer-test"
clang -Wall -Wextra -Werror "$project_dir/tests/effects_test.c" -o "$project_dir/build/effects-test"
"$project_dir/build/effects-test"
clang -Wall -Wextra -Werror "$project_dir/tests/camera_extras_test.c" -o "$project_dir/build/camera-extras-test"
"$project_dir/build/camera-extras-test"
clang -Wall -Wextra -Werror "$project_dir/tests/volumes_test.c" -o "$project_dir/build/volumes-test"
"$project_dir/build/volumes-test"
clang -Wall -Wextra -Werror "$project_dir/tests/frame_guard_test.c" -o "$project_dir/build/frame-guard-test"
"$project_dir/build/frame-guard-test"
clang -Wall -Wextra -Werror "$project_dir/tests/latency_test.c" -o "$project_dir/build/latency-test"
"$project_dir/build/latency-test"
clang -Wall -Wextra -Werror "$project_dir/tests/threads_test.c" -o "$project_dir/build/threads-test"
"$project_dir/build/threads-test"
# x86_64 like the game (Objective-C BOOL encodings differ on arm64); runs under Rosetta.
clang -arch x86_64 -fobjc-arc -Wall -Wextra -Werror -framework Metal -framework QuartzCore \
  "$project_dir/tests/metal_tuning_test.m" -o "$project_dir/build/metal-tuning-test"
"$project_dir/build/metal-tuning-test" 2>/dev/null
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
"$launcher" --list-options >/dev/null
"$launcher" --profile performance --option bloom=game,lod=30 --option textures=2 --dry-run >/dev/null
"$launcher" --profile balanced --option engine-threads=1,gpu-priority=1 --dry-run >/dev/null
# Every catalogue key is known to the library, with valid presets and choices.
catalogue_keys=()
while IFS=$'\t' read -r key group label hint choices original balanced performance; do
  [[ -n "$key" && "$key" != \#* ]] || continue
  catalogue_keys+=("$key")
  [[ "$group" == effects || "$group" == lighting || "$group" == detail || "$group" == input || "$group" == engine ]] ||
    { print -u2 "FAIL: $key has unknown tab $group"; exit 1; }
  /usr/bin/grep -Fq "= \"$key\"," "$project_dir/src/options.c" || { print -u2 "FAIL: $key missing from src/options.c"; exit 1; }
  for value in $original $balanced $performance; do
    # Input and engine settings stay out of the graphics presets ("-"); graphics options are always in them.
    if [[ "$group" == input || "$group" == engine ]]; then
      [[ "$value" == - ]] || { print -u2 "FAIL: $key is an input/engine setting but has preset value $value"; exit 1; }
    else
      [[ "|$choices|" == *"|$value:"* ]] || { print -u2 "FAIL: $key preset value $value is not a choice"; exit 1; }
    fi
  done
done < "$project_dir/options.tsv"
(( ${#catalogue_keys} == $(/usr/bin/grep -c '^    \[OPT_.*\] = "' "$project_dir/src/options.c") )) || { print -u2 'FAIL: catalogue and library key counts differ'; exit 1; }
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
expect_rejected --option
expect_rejected --option bloom
expect_rejected --option bloom=2
expect_rejected --option unknown=0
expect_rejected --option lod=50
expect_rejected --option frame-queue=2
expect_rejected --option gpu-priority=0
expect_rejected --option engine-threads=-
expect_rejected --set shadows=on
expect_rejected --set-fps 10
expect_rejected --set-fps
expect_rejected --set bloom=0 --set-profile original
expect_rejected --probe --set-fps 240
expect_rejected --set-occlusion invalid
expect_rejected --set-occlusion
expect_rejected --set-occlusion off --set-profile original
expect_rejected --set-profile invalid
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
[[ "$("$launcher" --supported-builds)" == *26.12.0/156210* ]] || { print -u2 'FAIL: --supported-builds'; exit 1; }
[[ "$("$launcher" --supported-unity)" == *2022.3* ]] || { print -u2 'FAIL: --supported-unity'; exit 1; }
# Fake game bundles in a temporary folder; the installed game is never used here.
fakes=$(mktemp -d /private/tmp/optimizer-fake-games.XXXXXX)
trap 'rm -rf -- "$fakes"' EXIT
print 'int main(void){return 0;}' > "$fakes/main.c"
make_fake_game() { # name version build archs... ; optional HARDENED=1, UNITY=2022.3.1f1
  local app="$fakes/$1/Pixel Gun 3D.app" version=$2 build=$3; shift 3
  mkdir -p "$app/Contents/MacOS"
  /usr/libexec/PlistBuddy -c "Add :CFBundleGetInfoString string Unity Player version ${UNITY:-none} (0). (c)" \
    -c "Add :CFBundleShortVersionString string $version" \
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
# An unreviewed build launches only on a supported Unity version.
expect_status 3 --dry-run --game "$(UNITY=6000.0.1f1 make_fake_game unity6 27.0.0 1 x86_64)"
expect_status 0 --dry-run --game "$(UNITY=2022.3.80f1 make_fake_game next 26.12.1 1 x86_64)"
expect_status 4 --dry-run --game "$(HARDENED=1 make_fake_game hardened 26.11.3 155325 x86_64)"
if [[ "$(sysctl -n hw.optional.arm64 2>/dev/null)" == 1 ]]; then
  expect_status 4 --dry-run --game "$(make_fake_game native 26.11.3 155325 x86_64 arm64)"
fi
"$launcher" --profile original --dry-run >/dev/null
"$launcher" --profile balanced --dry-run >/dev/null
"$launcher" --profile performance --dry-run >/dev/null
print -- "PASS: shell syntax, preflight, FPS/observation/mouse/graphics/input/engine options, option catalogue, invalid argument rejection, unsupported/native/hardened game refusal, unreviewed builds on supported Unity versions."
print -- "No game was launched by these static/unit tests. Live verification is recorded in TEST_RESULTS.md."
