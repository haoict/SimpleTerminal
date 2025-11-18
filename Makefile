# st - simple terminal
# See LICENSE file for copyright and license details.

include makefiles/config.mk

ifeq ($(UNION_PLATFORM),rg35xxplus)
include makefiles/config-rg35xxplus.mk
else ifeq ($(UNION_PLATFORM),r36s)
include makefiles/config-r36s.mk
else ifeq ($(UNION_PLATFORM),upscale)
include makefiles/config-generic-linux-upscale.mk
else ifeq ($(UNION_PLATFORM),r36s-sdl12compat)
include makefiles/config-r36s-sdl12compat.mk
else ifeq ($(UNION_PLATFORM),buildroot_rgb30)
include makefiles/config-buildroot-rgb30.mk
else ifeq ($(UNION_PLATFORM),buildroot_h700)
include makefiles/config-buildroot-h700.mk
else ifeq ($(UNION_PLATFORM),buildroot_raspberrypi)
include makefiles/config-buildroot-raspberrypi.mk
endif

SRC = $(wildcard src/*.c)

all: options build

options:
	@echo st build options:
	@echo "UNION_PLATFORM = ${UNION_PLATFORM}"
	@echo "PREFIX         = ${PREFIX}"
	@echo "CROSS_COMPILE  = ${CROSS_COMPILE}"
	@echo "SYSROOT        = ${SYSROOT}"
	@echo "CFLAGS   = ${CFLAGS}"
	@echo "LDFLAGS  = ${LDFLAGS}"
	@echo "CC       = ${CC}"
	@echo "SRC      = ${SRC}"
	@echo "VERSION  = ${VERSION}"


build:
	mkdir -p build
	${CC} -o build/SimpleTerminal ${SRC} ${CFLAGS} ${LDFLAGS}

clean:
	@echo cleaning
	rm -rf build

.PHONY: all options clean
