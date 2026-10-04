TARGET = EBOOT.PBP

OBJS = src/main.o \
       src/psp_game.o \
       src/psp_input.o \
       title.o \
       language.o \
       mainmenu.o \
       music.o

INCDIR = include

CFLAGS = -O2 -G0 -Wall
ASFLAGS = $(CFLAGS)

LIBS = -lpsputility -lpspvaudio -lpspaudio -lpspgum -lpspgu -lpspdisplay -lpspctrl -lpspdebug

EXTRA_TARGETS = EBOOT.PBP

PSP_EBOOT_TITLE = Ginseng Strip GTA

PSP_FW_VERSION = 600

PSPSDK = $(shell psp-config --pspsdk-path)

include $(PSPSDK)/lib/build.mak

title.o: assets/Title.raw
	bin2o -i assets/Title.raw title.o Title

language.o: assets/LanguageSelection.raw
	bin2o -i assets/LanguageSelection.raw language.o LanguageSelection

mainmenu.o: assets/MainMenu.raw
	bin2o -i assets/MainMenu.raw mainmenu.o MainMenu

music.o: assets/Music.pcm
	bin2o -i assets/Music.pcm music.o Music
