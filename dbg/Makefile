all: examples vendor
	@$(CXX) main.cpp vendor/linenoise.o -o dbg -Wall -Wextra -pedantic

.PHONY: examples
examples:
	@$(MAKE) -C examples

.PHONY: vendor
vendor:
	@$(MAKE) -C vendor

clean:
	@$(MAKE) -C examples clean
	@$(MAKE) -C vendor clean
	@rm -f dbg
