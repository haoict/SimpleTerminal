# simple-terminal

Simple Terminal Emulator for embedded Linux handhelds, migrated from SDL1.2 to SDL2 (by haoict)

<img src="images/st-img1.jpeg?raw=true" alt="Image1" width="250"/>
<img src="images/st-img2.jpeg?raw=true" alt="Image2" width="250"/>
<img src="images/st-img3.jpeg?raw=true" alt="Image3" width="250"/>
<img src="images/st-img1-trimuisp.jpg?raw=true" alt="Image1-TrimuiSP" width="250"/>

# Build

For generic linux:

```bash
sudo apt install build-essential libsdl2-dev libsdl2-ttf-dev
make
```

# Run

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

# Build with buildroot toolchain

you can build everything for the target device with buildroot:  
https://github.com/haoict/TiniLinux/blob/master/README.md#build

or build the toolchain only and build simple-terminal separately

```bash
# build toolchain
cd TiniLinux
./make-board-build.sh configs/toolchain_x86_64_aarch64_defconfig
cd output.toolchain_x86_64_aarch64
make -j $(nproc)

# build simple-terminal
cd SimpleTerminal
export CROSS_COMPILE=/home/haoict/TiniLinux/output.toolchain_x86_64_aarch64/host/bin/aarch64-none-linux-gnu-
make
```

# To edit embedded bitmap font

https://simple-terminal-psi.vercel.app

# License & Credits

MIT License  
Based on Aurélien APTEL <aurelien dot aptel at gmail dot com> bt source code.  
https://github.com/benob/rs97_st-sdl
