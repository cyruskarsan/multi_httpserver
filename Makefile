#Makefile for httpserver.c

httpserver: httpserver.o
	gcc -pthread -o httpserver httpserver.o -lm

httpserver.o: httpserver.c
		gcc -c -std=gnu99 -Wall httpserver.c
clean:
	rm -f httpserver httpserver.o