#!/bin/bash
# =============================================================================
#  KumAmp - instalador .pkg para macOS (VST3 + AU + app Standalone)
#
#  Uso:   installer/macos/build_pkg.sh [carpeta-de-build]
#         (por defecto build-universal; compila antes con -DKUMAMP_UNIVERSAL=ON)
#
#  Firma: sin variables, firma "ad-hoc" (sirve para probar; Gatekeeper pedira
#  autorizacion una vez). Con un Developer ID:
#     KUMAMP_SIGN_IDENTITY="Developer ID Application: ..." \
#     KUMAMP_INSTALLER_IDENTITY="Developer ID Installer: ..." build_pkg.sh
#  y luego notarizar con:  xcrun notarytool submit dist/*.pkg --wait ...
# =============================================================================
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BUILD="${1:-$ROOT/build-universal}"
ART="$BUILD/KumAmp_artefacts/Release"
VERSION="$(sed -n 's/^project(KumAmp VERSION \([0-9.]*\).*/\1/p' "$ROOT/CMakeLists.txt")"
SIGN_IDENTITY="${KUMAMP_SIGN_IDENTITY:--}"
INSTALLER_IDENTITY="${KUMAMP_INSTALLER_IDENTITY:-}"
OUT="$ROOT/dist"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

for bundle in "VST3/KumAmp.vst3" "AU/KumAmp.component" "Standalone/KumAmp.app"; do
    [ -d "$ART/$bundle" ] || { echo "Falta $ART/$bundle - compila primero." >&2; exit 1; }
done

ARCHS="$(lipo -archs "$ART/VST3/KumAmp.vst3/Contents/MacOS/KumAmp")"
case "$ARCHS" in *x86_64*arm64*|*arm64*x86_64*) SUFFIX="Universal" ;; *arm64*) SUFFIX="AppleSilicon" ;; *) SUFFIX="Intel" ;; esac
echo "KumAmp $VERSION  ($ARCHS)"

# 1. Stage + sign each bundle.
stage() {   # stage <bundle path relative to ART> <component name>
    mkdir -p "$WORK/root/$2"
    ditto "$ART/$1" "$WORK/root/$2/$(basename "$1")"
    codesign --force --deep --options runtime --timestamp=none --sign "$SIGN_IDENTITY" "$WORK/root/$2/$(basename "$1")"
    codesign --verify --deep --strict "$WORK/root/$2/$(basename "$1")"
}
stage "VST3/KumAmp.vst3"       vst3
stage "AU/KumAmp.component"    au
stage "Standalone/KumAmp.app"  app

# 2. One component package per format. Bundles must NOT be relocatable, or the
#    installer would "update" an old copy wherever it finds one instead.
component() {   # component <name> <install location> [scripts dir]
    local plist="$WORK/$1.plist"
    pkgbuild --analyze --root "$WORK/root/$1" "$plist" > /dev/null
    plutil -replace 0.BundleIsRelocatable -bool NO "$plist"
    local scripts=()
    [ -n "${3:-}" ] && scripts=(--scripts "$3")
    pkgbuild --root "$WORK/root/$1" --component-plist "$plist" --install-location "$2" \
             --identifier "com.kumampaudio.kumamp.$1" --version "$VERSION" ${scripts[@]+"${scripts[@]}"} \
             "$WORK/pkgs/$1.pkg" > /dev/null
}
mkdir -p "$WORK/pkgs"
component vst3 "/Library/Audio/Plug-Ins/VST3"
component au   "/Library/Audio/Plug-Ins/Components" "$ROOT/installer/macos/scripts"
component app  "/Applications"

# 3. Product archive with welcome / readme pages and per-format choices.
sed "s/@VERSION@/$VERSION/g" "$ROOT/installer/macos/distribution.xml" > "$WORK/distribution.xml"
mkdir -p "$OUT"
PKG="$OUT/KumAmp-$VERSION-macOS-$SUFFIX.pkg"
productbuild --distribution "$WORK/distribution.xml" --resources "$ROOT/installer/macos/resources" \
             --package-path "$WORK/pkgs" "$WORK/unsigned.pkg" > /dev/null

if [ -n "$INSTALLER_IDENTITY" ]; then
    productsign --sign "$INSTALLER_IDENTITY" "$WORK/unsigned.pkg" "$PKG"
else
    mv "$WORK/unsigned.pkg" "$PKG"
fi
echo "Listo: $PKG"
