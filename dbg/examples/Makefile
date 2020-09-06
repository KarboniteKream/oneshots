all: func hello variables

func:
	$(CC) func.c -no-pie -g -o func

hello:
	$(CC) hello.c -no-pie -g -o hello

variables:
	$(CC) variables.c -no-pie -g -o variables

clean:
	rm -f func hello variables
