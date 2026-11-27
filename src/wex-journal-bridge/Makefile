# SPDX-License-Identifier: GPL-2.0-or-later
# wex-journal-bridge: the systemd journal to the Event Log and back (docs/EVENTLOG_JOURNAL.md)

CC ?= gcc
CFLAGS ?= -std=gnu11 -Wall -Wextra -O2
CFLAGS += -D_GNU_SOURCE -I../common -I../../wine/include/wine
LIBS = -lsystemd

PREFIX ?= /usr/local
DESTDIR ?=

all: wex-journal-bridge

wex-journal-bridge: wex-journal-bridge.c ../common/wex_peer.h ../../wine/include/wine/wex_journal.h
	$(CC) $(CFLAGS) wex-journal-bridge.c -o $@ $(LIBS)

install: all
	install -d $(DESTDIR)$(PREFIX)/libexec/wineexplorer
	install -m 0755 wex-journal-bridge $(DESTDIR)$(PREFIX)/libexec/wineexplorer/wex-journal-bridge

clean:
	rm -f wex-journal-bridge

.PHONY: all install clean
