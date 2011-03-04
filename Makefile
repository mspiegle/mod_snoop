#
# Makefile - The main makefile for mod_snoop
# Copyright (C) 2011 Michael Spiegle (mike@nauticaltech.com)
#

APXS=/usr/sbin/apxs

MODDIR=src
SOURCE=src/mod_snoop.c
MODULE=mod_snoop.so
export LDFLAGS=""
CFLAGS="-g -O0 -Iinclude -Wall -std=gnu99"
APACHE=$(shell $(APXS) -q LIBEXECDIR)

all: mod_snoop.so

test:
	curl http://localhost/

mod_snoop.so:
	$(APXS) -c -o $(MODDIR)/$(MODULE) -Wc,$(CFLAGS) $(SOURCE)
	mv -f src/.libs/$(MODULE) $(MODDIR)/

clean:
	rm -f src/*.{so,la,slo,o,lo}
	rm -rf src/.libs

install:
	sudo cp $(MODDIR)/$(MODULE) $(APACHE)

uninstall:
	sudo rm -fv $(APACHE)/$(MODULE)

run-debug:
	sudo gdb --args /usr/sbin/apache2 -X -D DEFAULT_VHOST -D INFO -D PROXY -d /usr/lib64/apache2 -f /etc/apache2/httpd.conf -k start
	sudo killall apache2 2>&1 >/dev/null

run-valgrind:
	sudo valgrind --tool=callgrind /usr/sbin/apache2 -X -D DEFAULT_VHOST -D PROXY -D INFO -d /usr/lib64/apache2 -f /etc/apache2/httpd.conf -k start

run-strace:
	sudo strace -s 4096 -f -ff /usr/sbin/apache2 -X -D DEFAULT_VHOST -D INFO -D PROXY -d /usr/lib64/apache2 -f /etc/apache2/httpd.conf -k start

debug:
	make clean && make && make install && make run-debug

strace:
	make clean && make && make install && make run-strace

profile:
	make clean && make && make install && make run-valgrind

