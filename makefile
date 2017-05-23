webserver : webserver.c common.o
	gcc -Wall -O2 -pthread webserver.c common.o -o webserver
common : common.c common.h
	gcc -Wall -O2 -pthread -c common.c
clean :
	rm webserver *.o