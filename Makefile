-include config.mk

CC ?= clang
CFLAGS ?= -std=c23 -D_DEFAULT_SOURCE -D_XOPEN_SOURCE=600
LDFLAGS ?= -lcurses
SCDOC ?= scdoc

all: ee man ee.res

ee: ee.c
	$(CC) $(CFLAGS) ee.c -o ee $(LDFLAGS)

ee.res: ee.txt
	@if which genrb > /dev/null 2>&1; then \
		genrb ee.txt; \
		echo "Generated ee.res"; \
	else \
		echo "genrb not found, skipping ee.res generation"; \
	fi

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
	rm -f ee ee.1 init.ee.5 ee.res
