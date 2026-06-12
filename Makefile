# Makefile for gawk-udp extension
#
# Prerequisites:
#   gawk >= 5.0  (for the extension API used here)
#   gawk development headers (gawkapi.h)
#
# On Debian/Ubuntu:
#   apt install gawk
#   # gawkapi.h ships with the gawk source or gawk-dev package;
#   # if not found automatically, set GAWK_INCLUDE below or copy
#   # gawkapi.h from the gawk source tree into this directory.
#
# On Fedora/RHEL:
#   dnf install gawk-devel
#
# Building:
#   make          -- builds udp.so in the current directory
#   make install  -- copies udp.so to $(GAWK_EXTDIR) (default: /usr/local/lib/gawk)
#   make check    -- quick smoke-test (requires gawk-json as well)

CC      ?= gcc
CFLAGS  ?= -O2 -Wall -Wextra -fPIC

# Where to find gawkapi.h.
# The header is often already on the default include path (/usr/include).
# If it lives somewhere else, override: make GAWK_INCLUDE=/path/to/dir
GAWK_INCLUDE ?= $(shell \
  for d in /usr/include /usr/local/include /usr/include/gawk /usr/local/include/gawk .; do \
    [ -f "$$d/gawkapi.h" ] && echo "$$d" && break; \
  done)

# Only add -I if we found a directory (avoids bare "-I" when empty).
GAWK_INC_FLAG = $(if $(GAWK_INCLUDE),-I$(GAWK_INCLUDE),)

# Installation directory for GAWK extensions.
GAWK_EXTDIR ?= /usr/local/lib/gawk

TARGET = udp.so

$(TARGET): udp.c
	$(CC) $(CFLAGS) $(GAWK_INC_FLAG) -shared -o $@ $< -ldl

install: $(TARGET)
	install -d $(GAWK_EXTDIR)
	install -m 644 $(TARGET) $(GAWK_EXTDIR)/

clean:
	rm -f $(TARGET) *.o

check: $(TARGET)
	@echo "--- udp_open/close smoke test ---"
	AWKLIBPATH=$(CURDIR) gawk '@load "udp"; BEGIN { \
		fd = udp_open(0); \
		if (fd < 0) { print "FAIL: udp_open"; exit 1 } \
		print "udp_open returned fd=" fd; \
		udp_close(fd); \
		print "PASS" \
	}'

.PHONY: install clean check
