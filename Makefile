-include config.mk

CC ?= clang
CFLAGS ?= -std=c23 -O2 -D_DEFAULT_SOURCE -D_XOPEN_SOURCE=600
LDFLAGS ?= -lcursesw

# Feature flags (set in config.mk or via configure.sh)
CFLAGS += -DHAS_LSP -DHAS_TREE_SITTER -DHAS_ICU
SCDOC ?= scdoc

PREFIX ?= /usr/local
BINDIR = $(PREFIX)/bin
MANDIR = $(PREFIX)/share/man
RESDIR = $(PREFIX)/share/ee

all: ee man root.res

SRCS := ee.c input.c render.c state.c fileio.c lsp.c delete.c search.c format.c menu.c undo.c theme.c
OBJS := $(SRCS:.c=.o)

ee: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

root.res: locale.icu
	@if which genrb > /dev/null 2>&1; then \
		genrb locale.icu; \
		echo "Generated root.res"; \
	else \
		echo "genrb not found, skipping root.res generation"; \
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

install: all
	install -d $(DESTDIR)$(BINDIR)
	install -m 755 ee $(DESTDIR)$(BINDIR)/ee
	install -d $(DESTDIR)$(MANDIR)/man1
	install -m 644 ee.1 $(DESTDIR)$(MANDIR)/man1/ee.1
	install -d $(DESTDIR)$(MANDIR)/man5
	install -m 644 init.ee.5 $(DESTDIR)$(MANDIR)/man5/init.ee.5
	@if [ -f root.res ]; then \
		install -d $(DESTDIR)$(RESDIR); \
		install -m 644 root.res $(DESTDIR)$(RESDIR)/root.res; \
	fi

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/ee
	rm -f $(DESTDIR)$(MANDIR)/man1/ee.1
	rm -f $(DESTDIR)$(MANDIR)/man5/init.ee.5
	rm -rf $(DESTDIR)$(RESDIR)

clean:
	rm -f ee *.o ee.1 init.ee.5 root.res cluster.txt symorder.txt perf.data

propeller:
	chmod +x optimize_ee.sh
	./optimize_ee.sh

# Minimal build (no LSP, tree-sitter, or ICU)
minimal:
	$(CC) $(CFLAGS) -DHAS_MINIMAL -o ee $(SRCS) $(LDFLAGS)

# Link-Time Optimization
lto:
	$(CC) $(CFLAGS) -flto -o ee $(OBJS) $(LDFLAGS)

# PGO for minimal builds
pgo-minimal:
	$(CC) $(CFLAGS) -DHAS_MINIMAL -fprofile-generate -o ee $(SRCS) $(LDFLAGS)
	@echo "Run the binary to generate profile data, then rerun 'make pgo-minimal'"
	@echo "Example: ./ee test_input.txt"

pgo-minimal-final:
	$(CC) $(CFLAGS) -DHAS_MINIMAL -fprofile-use -o ee $(SRCS) $(LDFLAGS)

# Static linking
static:
	$(CC) $(CFLAGS) -static -o ee $(OBJS) $(LDFLAGS)

# Binary size breakdown
size:
	size ee
	objdump -t ee | sort -k 5 > symbols.txt

# Profiling
perf:
	perf stat -d ./ee
