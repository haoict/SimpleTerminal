VERSION ?= 2.0.0
# compiler and linker
CROSS_COMPILE ?= 
CC = ${CROSS_COMPILE}gcc
SYSROOT ?= $(shell ${CC} --print-sysroot)
# flags
CFLAGS = -Os -Wall -I. -I${SYSROOT}/usr/include -DVERSION=\"${VERSION}\" -D_GNU_SOURCE=1 -D_REENTRANT -std=gnu11 -flto -Wno-unused-result
LDFLAGS = -L${SYSROOT}/usr/lib -lSDL2 -lSDL2_ttf -lpthread -lutil -s


ifeq ($(PLATFORM),rgb30)
CFLAGS += -DBR2 -DRGB30
else ifeq ($(PLATFORM),h700)
CFLAGS += -DBR2 -DH700
else ifeq ($(PLATFORM),r36s)
CFLAGS += -DBR2 -DR36S
else ifeq ($(PLATFORM),pi)
CFLAGS += -DBR2 -DRPI
endif

SRC = $(wildcard src/*.c)

build:
	@echo st build options:
	@echo "PLATFORM       = ${PLATFORM}"
	@echo "CROSS_COMPILE  = ${CROSS_COMPILE}"
	@echo "SYSROOT        = ${SYSROOT}"
	@echo "CFLAGS         = ${CFLAGS}"
	@echo "LDFLAGS        = ${LDFLAGS}"
	@echo "CC             = ${CC}"
	@echo "SRC            = ${SRC}"
	@echo "VERSION        = ${VERSION}"
	${CC} -o simple-terminal ${SRC} ${CFLAGS} ${LDFLAGS}

clean:
	@echo cleaning
	rm -f simple-terminal

.PHONY: build clean
