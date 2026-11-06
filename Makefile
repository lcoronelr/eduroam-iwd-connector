CC = gcc
CFLAGS = -Wall -Wextra -O2
TARGET = eduwifi
PREFIX = /usr/local

all: $(TARGET)

$(TARGET): eduwifi.c
	$(CC) $(CFLAGS) eduwifi.c -o $(TARGET)

install: $(TARGET)
	install -Dm755 $(TARGET) $(DESTDIR)$(PREFIX)/bin/$(TARGET)

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/$(TARGET)

clean:
	rm -f $(TARGET)

.PHONY: all install uninstall clean
