TARGET = EBOOT.PBP

OBJS = src/main.o \
       src/game.o \
       src/psp_game.o \
       src/psp_input.o

INCDIR = include
CFLAGS = -O2 -G0 -Wall -I$(INCDIR)
CXXFLAGS = $(CFLAGS)
ASFLAGS = $(CFLAGS)

LIBS =

EXTRA_TARGETS = EBOOT.PBP
PSP_EBOOT_TITLE = Yenzangs Trip

PSPSDK = $(shell psp-config --pspsdk-path)

include $(PSPSDK)/lib/build.mak
