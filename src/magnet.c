#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include "magnet.h"
#include "common.h"
#include <ctype.h>

/**
 * unsigned char infohash_out[20],
 *
 * @param uri The magnet URI to parse.
 * @param out_infohash A buffer to store the 20-byte infohash extracted from the URI.
 * @param trackers_out A pointer to an array of tracker URLs extracted from the URI.
 *                      The caller is responsible for freeing this array.
 * @param tracker_count_out A pointer to an integer to store the number of trackers found.
 * @param display_name_out A buffer to store the display name (dn=...) if present.
 * @param errbuf A buffer to store error messages in case of failure.
 *               Must be at least ERRBUF_SIZE bytes long.
 * @return 0 on success, or 1 if an error occurred (error message will be in errbuf).
 */
 char *urldecode(const char *src);

 int parse_magnet_uri(const char *uri, unsigned char out_infohash[20], char ***trackers_out, int *tracker_count_out, char *display_name_out, char *errbuf) {
    if (!uri) {
        snprintf(errbuf, ERRBUF_SIZE, "URI is NULL");
        return 1;
    }
    if (strncmp(uri, "magnet:?", 8) != 0) {
        snprintf(errbuf, ERRBUF_SIZE, "Not a magnet URI: %s", uri);
        return 1;
    }

    char *copy = strdup(uri + 8); // skip "magnet:?"
    if (!copy) {
        snprintf(errbuf, ERRBUF_SIZE, "Out of memory");
        return 1;
    }

    char *saveptr;
    char *param = strtok_r(copy, "&", &saveptr);

    int tracker_count = 0;
    char **trackers = NULL;
    display_name_out[0] = '\0'; // empty by default

    while (param) {
        char *eq = strchr(param, '=');
        if (!eq) {
            snprintf(errbuf, ERRBUF_SIZE, "Malformed param: %s", param);
            free(copy);
            return 1;
        }

        *eq = '\0';
        const char *key = param;
        const char *value = eq + 1;

        if (strcmp(key, "xt") == 0 && strncmp(value, "urn:btih:", 9) == 0) {
            const char *hash = value + 9;
            if (strlen(hash) == 40) {
                for (int i = 0; i < 20; i++) {
                    sscanf(hash + 2 * i, "%2hhx", &out_infohash[i]);
                }
            } else {
                snprintf(errbuf, ERRBUF_SIZE, "Invalid infohash length");
                free(copy);
                return 1;
            }
        } else if (strcmp(key, "tr") == 0) {
            char *decoded = urldecode(value);
            trackers = realloc(trackers, sizeof(char *) * (tracker_count + 1));
            trackers[tracker_count++] = decoded;           
        }  else if (strcmp(key, "dn") == 0) {
            char *decoded = urldecode(value);
            strncpy(display_name_out, decoded, ERRBUF_SIZE - 1);
            display_name_out[ERRBUF_SIZE - 1] = '\0';
            free(decoded);
        }

        param = strtok_r(NULL, "&", &saveptr);
    }

    free(copy);
    *trackers_out = trackers;
    *tracker_count_out = tracker_count;

    return 0;
}

char *urldecode(const char *src) {
    char *out = malloc(strlen(src) + 1);
    char *dst = out;

    while (*src) {
        if (*src == '%' && isxdigit((unsigned char)src[1]) && isxdigit((unsigned char)src[2])) {
            int high = src[1] > '9' ? tolower(src[1]) - 'a' + 10 : src[1] - '0';
            int low  = src[2] > '9' ? tolower(src[2]) - 'a' + 10 : src[2] - '0';
            *dst++ = (char)((high << 4) | low);
            src += 3;
        } else if (*src == '+') {
            *dst++ = ' ';
            src++;
        } else {
            *dst++ = *src++;
        }
    }

    *dst = '\0';
    return out;
}
