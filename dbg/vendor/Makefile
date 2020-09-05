all: linenoise.o

linenoise.o:
	$(CC) linenoise/linenoise.c -c -O2
	cp linenoise/linenoise.h .

clean:
	rm -f linenoise.h linenoise.o
