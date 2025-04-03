dumptorrent: benc.c benc.h dumptorrent.c sha1.c sha1.h
	gcc -Wall -o dumptorrent dumptorrent.c benc.c sha1.c
