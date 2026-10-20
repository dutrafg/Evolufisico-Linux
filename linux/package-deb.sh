#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."

# Build and package for the distribution where the executable was linked.
# SQLCipher ABI differs: Ubuntu 24.04 => libsqlcipher1; 26.04 => libsqlcipher2.
# A .deb built on 24.04 must never be passed off as compatible with 26.04.
if [[ ! -r /etc/os-release ]]; then
  echo "Erro: distribuicao Linux nao identificada." >&2
  exit 1
fi
# shellcheck disable=SC1091
. /etc/os-release
case "${ID:-}/${VERSION_ID:-}" in
  ubuntu/24.04) sqlcipher_runtime=libsqlcipher1 ;;
  ubuntu/26.04) sqlcipher_runtime=libsqlcipher2 ;;
  *)
    echo "Empacotamento nao homologado para ${ID:-unknown} ${VERSION_ID:-unknown}." >&2
    echo "Use um runner Ubuntu 24.04 ou 26.04 para gerar o .deb correspondente." >&2
    exit 1
    ;;
esac

meson setup linux/build linux --prefix=/usr --buildtype=release --reconfigure 2>/dev/null || \
  meson setup linux/build linux --prefix=/usr --buildtype=release
meson compile -C linux/build

# Prevent a successful build from publishing a .deb with a missing runtime.
dpkg-query -W -f='${Status}' "$sqlcipher_runtime" | grep -qx 'install ok installed'
ldd linux/build/evolufisico-linux | grep -q 'libsqlcipher'
if ldd linux/build/evolufisico-linux | grep -q 'not found'; then
  echo "Erro: bibliotecas dinamicas ausentes no ambiente de compilacao." >&2
  exit 1
fi

app_version="$(meson introspect --projectinfo linux/build | python3 -c 'import json,sys; print(json.load(sys.stdin)["version"])')"
distro="ubuntu${VERSION_ID}"
package_version="${app_version}+${distro}"
arch="$(dpkg --print-architecture)"
output="linux/dist/evolufisico-linux_${package_version}_${arch}.deb"

stage="$(mktemp -d)"
trap 'rm -rf "$stage"' EXIT
DESTDIR="$stage" meson install -C linux/build --no-rebuild
mkdir -p "$stage/DEBIAN" linux/dist
cat > "$stage/DEBIAN/control" <<EOF
Package: evolufisico-linux
Version: ${package_version}
Section: office
Priority: optional
Architecture: ${arch}
Maintainer: EvoluFisico Desktop <noreply@github.com>
Depends: libgtk-4-1 (>= 4.10), ${sqlcipher_runtime}
Description: EvoluFisico Linux GTK4 preview for ${distro}
 Native GTK4 development preview. Not suitable for clinical data.
EOF
dpkg-deb --build --root-owner-group "$stage" "$output"
dpkg-deb --info "$output"
echo "OK: pacote construido para $distro com $sqlcipher_runtime: $output"
