CROSS_COMPILE = /opt/toolchains/arm64/aarch64-linux-gnu-7.5.0-linaro/bin/aarch64-linux-gnu-

# compiler and linker
CC = ${CROSS_COMPILE}gcc
SYSROOT = /opt/toolchains/arm64/sysroot

INCS += -I$(SYSROOT)/usr/include -DTRIMUISP
CFLAGS += -mtune=cortex-a53 -mcpu=cortex-a53

