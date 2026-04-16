-include config.mk

CC ?= clang
CFLAGS ?= -std=c23 -D_DEFAULT_SOURCE -D_XOPEN_SOURCE=600
LDFLAGS ?= -lcurses
SCDOC ?= scdoc

all: ee man

ee: ee.c
	$(CC) $(CFLAGS) ee.c -o ee $(LDFLAGS)

man: ee.1 init.ee.5

ee.1: ee.1.scd
	@if [ -n "$(SCDOC)" ]; then \
		$(SCDOC) < ee.1.scd > ee.1; \
		echo "Generated ee.1"; \
	else \
		echo "scdoc not found, skipping ee.1 generation"; \
	fi

init.ee.5: init.ee.5.scd
	@if [ -n "$(SCDOC)" ]; then \
		$(SCDOC) < init.ee.5.scd > init.ee.5; \
		echo "Generated init.ee.5"; \
	else \
		echo "scdoc not found, skipping init.ee.5 generation"; \
	fi

clean:
	rm -f ee ee.1 init.ee.5

