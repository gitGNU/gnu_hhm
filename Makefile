CFLAGS=-g -DDEBUG -Ilzx_compress
LDFLAGS=-g -Llzx_compress
LDLIBS=-llzxcomp -lm

all: hhm

clean:
	rm -f hhm hhm.exe core *.stackdump
