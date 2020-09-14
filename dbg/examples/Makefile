OPTS = -no-pie -fno-omit-frame-pointer -g

all: func hello variables

func:
	$(CC) func.c $(OPTS) -o func

hello:
	$(CC) hello.c $(OPTS) -o hello

variables:
	$(CC) variables.c $(OPTS) -o variables

clean:
	rm -f func hello variables
