-include config.mk

CC ?= clang
CFLAGS ?= -std=c23 -D_DEFAULT_SOURCE -D_XOPEN_SOURCE=600
LDFLAGS ?= -lcurses
SCDOC ?= scdoc

all: ee man

ee: ee.c
	$(CC) $(CFLAGS) ee.c -o ee $(LDFLAGS)

man: ee.1

ee.1: ee.1.scd
	@if [ -n "$(SCDOC)" ]; then \
		$(SCDOC) < ee.1.scd > ee.1; \
		echo "Generated ee.1"; \
	else \
		echo "scdoc not found, skipping ee.1 generation"; \
	fi

clean:
	rm -f ee ee.1

