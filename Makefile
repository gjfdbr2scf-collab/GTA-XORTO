TARGET = EBOOT.PBP

OBJS = src/main.o \
       src/game.o \
       src/psp_game.o \
       src/psp_input.o \
       mainmenu.o

INCDIR = include

CFLAGS = -O2 -G0 -Wall
ASFLAGS = $(CFLAGS)

LIBS = -lpspgu

EXTRA_TARGETS = EBOOT.PBP

PSP_EBOOT_TITLE = Ginseng Strip GTA

PSP_FW_VERSION = 600

PSPSDK = $(shell psp-config --pspsdk-path)

include $(PSPSDK)/lib/build.mak


mainmenu.o: assets/MainMenu.raw
	bin2o -i assets/MainMenu.raw mainmenu.o MainMenu
