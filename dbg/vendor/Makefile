LINENOISE = linenoise
LIBDWARF = libelfin/dwarf
LIBELF = libelfin/elf

all: ${LINENOISE}/linenoise.o ${LIBDWARF}/libdwarf++.a ${LIBELF}/libelf++.a

${LINENOISE}/linenoise.o:
	$(CC) ${LINENOISE}/linenoise.c -c -o ${LINENOISE}/linenoise.o -O2

${LIBDWARF}/libdwarf++.a:
	$(MAKE) -C ${LIBDWARF} libdwarf++.a

${LIBELF}/libelf++.a:
	$(MAKE) -C ${LIBELF} libelf++.a

clean:
	rm -f ${LINENOISE}/linenoise.o
	$(MAKE) -C ${LIBDWARF} clean
	$(MAKE) -C ${LIBELF} clean
