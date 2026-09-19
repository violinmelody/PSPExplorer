TARGET = PSPExplorer
OBJS = src/main.o src/app.o src/explorer.o src/renderer.o src/settings.o src/image.o src/audio.o
INCDIR = include
CFLAGS = -O2 -G0 -Wall -Wextra -Wshadow -Wstrict-prototypes
CXXFLAGS = $(CFLAGS) -fno-exceptions -fno-rtti
ASFLAGS = $(CFLAGS)
LIBS = -lpng -ljpeg -lz -lm -lpspgu -lpspdisplay -lpspctrl -lpspaudio -lpspmp3 -lpsprtc
EXTRA_TARGETS = EBOOT.PBP
PSP_EBOOT_TITLE = PSPExplorer
PSP_EBOOT_ICON = assets/ICON0.PNG
SFOFLAGS += -s APP_VER=01.00
PSP_EBOOT_SFO = PARAM.SFO
BUILD_PRX = 0
PSP_FW_VERSION = 660
include $(PSPSDK)/lib/build.mak
