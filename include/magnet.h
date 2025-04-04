#ifndef MAGNET_H
#define MAGNET_H

int parse_magnet_uri(const char *uri, unsigned char out_infohash[20], char ***out_trackers, int *out_tracker_count, char *out_display_name, char *errbuf);

#endif
