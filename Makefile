-include config.mk

CC ?= clang
CFLAGS ?= -std=c23 -D_DEFAULT_SOURCE -D_XOPEN_SOURCE=600
LDFLAGS ?= -lcurses

ee: ee.c
	$(CC) $(CFLAGS) ee.c -o ee $(LDFLAGS)

clean:
	rm -f ee
