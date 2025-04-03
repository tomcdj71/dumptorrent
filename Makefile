all: dumptorrent scrapec

dumptorrent: dumptorrent.c benc.c benc.h scrapec.c scrapec.h sha1.c sha1.h common.h
	gcc -Wall -o dumptorrent dumptorrent.c benc.c scrapec.c sha1.c

scrapec: scrapec.c scrapec.h benc.c benc.h sha1.c sha1.h common.h
	gcc -Wall -DBUILD_MAIN -o scrapec scrapec.c benc.c sha1.c

.PHONY: clean
clean:
	rm -f dumptorrent scrapec
