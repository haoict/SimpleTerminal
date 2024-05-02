#
#	Simple Terminal config for RG35xx Plus
#		based on RG350 ver https://github.com/jamesofarrell/st-sdl
#
export LD_LIBRARY_PATH=/opt/rg35xxplus-toolchain/usr/lib

PREFIX=/opt/rg35xxplus-toolchain/usr/arm-buildroot-linux-gnueabihf/sysroot/usr
CROSS_COMPILE=/opt/rg35xxplus-toolchain/usr/bin/arm-buildroot-linux-gnueabihf-

INCS += -DRG35XXPLUS

CFLAGS += -marm -mtune=cortex-a53 -mcpu=cortex-a53 -mfpu=neon-fp-armv8 -mfloat-abi=hard
