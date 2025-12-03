# vidwall

A lightweight, hardware-accelerated animated wallpaper utility for Hyprland, built with GTK4, Layer Shell, and MPV.


## Installation & Build

### Using Nix (Recommended)

This project includes a `flake.nix` for easy development and building.

```bash
# Build the package
nix build

# Run directly
nix run . -- /path/to/video.mp4
```

### Manual Build

Requirements:
- GTK4
- gtk4-layer-shell
- mpv
- libepoxy
- meson
- ninja
- gcc/clang

```bash
# Setup build directory
meson setup build

# Compile
ninja -C build

# Run
./build/vidwall /path/to/video.mp4
```

## Usage

```bash
vidwall [OPTIONS] <video-file>
```

### Options

| Option | Description |
|--------|-------------|
| `-h`, `--help` | Show help message |
| `-m`, `--mute` | Mute audio (default: on) |
| `-u`, `--no-mute` | Enable audio |
| `-l`, `--no-loop` | Don't loop the video |
| `-p`, `--no-pause` | Disable auto-pause on window focus |
| `-n`, `--no-downscale` | Disable 4K downscaling (higher quality, more CPU) |
| `-H`, `--no-hwdec` | Disable hardware decoding (use if crashing) |

### Examples

**Basic usage (muted, looping, auto-pause enabled):**
```bash
vidwall ~/Videos/wallpaper.mp4
```

**Enable audio:**
```bash
vidwall --no-mute ~/Videos/music_video.mp4
```

**Disable auto-pause (always playing):**
```bash
vidwall --no-pause ~/Videos/background.mp4
```
