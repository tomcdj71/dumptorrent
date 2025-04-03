#ifndef TORRENT_H
#define TORRENT_H

#include "benc.h"

// Evaluate if the torrent is valid
int check_torrent(struct benc_entity *root, char *errbuf);

// Show torrent info (e.g. name, size, etc.)
void show_torrent_info(struct benc_entity *root);

// Print a single field (brief usage for scripting)
void print_field(struct benc_entity *root);

// Display or fetch scrape info from a .torrent
void scrape_torrent(struct benc_entity *root);

#endif
