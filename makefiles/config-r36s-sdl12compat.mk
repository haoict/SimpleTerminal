#
#	Simple Terminal config for R36S SDL12COMPAT
#		based on RG350 ver https://github.com/jamesofarrell/st-sdl
#

INCS += -DR36S_SDL12COMPAT -DSDL12COMPAT

CFLAGS += -mtune=cortex-a53 -mcpu=cortex-a53
