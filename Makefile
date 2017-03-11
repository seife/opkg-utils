UTILS = opkg-build opkg-unbuild opkg-make-index opkg.py opkg-list-fields \
	arfile.py opkg-buildpackage opkg-diff opkg-extract-file opkg-show-deps \
	opkg-compare-indexes update-alternatives

MANPAGES = opkg-build.1

DESTDIR =
PREFIX ?= /usr/local
bindir ?= $(PREFIX)/bin
mandir ?= $(PREFIX)/man

CFLAGS ?= -W -Wall -O2 -g
UTILS += update-alternatives.bin

# in order to cancel the implicit rules, which would compile update-alternatives.c to
# update-alternatives and overwrite the script...
.SUFFIXES:
SUFFIXES :=
#
.SUFFIXES: .1

%.1: %
	pod2man -r "" -c "opkg-utils Documentation" $< $@

all: $(UTILS) $(MANPAGES)

update-alternatives.bin: update-alternatives.c
	$(CC) $(CFLAGS) $< -o $@

install: all
	install -d $(DESTDIR)$(bindir)
	install -m 755 $(UTILS) $(DESTDIR)$(bindir)
	install -d $(DESTDIR)$(mandir)
	for m in $(MANPAGES); \
	do \
		install -d $(DESTDIR)$(mandir)/man$${m##*.}; \
		install -m 644 $(MANPAGES) $(DESTDIR)$(mandir)/man$${m##*.}; \
	done
	mv $(DESTDIR)$(bindir)/update-alternatives.bin $(DESTDIR)$(bindir)/update-alternatives

.PHONY: install all
