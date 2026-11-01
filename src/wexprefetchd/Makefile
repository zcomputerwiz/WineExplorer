# SPDX-License-Identifier: GPL-2.0-or-later

CC ?= gcc
CFLAGS ?= -std=gnu11 -Wall -Wextra -O2
CFLAGS += -D_GNU_SOURCE

PREFIX ?= /usr/local
DESTDIR ?=

all: wexprefetchd wexpf-client test_sha256

wexprefetchd: wexprefetchd.c sha256.c protocol.h sha256.h
	$(CC) $(CFLAGS) wexprefetchd.c sha256.c -o $@ -pthread

wexpf-client: wexpf-client.c protocol.h
	$(CC) $(CFLAGS) wexpf-client.c -o $@

test_sha256: test_sha256.c sha256.c sha256.h
	$(CC) $(CFLAGS) test_sha256.c sha256.c -o $@

check: test_sha256
	./test_sha256

install: all
	install -d $(DESTDIR)$(PREFIX)/sbin
	install -d $(DESTDIR)$(PREFIX)/bin
	install -m 0755 wexprefetchd $(DESTDIR)$(PREFIX)/sbin/wexprefetchd
	install -m 0755 wexpf-client $(DESTDIR)$(PREFIX)/bin/wexpf-client

clean:
	rm -f wexprefetchd wexpf-client test_sha256

.PHONY: all check install clean
