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

# run commands when open
./simple-terminal -e "ls -la" "uname -a" whoami 
```

# Build with buildroot toolchain
you can build everything for the target device with buildroot:  
https://github.com/haoict/TiniLinux/blob/master/README.md#build

or build the toolchain only and build simple-terminal separately
```bash
# build toolchain
cd buildroot
make O=../TiniLinux/output.arm64 BR2_EXTERNAL=../TiniLinux toolchain_arm64_defconfig
cd ../TiniLinux
make -j $(nproc)

# build simple-terminal
cd package/simple-terminal
export CROSS_COMPILE=/home/haoict/TiniLinux/output.arm64/host/bin/aarch64-buildroot-linux-gnu-
make
```

# License & Credits
MIT License  
Based on  Aurélien APTEL <aurelien dot aptel at gmail dot com> bt source code.  
https://github.com/benob/rs97_st-sdl
