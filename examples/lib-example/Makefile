all: libexample.so

libexample.so:
	gcc -g -c -Wall -Werror -fpic example.c
	gcc -shared -o libexample.so example.o
	rm -rf example.o

clean:
	rm -rf libexample.so example.o
