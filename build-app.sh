#!/bin/zsh
set -euo pipefail
project_dir=${0:A:h}
stage=$(mktemp -d /private/tmp/pg3d-optimizer-package.XXXXXX)
# Remove the staging folder on success and failure alike.
trap 'rm -rf -- "$stage"' EXIT
# The app's name comes from Info.plist (CFBundleName), so renaming is a one-line change there.
app_name=$(/usr/libexec/PlistBuddy -c 'Print :CFBundleName' "$project_dir/macos-app/Info.plist")
app_bundle="$stage/$app_name.app"
mkdir -p "$app_bundle/Contents/MacOS" "$app_bundle/Contents/Resources" "$stage/AppIcon.iconset"
ditto "$project_dir/backend" "$app_bundle/Contents/Resources/Launcher"
# Ship only what the launcher runs: no unit tests, local test binaries or test notes.
rm -rf -- "$app_bundle/Contents/Resources/Launcher/tests" "$app_bundle/Contents/Resources/Launcher/build" \
  "$app_bundle/Contents/Resources/Launcher/TEST_RESULTS.md"
/bin/zsh "$app_bundle/Contents/Resources/Launcher/build.sh"
for cpu in arm64 x86_64; do
  xcrun swiftc -swift-version 5 -parse-as-library -O -target "$cpu-apple-macos13.0" \
    -framework AppKit -framework SwiftUI "$project_dir/macos-app/Launcher.swift" -o "$stage/PG3DLauncher-$cpu"
done
lipo -create "$stage/PG3DLauncher-arm64" "$stage/PG3DLauncher-x86_64" -output "$app_bundle/Contents/MacOS/PG3DLauncher"
ditto "$project_dir/macos-app/Info.plist" "$app_bundle/Contents/Info.plist"
# Russo One (SIL OFL 1.1), the UI font Pixel Gun 3D uses, with its license.
ditto "$project_dir/macos-app/Fonts" "$app_bundle/Contents/Resources/Fonts"
xcrun swift "$project_dir/macos-app/MakeIcon.swift" "$stage/AppIcon.iconset"
iconutil -c icns "$stage/AppIcon.iconset" -o "$app_bundle/Contents/Resources/AppIcon.icns"
xattr -cr "$app_bundle"
codesign --force --sign - "$app_bundle"
codesign --verify --deep --strict "$app_bundle"
output_app="$project_dir/$app_name.app"
if [[ -e "$output_app" ]]; then
  mv "$output_app" "$project_dir/previous-app-$(date +%Y%m%d-%H%M%S)-$$.app"
fi
mv "$app_bundle" "$output_app"
# Synced folders (File Provider) can tag the bundle root with Finder info, which
# strict signature verification rejects. It is metadata only; remove it.
xattr -d com.apple.FinderInfo "$output_app" 2>/dev/null || true
print -- "Built $output_app"
