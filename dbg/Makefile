all: examples vendor
	@$(CXX) main.cpp vendor/linenoise.o -o dbg

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
