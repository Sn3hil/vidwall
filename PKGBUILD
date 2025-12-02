pkgname=vidwall
pkgver=0.1.0
pkgrel=1
pkgdesc="Video wallpaper for Hyprland with auto-pause"
arch=('x86_64')
url=""
license=('MIT')
depends=('gtk4' 'gtk4-layer-shell' 'mpv' 'libepoxy' 'glibc' 'gcc-libs')
makedepends=('meson' 'ninja' 'git')
source=("git+file://${PWD}#branch=main")
md5sums=('SKIP')

build() {
	arch-meson "$pkgname" build
	meson compile -C build
}

package() {
	meson install -C build --destdir "$pkgdir"
}
