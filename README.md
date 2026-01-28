# simple-terminal

Simple Terminal Emulator for embedded Linux handhelds, migrated from SDL1.2 to SDL2 (by haoict)

<img src="images/st-img1.jpeg?raw=true" alt="Image1" width="250"/>
<img src="images/st-img2.jpeg?raw=true" alt="Image2" width="250"/>
<img src="images/st-img3.jpeg?raw=true" alt="Image3" width="250"/>
<img src="images/st-img1-trimuisp.jpg?raw=true" alt="Image1-TrimuiSP" width="250"/>

## Run

```bash
./simple-terminal
./simple-terminal -font 2 # with alternative embedded font
./simple-terminal -scale 1 -font /path/to/font.ttf -fontsize 12 -fontshade 1 # with a ttf font

# rotate content inside the window (0|90|180|270)
./simple-terminal -rotate 90
./simple-terminal -rotate 270

# run commands when open
./simple-terminal -r "ls -la" "uname -a" whoami
```

## Options

- **-scale**: scale factor for window sizing (e.g., `-scale 2.0`).
- **-font**: embedded font id (`1..5`) or path to a TTF file.
- **-fontsize**: TTF font size when using `-font /path/to.ttf`.
- **-fontshade**: TTF render mode (`0` solid, `1` blended, `2` shaded).
- **-rotate**: rotate the rendered content only (`0|90|180|270`). For `90` and `270`, characters and on-screen keyboard are rotated while window size stays the same.
- **-r**: run one or more commands in the terminal on start.
- **-q**: quiet mode.

## Features

### Scrollback Buffer
Simple Terminal supports scrollback history to review previous output:
- **Buffer size**: 256 lines by default (configurable in `src/config.h` via `scrollback_lines`)
- **Scroll up**: Press `F8` (PC) or `L2` (handhelds, with OSK deactivated) to scroll up
- **Scroll down**: Press `F7` (PC) or `R2` (handhelds, with OSK deactivated) to scroll down
- **Scroll indicator**: When scrolled, a `[offset]^` indicator appears in the top-right corner
- **Auto-reset**: Any key press (except scroll keys) returns to the bottom of the buffer


## Platforms

SimpleTerminal includes built-in input mappings for several embedded handheld devices.

When building for a handheld device, specify the target platform using the PLATFORM variable:

```bash
make PLATFORM=<platform>
```

Currently supported platforms include:
  - `rgb30`
  - `h700`
  - `r36s`
  - `pi` (Raspberry Pi / generic controller)

If no platform is specified, SimpleTerminal builds with a generic Linux keyboard mapping.

Note: The `PLATFORM` option currently only affects controller and keyboard input mappings. Other devices may work correctly if their controller layout matches one of the existing platforms.


## Build

### Generic linux

```bash
sudo apt install build-essential libsdl2-dev libsdl2-ttf-dev
make
```

### Build with buildroot toolchain

You can build everything for the target device with buildroot:
https://github.com/haoict/TiniLinux/blob/master/README.md#build

or build the toolchain only and build simple-terminal separately

```bash
# build toolchain
cd TiniLinux
./make-board-build.sh configs/toolchain_aarch64_defconfig
cd output.toolchain_aarch64
make -j $(nproc)

# build simple-terminal
cd SimpleTerminal
export CROSS_COMPILE=~/TiniLinux/output.toolchain_aarch64/host/bin/aarch64-linux-
make
```

### Build using Podman

You can also build SimpleTerminal using Podman.

```bash
podman run --rm -it \
  -v "$PWD:/work:Z" -w /work \
  docker.io/debian:trixie-slim \
  bash -lc '
    set -euo pipefail

    dpkg --add-architecture arm64
    apt update
    apt install -y --no-install-recommends \
      make pkg-config \
      gcc-aarch64-linux-gnu libc6-dev-arm64-cross \
      libsdl2-dev:arm64 libsdl2-ttf-dev:arm64

    export CC=aarch64-linux-gnu-gcc
    export PKG_CONFIG_LIBDIR=/usr/lib/aarch64-linux-gnu/pkgconfig:/usr/share/pkgconfig
    export PKG_CONFIG_SYSROOT_DIR=/

    make clean || true
    make PLATFORM=r36s CC="$CC"
  '
```

## To edit embedded bitmap font

https://simple-terminal-psi.vercel.app

# License & Credits

MIT License  
Based on Aurélien APTEL <aurelien dot aptel at gmail dot com> bt source code.  
https://github.com/benob/rs97_st-sdl
