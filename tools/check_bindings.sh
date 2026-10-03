#!/bin/zsh
# Offline check after a game update (never launches the game): every Unity
# binding name the library resolves must be registered by the game's
# UnityPlayer.dylib, every IL2CPP export it uses must exist in
# GameAssembly.dylib, and the frame-rate / v-sync wrappers must be found.
#   tools/check_bindings.sh [Pixel Gun 3D.app]
here=${0:A:h}
src=$here/../backend/src
app=${1:-"$HOME/Library/Application Support/Steam/steamapps/common/Pixel Gun 3D PC Edition/Pixel Gun 3D.app"}
contents=$app/Contents
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT

print -- "Game $(/usr/libexec/PlistBuddy -c 'Print :CFBundleShortVersionString' "$contents/Info.plist")/$(/usr/libexec/PlistBuddy -c 'Print :CFBundleVersion' "$contents/Info.plist")," \
  "$(lipo -archs "$contents/Frameworks/GameAssembly.dylib")"
strings -n 6 "$contents/Frameworks/UnityPlayer.dylib" > "$work/unityplayer.txt"
print -- "Unity $(grep -m1 -E '^20[0-9]{2}\.[0-9]+\.[0-9]+f[0-9]+$' "$work/unityplayer.txt")"

missing=0
# Literal names in the library, plus the getter/setter pairs optimizer.c builds
# from its settings table.
{
  grep -ohE '"UnityEngine[A-Za-z0-9_.]*::[A-Za-z0-9_]+(\([^"]*\))?"' "$src"/*.c "$src"/*.m | tr -d '"'
  sed -nE 's/^ *\{\.owner=(QS|"UnityEngine\.RenderSettings"), \.name="([A-Za-z]+)".*/\1 \2/p' "$src/optimizer.c" |
    while read owner prop; do
      [[ $owner == QS ]] && owner=UnityEngine.QualitySettings || owner=UnityEngine.RenderSettings
      print -- "$owner::get_$prop"; print -- "$owner::set_$prop"
    done
} | sort -u > "$work/wanted.txt"
total=0
while read name; do
  (( ++total ))
  short=${name%%\(*}
  if ! grep -qxF "$name" "$work/unityplayer.txt" && ! grep -qE "^${short//./\\.}(\(|$)" "$work/unityplayer.txt"; then
    print -- "MISSING binding: $name"; missing=1
  fi
done < "$work/wanted.txt"
(( missing )) || print -- "Unity bindings: $total/$total registered"

nm -gU "$contents/Frameworks/GameAssembly.dylib" 2>/dev/null | awk '{print $3}' | sed 's/^_//' | sort -u > "$work/exports.txt"
grep -ohE '"il2cpp_[a-z_0-9]+"' "$src"/*.c "$src"/*.m | tr -d '"' | sort -u > "$work/il2cpp.txt"
gone=$(comm -23 "$work/il2cpp.txt" "$work/exports.txt")
if [[ -n $gone ]]; then
  print -- "MISSING IL2CPP exports: ${(f)gone}"; missing=1
else
  print -- "IL2CPP exports: $(wc -l < "$work/il2cpp.txt" | tr -d ' ') present"
fi

# The pacing wrappers the library redirects (it finds them the same way at runtime).
python3 "$here/find_wrappers.py" "$contents/Frameworks/GameAssembly.dylib" || missing=1
exit $missing
