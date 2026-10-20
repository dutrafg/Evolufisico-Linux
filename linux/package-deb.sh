#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
meson setup linux/build linux --prefix=/usr --buildtype=release --reconfigure 2>/dev/null || meson setup linux/build linux --prefix=/usr --buildtype=release
meson compile -C linux/build
stage="$(mktemp -d)"
trap 'rm -rf "$stage"' EXIT
DESTDIR="$stage" meson install -C linux/build --no-rebuild
mkdir -p "$stage/DEBIAN" linux/dist
cat > "$stage/DEBIAN/control" <<EOF
Package: evolufisico-linux
Version: 0.1.0
Section: office
Priority: optional
Architecture: $(dpkg --print-architecture)
Maintainer: EvoluFisico Desktop <noreply@github.com>
Depends: libgtk-4-1 (>= 4.10), libsqlcipher0
Description: EvoluFisico Linux GTK4 preview
 Native GTK4 development preview. Not suitable for clinical data.
EOF
dpkg-deb --build --root-owner-group "$stage" "linux/dist/evolufisico-linux_0.1.0_$(dpkg --print-architecture).deb"
dpkg-deb --info "linux/dist/evolufisico-linux_0.1.0_$(dpkg --print-architecture).deb"
