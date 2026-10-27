#!/bin/bash
# Builds an installer package for macOS that puts the VST3 and the Audio Unit in /Library/Audio/Plug-Ins.
# Usage: build_pkg.sh <path to .vst3> <path to .component> <version> <output.pkg>
set -euo pipefail
vst3="$1"; au="$2"; version="$3"; out="$4"
here="$(cd "$(dirname "$0")" && pwd)"
work="$(mktemp -d)"

make_component () {   # name, bundle, install location
    local name="$1" bundle="$2" location="$3"
    mkdir -p "$work/$name/root"
    cp -R "$bundle" "$work/$name/root/"
    pkgbuild --analyze --root "$work/$name/root" "$work/$name/components.plist"
    # never "relocate" the bundle to an old copy somewhere else on the disk
    plutil -replace BundleIsRelocatable -bool NO "$work/$name/components.plist"
    pkgbuild --root "$work/$name/root" --component-plist "$work/$name/components.plist" \
             --identifier "com.salleq.ddr.$name" --version "$version" \
             --install-location "$location" ${5:+--scripts "$5"} "$work/$name.pkg"
}

make_component vst3 "$vst3" "/Library/Audio/Plug-Ins/VST3"
make_component au   "$au"   "/Library/Audio/Plug-Ins/Components" "$here/scripts"

sed "s/@VERSION@/$version/g" "$here/distribution.xml" > "$work/distribution.xml"
cp "$here/welcome.html" "$work/"
cp "$here/../../LICENSE" "$work/license.txt"
productbuild --distribution "$work/distribution.xml" --package-path "$work" --resources "$work" "$out"
echo "Built $out"
