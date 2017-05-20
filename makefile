webserver : webserver.c common.o
	gcc -Wall -O2 webserver.c common.o -o webserver
common : common.c common.h
	gcc -Wall -O2 -c common.c
clean :
	rm webserver *.o