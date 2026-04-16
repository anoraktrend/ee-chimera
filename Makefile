-include config.mk

CC = clang
CFLAGS ?= -std=c23 -DNO_CATGETS -D_DEFAULT_SOURCE -D_XOPEN_SOURCE=600
LDFLAGS ?= -lcurses -ltree-sitter -ltree-sitter-c -licuuc -licuio

ee: ee.c
	$(CC) $(CFLAGS) ee.c -o ee $(LDFLAGS)

clean:
	rm -f ee
