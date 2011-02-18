#
# Makefile - The main makefile for mod_snoop
# Copyright (C) 2011 Michael Spiegle (mike@nauticaltech.com)
#

APXS=/usr/sbin/apxs

MODDIR=src
SOURCE=src/mod_snoop.c
MODULE=mod_snoop.so
APACHE=$(shell $(APXS) -q LIBEXECDIR)
CFLAGS="-g -O0 -Iinclude -Wall -std=gnu99"

all: mod_snoop.so

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

run:
	make clean
	make
	make install
	#sudo gdb -d /home/mspiegle/tmp/httpd-source --args /usr/sbin/apache2 -X -D DEFAULT_VHOST -D INFO -D PROXY -d /usr/lib64/apache2 -f /etc/apache2/httpd.conf -k start
	sudo gdb --args /usr/sbin/apache2 -X -D DEFAULT_VHOST -D INFO -D PROXY -d /usr/lib64/apache2 -f /etc/apache2/httpd.conf -k start
	sudo killall apache2 2>&1 >/dev/null

valgrind:
	sudo valgrind --tool=callgrind /usr/sbin/apache2 -X -D DEFAULT_VHOST -D PROXY -D INFO -d /usr/lib64/apache2 -f /etc/apache2/httpd.conf -k start

debug:
	make clean && make && make install && make run

profile:
	make clean && make && make install && make valgrind

