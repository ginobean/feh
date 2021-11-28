include ../config.mk

TARGETS = \
	events.c \
	feh_png.c \
	filelist.c \
	getopt.c \
	getopt1.c \
	gib_hash.c \
	gib_imlib.c \
	gib_list.c \
	gib_style.c \
	imlib.c \
	index.c \
	keyevents.c \
	list.c \
	main.c \
	md5.c \
	menu.c \
	multiwindow.c \
	options.c \
	signals.c \
	slideshow.c \
	thumbnail.c \
	timers.c \
	utils.c \
	wallpaper.c \
	winwidget.c

CXX = g++
CXX_SOURCES = $(wildcard *.cpp)
CXX_OBJECTS = $(patsubst %.cpp,%.o,$(CXX_SOURCES))


ifeq (${exif},1)
	TARGETS += \
		exif.c \
		exif_canon.c \
		exif_nikon.c
endif

ifneq (${verscmp},1)
	TARGETS += strverscmp.c
endif

OBJECTS = ${TARGETS:.c=.o}

%.o : %.c
	$(CC) $(CFLAGS) -c "$<" -o "$@"

%.o : %.cpp
	$(CXX) $(CXXFLAGS) -c "$<" -o "$@"

I_SRCS = ${shell echo *.raw}
I_DSTS = ${I_SRCS:.raw=.inc}

feh: deps.mk ${OBJECTS} ${CXX_OBJECTS} ${I_DSTS}
	${CXX} ${LDFLAGS} ${CXX_LFLAGS} ${CXXFLAGS} -o $@ ${OBJECTS} ${CXX_OBJECTS} ${LDLIBS}

include deps.mk

.raw.inc:
	sed 's/^\(.*\)$$/"\1\\n"/' < ${@:.inc=.raw} > $@

fehrc.inc: fehrc.raw
help.inc: help.raw

# CFLAGS might contain include paths needed to resolve includes in headers
deps.mk: ${TARGETS} ${CXX_SOURCES} ${I_DSTS}
	${CC} ${CFLAGS} -MM ${TARGETS} > $@
	${CXX} ${CXXFLAGS} -MM ${CXX_SOURCES} >> $@

clean:
	rm -f feh *.o *.inc

.PHONY: clean

.SUFFIXES: .inc .raw
