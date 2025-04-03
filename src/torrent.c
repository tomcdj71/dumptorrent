#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <inttypes.h>
#include "torrent.h"
#include "common.h"
#include "benc.h"
#include "scrapec.h"

extern int option_output;
extern int option_timeout;

static char *human_readable_number(uint64_t n)
{
    static char buff[51];
    const char *suffix[] = {"B", "K", "M", "G", "T"};
    double bytes = (double)n;
    int i = 0;

    while (bytes > 1024 && i < 4) {
        bytes /= 1024.0;
        i++;
    }

    snprintf(buff, 50, "%" PRIu64 " (%.3g%s)", n, bytes, suffix[i]);
    return buff;
}

int check_torrent(struct benc_entity *root, char *errbuf)
{
    struct benc_entity *info, *announce;

    if (root->type != BENC_DICTIONARY) {
        snprintf(errbuf, ERRBUF_SIZE, "root is not a dictionary");
        return 1;
    }

    announce = benc_lookup_string(root, "announce");
    if (announce == NULL || announce->type != BENC_STRING) {
        snprintf(errbuf, ERRBUF_SIZE, "no announce");
        return 1;
    }
    if (memcmp(announce->string.str, "http://", 7) != 0 &&
        memcmp(announce->string.str, "https://", 8) != 0 &&
        memcmp(announce->string.str, "udp://", 6) != 0) {
        snprintf(errbuf, ERRBUF_SIZE, "invalid announce url: \"%s\"", announce->string.str);
        errbuf[ERRBUF_SIZE - 1] = '\0';
        return 1;
    }

    info = benc_lookup_string(root, "info");
    if (info == NULL || info->type != BENC_DICTIONARY) {
        snprintf(errbuf, ERRBUF_SIZE, "no info");
        return 1;
    }

    /* Check info.name */
    if (benc_lookup_string(info, "name") == NULL ||
        benc_lookup_string(info, "name")->type != BENC_STRING) {
        snprintf(errbuf, ERRBUF_SIZE, "no info.name");
        return 1;
    }

    /* Check info.piece length */
    if (benc_lookup_string(info, "piece length") == NULL ||
        benc_lookup_string(info, "piece length")->type != BENC_INTEGER) {
        snprintf(errbuf, ERRBUF_SIZE, "no info.piece length");
        return 1;
    }

    /* Check single-file or multi-file length(s) */
    if (benc_lookup_string(info, "length") == NULL) {
        struct benc_entity *files = benc_lookup_string(info, "files");
        if (files == NULL || files->type != BENC_LIST || files->list.head == NULL) {
            snprintf(errbuf, ERRBUF_SIZE, "no info.length nor info.files");
            return 1;
        }

        /* For multi-file, check each entry */
        for (struct benc_entity *fileslist = files->list.head; fileslist != NULL; fileslist = fileslist->next) {
            if (fileslist->type != BENC_DICTIONARY) {
                snprintf(errbuf, ERRBUF_SIZE, "files list item is not dictionary");
                return 1;
            }
            if (benc_lookup_string(fileslist, "length") == NULL ||
                benc_lookup_string(fileslist, "length")->type != BENC_INTEGER ||
                benc_lookup_string(fileslist, "length")->integer < 0) {
                snprintf(errbuf, ERRBUF_SIZE, "files list item doesn't have valid length");
                return 1;
            }
            struct benc_entity *path = benc_lookup_string(fileslist, "path");
            if (path == NULL || path->type != BENC_LIST || path->list.head == NULL) {
                snprintf(errbuf, ERRBUF_SIZE, "files list item doesn't have path");
                return 1;
            }
            for (struct benc_entity *pathlist = path->list.head; pathlist != NULL; pathlist = pathlist->next) {
                if (pathlist->type != BENC_STRING) {
                    snprintf(errbuf, ERRBUF_SIZE, "path list item is not string");
                    return 1;
                }
            }
        }
    } else {
        if (benc_lookup_string(info, "length")->type != BENC_INTEGER ||
            benc_lookup_string(info, "length")->integer <= 0) {
            snprintf(errbuf, ERRBUF_SIZE, "info.length is not valid");
            return 1;
        }
    }

    return 0;
}

