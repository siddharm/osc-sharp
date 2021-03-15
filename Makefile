include config.mk

CC = cc
CFLAGS = -std=c99 -Wall -Wextra -pedantic -Wno-unused-parameter -Os -g -O0 ${JACK_CFLAGS}
LDLIBS = ${JACK_LDLIBS}

BINS = \
	osc-sharp \


all: ${BINS}

LDLIBS.osc-sharp = -lm -lSDL2main -lSDL2


.c:
	${CC} ${CFLAGS} -o $@ $< ${LDLIBS} ${LDLIBS.$@}

clean:
	rm -f ${BINS} *.o *.exe a.out

.PHONY: all clean
