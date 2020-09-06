LINENOISE = vendor/linenoise
LIBDWARF = vendor/libelfin/dwarf
LIBELF = vendor/libelfin/elf

all: examples vendor
	$(CXX) main.cpp ${LINENOISE}/linenoise.o ${LIBDWARF}/libdwarf++.a ${LIBELF}/libelf++.a -o dbg -Wall

.PHONY: examples
examples:
	$(MAKE) -C examples

.PHONY: vendor
vendor:
	$(MAKE) -C vendor

clean:
	$(MAKE) -C examples clean
	$(MAKE) -C vendor clean
	rm -f dbg
