CFLAGS=-Wall -g -DDEBUG -Ilzx_compress
LDFLAGS=-g -Llzx_compress
LDLIBS=-llzxcomp -lm
VERSION=0.1

all: hhm

clean:
	rm -f hhm hhm.exe core *.stackdump

dist: README ChangeLog Makefile COPYING FAQ hhm.c TODO
	rm -rf hhm-$(VERSION)
	mkdir hhm-$(VERSION)
	cp README ChangeLog Makefile COPYING FAQ hhm.c TODO hhm-$(VERSION)
	tar zcf hhm-$(VERSION).tar.gz hhm-$(VERSION)
	rm -rf hhm-$(VERSION)

COPYING:
	cp /usr/share/automake*/COPYING COPYING
