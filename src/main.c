#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <stdint.h>
#include <inttypes.h>
#include "common.h"
#include "benc.h"
#include "scrapec.h"
#include "torrent.h"
#include "magnet.h"

/* -------------------------------------------------------------------------
    VERSION DEFINITION
   ------------------------------------------------------------------------- */
#ifndef DUMPTORRENT_VERSION
#define DUMPTORRENT_VERSION "unknown"
#endif

int   option_output   = OUTPUT_DEFAULT;
char *option_field    = NULL;
int          option_timeout  = 0;
char        *option_tracker  = NULL;
char        *option_info_hash = NULL;

/* -------------------------------------------------------------------------
    UTILITY FUNCTIONS
   ------------------------------------------------------------------------- */
static int is_magnet_uri(const char *str) {
    return strncmp(str, "magnet:?", 8) == 0;

}
static int do_scrapec(void)
{
    unsigned char info_hash[20];
    int result[3];
    char errbuf[ERRBUF_SIZE];

    /* Convert the hex info_hash string to 20-byte binary */
    for (int i = 0; i < 20; i++) {
        int c;
        /* high nibble */
        if (option_info_hash[i * 2] >= '0' && option_info_hash[i * 2] <= '9')
            c = (option_info_hash[i * 2] - '0') << 4;
        else if (option_info_hash[i * 2] >= 'a' && option_info_hash[i * 2] <= 'f')
            c = (option_info_hash[i * 2] - 'a' + 10) << 4;
        else if (option_info_hash[i * 2] >= 'A' && option_info_hash[i * 2] <= 'F')
            c = (option_info_hash[i * 2] - 'A' + 10) << 4;
        else {
            printf("invalid infohash value (non-hex). Must be 40 hex characters.\n");
            return 1;
        }

        /* low nibble */
        if (option_info_hash[i * 2 + 1] >= '0' && option_info_hash[i * 2 + 1] <= '9')
            c |= option_info_hash[i * 2 + 1] - '0';
        else if (option_info_hash[i * 2 + 1] >= 'a' && option_info_hash[i * 2 + 1] <= 'f')
            c |= option_info_hash[i * 2 + 1] - 'a' + 10;
        else if (option_info_hash[i * 2 + 1] >= 'A' && option_info_hash[i * 2 + 1] <= 'F')
            c |= option_info_hash[i * 2 + 1] - 'A' + 10;
        else {
            printf("invalid infohash value (non-hex). Must be 40 hex characters.\n");
            return 1;
        }

        info_hash[i] = (unsigned char) c;
    }

    if (scrapec(option_tracker, info_hash, result, errbuf) != 0) {
        printf("%s\n", errbuf);
        return 1;
    }

    printf("seeders=%d, completed=%d, leechers=%d\n", result[0], result[1], result[2]);
    return 0;
}

/* -------------------------------------------------------------------------
    PRINT USAGE / HELP
   ------------------------------------------------------------------------- */
static void print_help(const char *prog)
{
    printf("Dump Torrent v%s\n", DUMPTORRENT_VERSION);
    printf("Usage: %s [options] [--] <files.torrent...>\n", prog);
    printf("  -t: validate torrent files only (test mode)\n");
    printf("  -f <field>: output a single field (e.g. 'name'), one per file\n");
    printf("  -b: brief dump\n");
    printf("  -v: full dump\n");
    printf("  -d: raw hierarchical dump\n");
    printf("  -s: show scrape info (via built-in logic)\n");
    printf("  -w <timeout>: network timeout in seconds\n");
    printf("  -scrape <url> <infohash>: scrape a particular infohash from the given tracker\n");
    printf("  -V: print dumptorrent version and exit\n");
    printf("  -h: print this help message\n\n");
    printf("Examples:\n");
    printf("  %s somefile.torrent                 (default output)\n", prog);
    printf("  %s -t file1.torrent file2.torrent   (test each file)\n", prog);
    printf("  %s -scrape http://tracker/ann ...   (scrape a specific infohash)\n", prog);
}

/* -------------------------------------------------------------------------
   MAIN
   ------------------------------------------------------------------------- */