void print_field(struct benc_entity *root)
{
    printf("not implemented\n");
}

void show_torrent_info(struct benc_entity *root)
{
    struct benc_entity *announce, *info, *name, *piece_length, *length;
    unsigned char info_hash[20];
    long long int total_length;
    int max_filename_length;

    announce = benc_lookup_string(root, "announce");
    if (announce == NULL) {
        printf("can't find \"announce\" entry.\n");
        return;
    }

    info = benc_lookup_string(root, "info");
    if (info == NULL) {
        printf("can't find \"info\" entry.\n");
        return;
    }

    name = benc_lookup_string(info, "name");
    if (name == NULL) {
        printf("can't find \"name\" entry.\n");
        return;
    }

    piece_length = benc_lookup_string(info, "piece length");
    if (piece_length == NULL) {
        printf("can't find \"piece length\" entry.\n");
        return;
    }

    length = benc_lookup_string(info, "length");
    if (length != NULL) {
        total_length = length->integer;
        max_filename_length = name->string.length;
    } else {
        struct benc_entity *files = benc_lookup_string(info, "files");
        if (files == NULL) {
            printf("can't find neither \"length\" nor \"files\" entry in \"info\".\n");
            return;
        }
        total_length = 0;
        max_filename_length = 0;
        for (struct benc_entity *fileslist = files->list.head; fileslist != NULL; fileslist = fileslist->next) {
            struct benc_entity *path = benc_lookup_string(fileslist, "path");
            struct benc_entity *length2 = benc_lookup_string(fileslist, "length");
            if (!path || !length2) {
                printf("invalid file structure.\n");
                return;
            }
            total_length += length2->integer;
            int filename_length = -1;
            for (struct benc_entity *pathlist = path->list.head; pathlist != NULL; pathlist = pathlist->next) {
                filename_length += pathlist->string.length + 1;
            }
            if (max_filename_length < filename_length) {
                max_filename_length = filename_length;
            }
        }
    }

    if (option_output == OUTPUT_BRIEF) {
        printf("%s, %s\n", human_readable_number(total_length), name->string.str);
        return;
    }

    printf("Name:           %s\n", name->string.str);
    printf("Size:           %s\n", human_readable_number(total_length));
    printf("Announce:       %s\n", announce->string.str);

    if (option_output == OUTPUT_FULL) {
        benc_sha1_entity(info, info_hash);
        printf("Info Hash:      ");
        for (int i = 0; i < 20; i++)
            printf("%02x", info_hash[i]);
        printf("\n");

        printf("Piece Length:   %s\n", human_readable_number(piece_length->integer));
        if (benc_lookup_string(root, "creation date")) {
            time_t unix_time = benc_lookup_string(root, "creation date")->integer;
            printf("Creation Date:  %s", ctime(&unix_time));
        }
        if (benc_lookup_string(root, "comment")) {
            printf("Comment:        %s\n", benc_lookup_string(root, "comment")->string.str);
        }
        if (benc_lookup_string(info, "publisher")) {
            printf("Publisher:      %s\n", benc_lookup_string(info, "publisher")->string.str);
        }
        if (benc_lookup_string(info, "publisher-url")) {
            printf("Publisher URL:  %s\n", benc_lookup_string(info, "publisher-url")->string.str);
        }
        if (benc_lookup_string(root, "created by")) {
            printf("Created By:     %s\n", benc_lookup_string(root, "created by")->string.str);
        }
        if (benc_lookup_string(root, "encoding")) {
            printf("Encoding:       %s\n", benc_lookup_string(root, "encoding")->string.str);
        }
        if (benc_lookup_string(info, "private") && benc_lookup_string(info, "private")->integer) {
            printf("Private:        yes\n");
        }
    }

    printf("Files:\n");
    if (benc_lookup_string(info, "length") != NULL) {
        printf("                %s %s\n", name->string.str, human_readable_number(total_length));
    } else {
        struct benc_entity *fileslist;
        for (fileslist = benc_lookup_string(info, "files")->list.head; fileslist != NULL; fileslist = fileslist->next) {
            struct benc_entity *pathlist = benc_lookup_string(fileslist, "path")->list.head;
            long long file_length = benc_lookup_string(fileslist, "length")->integer;
            int filename_length = -1;
            printf("                ");
            for (; pathlist != NULL; pathlist = pathlist->next) {
                filename_length += pathlist->string.length + 1;
                printf("%s", pathlist->string.str);
                if (pathlist->next)
                    printf("/");
            }
            while (filename_length++ <= max_filename_length)
                printf(" ");
            puts(human_readable_number(file_length));
        }
    }

    if (option_output == OUTPUT_FULL && benc_lookup_string(root, "announce-list")) {
        struct benc_entity *tierlist;
        printf("Announce List:\n");
        for (tierlist = benc_lookup_string(root, "announce-list")->list.head; tierlist != NULL; tierlist = tierlist->next) {
            struct benc_entity *backuplist;
            printf("                ");
            for (backuplist = tierlist->list.head; backuplist != NULL; backuplist = backuplist->next) {
                printf("%s", backuplist->string.str);
                if (backuplist->next)
                    printf(", ");
            }
            printf("\n");
        }
    }

    if (option_output == OUTPUT_FULL && benc_lookup_string(root, "nodes")) {
        struct benc_entity *nodeslist;
        printf("Nodes:\n");
        for (nodeslist = benc_lookup_string(root, "nodes")->list.head; nodeslist != NULL; nodeslist = nodeslist->next) {
            if (nodeslist->list.head && nodeslist->list.head->next) {
                printf("                %s:%d\n",
                    nodeslist->list.head->string.str,
                    (int)nodeslist->list.head->next->integer);
            }
        }
    }
}

