CC ?= gcc
CFLAGS ?= -Wall -Wextra -O2 -std=gnu11 -D_GNU_SOURCE
LDFLAGS ?= -lncursesw

SRCS = main.c ui.c screens.c
OBJS = $(SRCS:.c=.o)
TARGET = textsetup

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(OBJS) -o $@ $(LDFLAGS)

%.o: %.c textsetup.h
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)

install: $(TARGET)
	install -D -m 0755 $(TARGET) $(DESTDIR)/setup/textsetup

.PHONY: all clean install