int main(int argc, char *argv[])
{
    struct string_list {
        char *str;
        struct string_list *next;
    };

    struct string_list *head = NULL, *tail = NULL;
    struct string_list *curr, *temp;
    char errbuf[ERRBUF_SIZE];
    int count;

    srand((unsigned) time(NULL));

    /* Parse command-line arguments */
    for (count = 1; count < argc; count++) {
        if (strcmp(argv[count], "-h") == 0) {
            print_help(argv[0]);
            return 0;
        } 
        else if (strcmp(argv[count], "-V") == 0) {
            /* PRINT VERSION AND EXIT */
            printf("dumptorrent version %s\n", DUMPTORRENT_VERSION);
            return 0;
        }
        else if (strcmp(argv[count], "-t") == 0) {
            option_output = OUTPUT_TEST;
        } 
        else if (strcmp(argv[count], "-f") == 0) {
            if (count + 1 >= argc) {
                printf("-f requires a <field> argument.\n");
                return 1;
            }
            option_output = OUTPUT_FIELD;
            option_field = argv[++count];
        } 
        else if (strcmp(argv[count], "-b") == 0) {
            option_output = OUTPUT_BRIEF;
        } 
        else if (strcmp(argv[count], "-v") == 0) {
            option_output = OUTPUT_FULL;
        } 
        else if (strcmp(argv[count], "-d") == 0) {
            option_output = OUTPUT_DUMP;
        } 
        else if (strcmp(argv[count], "-s") == 0) {
            option_output = OUTPUT_SCRAPE;
        } 
        else if (strcmp(argv[count], "-w") == 0) {
            if (count + 1 >= argc) {
                printf("-w requires an integer <timeout> argument.\n");
                return 1;
            }
            option_timeout = strtol(argv[++count], NULL, 10);
            if (option_timeout < 0) {
                printf("timeout must be a non-negative integer. \"%s\" is invalid.\n", argv[count]);
                print_help(argv[0]);
                return 1;
            }
        } 
        else if (strcmp(argv[count], "-scrape") == 0) {
            if (count + 2 >= argc) {
                printf("url and infohash expected for -scrape.\n");
                print_help(argv[0]);
                return 1;
            }
            option_output = OUTPUT_SCRAPEC;
            option_tracker  = argv[count + 1];
            option_info_hash = argv[count + 2];
            count += 2;
        }
        else if (strcmp(argv[count], "-magnet") == 0) {
            if (count + 1 >= argc) {
                printf("Missing URI for -magnet.\n");
                print_help(argv[0]);
                return 1;
            }
            option_output = OUTPUT_MAGNET;
            option_tracker = argv[count + 1];
            count += 1;
        }
        else if (argv[count][0] == '-' && argv[count][1] != '\0') {
            /* "-" alone means stdin, but anything else unrecognized is an error */
            if (strcmp(argv[count], "-") != 0) {
                printf("Unknown option \"%s\"\n", argv[count]);
                print_help(argv[0]);
                return 1;
            }
            /* if it’s just "-", handle below as normal file. */
            curr = (struct string_list *) malloc(sizeof(struct string_list));
            curr->str = argv[count];
            curr->next = NULL;
            if (!head) head = tail = curr;
            else {
                tail->next = curr;
                tail = curr;
            }
        } 
        else {
            /* Non-option argument, treat as file name */
            curr = (struct string_list *)malloc(sizeof(struct string_list));
            curr->str = argv[count];
            curr->next = NULL;
            if (head == NULL) {
                head = tail = curr;
            } else {
                tail->next = curr;
                tail = curr;
            }
        }
    }

    if (option_output == OUTPUT_MAGNET) {
        unsigned char infohash[20];
        char display_name[ERRBUF_SIZE];
        char **trackers = NULL;
        int tracker_count = 0;
        char errbuf[ERRBUF_SIZE];
    
        if (parse_magnet_uri(option_tracker, infohash, &trackers, &tracker_count, display_name, errbuf) != 0) {
            fprintf(stderr, "Magnet parse error: %s\n", errbuf);
            return 1;
        }
        
        if (display_name[0])
            printf("Name:           %s\n", display_name);
            printf("Magnet URI:     %s\n", option_tracker);
            printf("Info Hash:      ");
        for (int i = 0; i < 20; i++) printf("%02x", infohash[i]);
            printf("\n");

        if (tracker_count > 0) {
            printf("Announce List:\n");
            for (int i = 0; i < tracker_count; i++) {
                printf("                %s\n", trackers[i]);
            }
        }
        printf("\nScrapping test:\n");

        int total_seeders = 0, total_leechers = 0, total_completed = 0;
        int scrape_success = 0;
        int result[3];

        for (int i = 0; i < tracker_count; i++) {
            if (scrapec(trackers[i], infohash, result, errbuf) != 0) {
                printf("  %s\n", errbuf);
            } else {
                printf("                %s, (seeders=%d, completed=%d, leechers=%d)\n", trackers[i], result[0], result[1], result[2]);
                total_seeders += result[0];
                total_completed += result[1];
                total_leechers += result[2];
                scrape_success++;
            }
        }

        if (scrape_success > 1) {
            printf("\nTotal (from %d trackers): seeders=%d, completed=%d, leechers=%d\n",
                scrape_success, total_seeders, total_completed, total_leechers);
        }
    
        for (int i = 0; i < tracker_count; i++) free(trackers[i]);
        free(trackers);
        return 0;
    }

    if (option_output == OUTPUT_SCRAPEC) {
        /* If we’re scraping a single infohash from a tracker, we shouldn’t have any files. */
        if (head != NULL) {
            printf("Usage of -scrape is invalid with additional file arguments.\n");
            print_help(argv[0]);
            return 1;
        }
        return do_scrapec();
    }

    /* If no files given, show usage. */
    if (!head) {
        printf("No .torrent file specified.\n");
        print_help(argv[0]);
        return 1;
    }

    /* Process each file */
    int test_fail_count = 0;
    for (curr = head; curr != NULL; /* advanced below */) {
        struct benc_entity *root;
        if (strcmp(curr->str, "-") == 0) {
            root = benc_parse_stream(stdin, errbuf);
        } else if (is_magnet_uri(curr->str)) {
            unsigned char infohash[20];
            char *display_name = malloc(ERRBUF_SIZE);
            char **trackers = NULL;
            int tracker_count = 0;
            int result[3];
            int total_seeders = 0, total_leechers = 0, total_completed = 0;
            int scrape_success = 0;
        
            if (parse_magnet_uri(curr->str, infohash, &trackers, &tracker_count, display_name, errbuf) != 0) {
                printf("%s: %s\n", curr->str, errbuf);
                test_fail_count++;
                free(display_name);
                curr = curr->next;
                continue;
            }
        
            if (tracker_count == 0) {
                printf("%s: no tracker found in magnet URI\n", curr->str);
                free(display_name);
                curr = curr->next;
                continue;
            }
        
            printf("%s:\n", curr->str);
            if (display_name[0])
                printf("%s:\n", curr->str);
                printf("Name:           %s\n", display_name);
                printf("Magnet URI:     %s\n", option_tracker);
                printf("Info Hash:      ");
            for (int i = 0; i < 20; i++) printf("%02x", infohash[i]);
            printf("\n");
        
            if (tracker_count > 0) {
                printf("Announce List:\n");
                for (int i = 0; i < tracker_count; i++) {
                    printf("                %s\n", trackers[i]);
                }
            }
            printf("\nScrapping test:\n");
        
            for (int i = 0; i < tracker_count; i++) {
                if (scrapec(trackers[i], infohash, result, errbuf) != 0) {
                    printf("  %s\n", errbuf);
                } else {
                    printf("                %s, (seeders=%d, completed=%d, leechers=%d)\n", trackers[i], result[0], result[1], result[2]);
                    total_seeders += result[0];
                    total_completed += result[1];
                    total_leechers += result[2];
                    scrape_success++;
                }
            }
        
            if (scrape_success > 1) {
                printf("Total (from %d trackers): seeders=%d, completed=%d, leechers=%d\n",
                    scrape_success, total_seeders, total_completed, total_leechers);
            }
        
            for (int i = 0; i < tracker_count; i++) free(trackers[i]);
            free(trackers);
            free(display_name);
            curr = curr->next;
            continue;
        } else {
            root = benc_parse_file(curr->str, errbuf);
        }
        switch (option_output) {
            case OUTPUT_TEST:
                if (!root) {
                    printf("%s: %s\n", curr->str, errbuf);
                    test_fail_count++;
                } else if (check_torrent(root, errbuf)) {
                    printf("%s: %s\n", curr->str, errbuf);
                    test_fail_count++;
                }
                break;

            case OUTPUT_FIELD:
                if (!root) {
                    /* print empty line on error */
                    printf("\n");
                } else {
                    print_field(root);
                }
                break;

            case OUTPUT_BRIEF:
                if (!root) {
                    printf("%s: %s\n", curr->str, errbuf);
                } else {
                    printf("%s: ", curr->str);
                    show_torrent_info(root);
                }
                break;

            case OUTPUT_DEFAULT:
            case OUTPUT_FULL:
                printf("%s:\n", curr->str);
                if (!root) {
                    printf("%s\n", errbuf);
                } else {
                    show_torrent_info(root);
                    printf("\n");
                }
                break;

            case OUTPUT_DUMP:
                printf("%s:\n", curr->str);
                if (root)
                    benc_dump_entity(root);
                else
                    printf("%s\n", errbuf);
                printf("\n");
                break;

            case OUTPUT_SCRAPE:
                printf("%s:\n", curr->str);
                if (!root) {
                    printf("%s\n", errbuf);
                } else {
                    scrape_torrent(root);
                    printf("\n");
                }
                break;

            default:
                assert(0); /* Should never happen */
        }

        if (root)
            benc_free_entity(root);

        /* free current item and move on */
        temp = curr->next;
        free(curr);
        curr = temp;
    }

    return test_fail_count;
}