void scrape_torrent(struct benc_entity *root)
{
    static const int url_max = 64;
    struct benc_entity *announce, *info, *announce_list;
    unsigned char info_hash[20];
    int result[3];
    char errbuf[ERRBUF_SIZE];
    char *urls[url_max];
    int url_num = 0;

    info = benc_lookup_string(root, "info");
    if (info == NULL) {
        printf("info entry not found\n");
        return;
    }
    benc_sha1_entity(info, info_hash);

    announce_list = benc_lookup_string(root, "announce-list");
    if (announce_list == NULL) {
        announce = benc_lookup_string(root, "announce");
        if (announce == NULL) {
            printf("announce entry not found\n");
            return;
        }
        urls[url_num++] = announce->string.str;
    } else {
        /* collect all announce-list URLs, randomizing the order within each “tier” */
        for (struct benc_entity *tierlist = announce_list->list.head; tierlist != NULL; tierlist = tierlist->next) {
            int added_in_tier = 0;
            for (struct benc_entity *backuplist = tierlist->list.head;
                backuplist != NULL && url_num < url_max;
                backuplist = backuplist->next) {
                urls[url_num++] = backuplist->string.str;
                added_in_tier++;
                if (added_in_tier >= 2) {
                    int r = rand() % added_in_tier;
                    char *tmp = urls[url_num - 1];
                    urls[url_num - 1] = urls[url_num - 1 - r];
                    urls[url_num - 1 - r] = tmp;
                }
            }
            if (url_num >= url_max)
                break;
        }
    }

    for (int count = 0; count < url_num; count++) {
        printf("scraping %s ...\n", urls[count]);
        if (scrapec(urls[count], info_hash, result, errbuf) != 0) {
            printf("%s\n", errbuf);
        } else {
            printf("seeders=%d, completed=%d, leechers=%d\n", result[0], result[1], result[2]);
            return;
        }
    }
    printf("no more trackers to try.\n");
}
