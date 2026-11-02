# SPDX-License-Identifier: GPL-2.0-or-later

CC ?= gcc
CFLAGS ?= -std=gnu11 -Wall -Wextra -O2
CFLAGS += -D_GNU_SOURCE

PREFIX ?= /usr/local
DESTDIR ?=

all: wexprefetchd wexpf-client test_sha256

DAEMON_SRC = wexprefetchd.c sha256.c trace.c pf_v17.c readahead.c ksm_tuner.c

wexprefetchd: $(DAEMON_SRC) protocol.h sha256.h trace.h pf_v17.h readahead.h ksm_tuner.h
	$(CC) $(CFLAGS) $(DAEMON_SRC) -o $@ -pthread

wexpf-client: wexpf-client.c protocol.h
	$(CC) $(CFLAGS) wexpf-client.c -o $@

test_sha256: test_sha256.c sha256.c sha256.h
	$(CC) $(CFLAGS) test_sha256.c sha256.c -o $@

test_readahead: test_readahead.c readahead.c readahead.h
	$(CC) $(CFLAGS) test_readahead.c readahead.c -o $@

test_ksm_tuner: test_ksm_tuner.c ksm_tuner.c ksm_tuner.h
	$(CC) $(CFLAGS) test_ksm_tuner.c ksm_tuner.c -o $@

# Unit tests for the modules; the daemon suite needs a test PE: python3 test_wexprefetchd.py ./wexprefetchd FILE.exe
check: test_sha256 test_readahead test_ksm_tuner
	./test_sha256
	TMPDIR=/var/tmp ./test_readahead
	./test_ksm_tuner
	python3 -I test_pf_v17.py

# Daemon traces, readahead and KSM tuner mode: make check-daemon PE=/path/to/small.exe
check-daemon: wexprefetchd
	python3 -I test_wexprefetchd.py ./wexprefetchd $(PE)
	python3 test_trace.py ./wexprefetchd $(PE)

install: all
	install -d $(DESTDIR)$(PREFIX)/sbin
	install -d $(DESTDIR)$(PREFIX)/bin
	install -m 0755 wexprefetchd $(DESTDIR)$(PREFIX)/sbin/wexprefetchd
	install -m 0755 wexpf-client $(DESTDIR)$(PREFIX)/bin/wexpf-client

clean:
	rm -f wexprefetchd wexpf-client test_sha256 test_readahead test_ksm_tuner

.PHONY: all check check-daemon install clean
