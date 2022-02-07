
main: myhttpd.c
	gcc -Wall -std=c99 -O3 myhttpd.c -o myhttpd

clean:
	-rm -f myhttpd
