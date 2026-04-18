-include config.mk

CC ?= clang
CFLAGS ?= -std=c23 -D_DEFAULT_SOURCE -D_XOPEN_SOURCE=600
LDFLAGS ?= -lcurses
SCDOC ?= scdoc

PREFIX ?= /usr/local
BINDIR = $(PREFIX)/bin
MANDIR = $(PREFIX)/share/man
RESDIR = $(PREFIX)/share/ee

all: ee man root.res

ee: ee.c
	$(CC) $(CFLAGS) ee.c -o ee $(LDFLAGS)

root.res: ee.txt
	@if which genrb > /dev/null 2>&1; then \
		genrb ee.txt; \
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
	rm -f ee ee.1 init.ee.5 root.res cluster.txt symorder.txt perf.data

propeller:
	chmod +x optimize_ee.sh
	./optimize_ee.sh
