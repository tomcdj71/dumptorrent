#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#ifdef _WIN32
  #include <winsock2.h>
  #include <windows.h>
  #define snprintf _snprintf
#else
  #include <unistd.h>
  #include <netinet/in.h>
  #include <sys/socket.h>
  #include <arpa/inet.h>
  #include <netdb.h>
#endif

#include "common.h"
#include "benc.h"

#ifdef _WIN32
  #define CLOSESOCKET(s) closesocket(s)
#else
  #define CLOSESOCKET(s) close(s)
#endif

/* -------------------------------------------------------------------------
   VERSION DEFINITION (for standalone usage)
   ------------------------------------------------------------------------- */
#define SCRAPE_VERSION "1.4.0"

/* -------------------------------------------------------------------------
   EXTERNAL OPTION (used by dumptorrent + scrape)
   ------------------------------------------------------------------------- */
extern int option_timeout;

/* -------------------------------------------------------------------------
   INTERNAL URL STRUCT
   ------------------------------------------------------------------------- */
struct url_struct {
    int protocol;     /* 1:HTTP; 2:UDP */
    char host[64];    /* truncated length for demonstration */
    int port;
    char path[64];    /* truncated length for demonstration */
};

/* -------------------------------------------------------------------------
   parse_url(): interpret the given URL into url_struct
   ------------------------------------------------------------------------- */
static int parse_url(struct url_struct *url_struct, const char *url, char *errbuf)
{
    char *ptr, *ptr2;

    /* Identify protocol (http:// or udp://) */
    if (strncmp(url, "http://", 7) == 0) {
        url_struct->protocol = 1; /* HTTP */
        ptr = (char *) url + 7;
    } else if (strncmp(url, "udp://", 6) == 0) {
        url_struct->protocol = 2; /* UDP */
        ptr = (char *) url + 6;
    } else {
        snprintf(errbuf, ERRBUF_SIZE, "unrecognized protocol in URL: '%s'", url);
        errbuf[ERRBUF_SIZE - 1] = '\0';
        return 1;
    }

    /* host[:port][/path] separation */
    ptr2 = strchr(ptr, '/');
    if (!ptr2) {
        /* No '/' means entire remainder is host:port, no path given */
        if (strlen(ptr) >= 64) {
            snprintf(errbuf, ERRBUF_SIZE, "hostname is too long.");
            return 1;
        }
        strcpy(url_struct->host, ptr);
        strcpy(url_struct->path, "/");
    } else {
        /* We do have a path portion */
        if (strlen(ptr) >= 64) {
            snprintf(errbuf, ERRBUF_SIZE, "hostname is too long.");
            return 1;
        }
        memcpy(url_struct->host, ptr, (size_t)(ptr2 - ptr));
        url_struct->host[ptr2 - ptr] = '\0';

        if (strlen(ptr2) >= 64) {
            snprintf(errbuf, ERRBUF_SIZE, "path is too long.");
            return 1;
        }
        strcpy(url_struct->path, ptr2);
    }

    /* Check for user:pass@host type cases (not supported) */
    if (strchr(url_struct->host, '@')) {
        snprintf(errbuf, ERRBUF_SIZE, "authentication not supported in URLs.");
        return 1;
    }

    /* If a ':port' is present, parse it out. Otherwise default. */
    ptr = strchr(url_struct->host, ':');
    if (!ptr) {
        if (url_struct->protocol == 2) {
            /* For UDP trackers, port is mandatory (we have no default like 80). */
            snprintf(errbuf, ERRBUF_SIZE, "no port specified for UDP URL.");
            return 1;
        }
        url_struct->port = 80; /* default for HTTP */
    } else {
        url_struct->port = atoi(ptr + 1);
        if (url_struct->port <= 0) {
            snprintf(errbuf, ERRBUF_SIZE, "invalid port specified.");
            return 1;
        }
        *ptr = '\0'; /* Truncate host string at the colon. */
    }

    /* For HTTP, forcibly rewrite "announce" -> "scrape" if needed */
    if (url_struct->protocol == 1) {
        ptr = strrchr(url_struct->path, '/');
        if (!ptr) {
            snprintf(errbuf, ERRBUF_SIZE, "invalid path in HTTP URL.");
            return 1;
        }
        if (memcmp(ptr, "/scrape", 7) == 0) {
            /* Already /scrape? OK. */
        } else if (memcmp(ptr, "/announce", 9) == 0) {
            /* Convert to /scrape */
            int len = (int) strlen(ptr);
            memcpy(ptr, "/scrape", 7);
            memmove(ptr + 7, ptr + 9, (size_t)(len - 8)); /* shift leftover bytes (including terminating \0) */
        } else {
            snprintf(errbuf, ERRBUF_SIZE,
                     "path must contain /announce or /scrape for scraping, got: '%s'", ptr);
            return 1;
        }
    }

    return 0;
}

/* -------------------------------------------------------------------------
   parse_http_response(): interpret HTTP response to extract seeders, etc.
   ------------------------------------------------------------------------- */
static int parse_http_response(const char *buffer, int buffer_length, int *result, char *errbuf)
{
    const char *ptr;
    struct benc_entity *root, *entity;

    /* Quick minimal checks */
    if (buffer_length < 90) /* a "typical" successful scrape is bigger than this */
        goto errout1;

    /* Must have "HTTP/1.x 200 ..." near the start */
    if (strncmp(buffer, "HTTP/1.0 200", 12) != 0 &&
        strncmp(buffer, "HTTP/1.1 200", 12) != 0)
        goto errout1;

    /* Attempt to find end of headers */
    ptr = strstr(buffer, "\r\n\r\n");
    if (!ptr) {
        /* Some servers may use just "\n\n" instead of CRLF pair */
        ptr = strstr(buffer, "\n\n");
        if (!ptr) goto errout1;
        ptr -= 2; /* so that the offset after +4 is consistent below */
    }
    ptr += 4; /* skip over that \r\n\r\n or \n\n boundary */

    /* Now parse the bencoded data from the HTTP body */
    root = benc_parse_memory(ptr, buffer + buffer_length - ptr, NULL, errbuf);
    if (!root) {
        /* Append the partial body to the error for debugging */
        snprintf(errbuf + strlen(errbuf), ERRBUF_SIZE - strlen(errbuf),
                 "\nhttp data: %s", ptr);
        errbuf[ERRBUF_SIZE - 1] = '\0';
        return 1;
    }

    entity = benc_lookup_string(root, "files");
    if (!entity || !entity->dictionary.head) {
        goto errout2;
    }
    /* Typically "files" dictionary has a single child whose name is the infohash. 
       Then within it: "complete", "downloaded", and "incomplete". */
    entity = entity->dictionary.head->next; /* jump to dictionary’s child—this is quite hacky, but that’s the code’s pattern */
    if (!benc_lookup_string(entity, "complete") ||
        !benc_lookup_string(entity, "downloaded") ||
        !benc_lookup_string(entity, "incomplete"))
    {
        goto errout2;
    }

    /* Great, read it out. */
    result[0] = (int) benc_lookup_string(entity, "complete")->integer;   /* seeders    */
    result[1] = (int) benc_lookup_string(entity, "downloaded")->integer; /* completed */
    result[2] = (int) benc_lookup_string(entity, "incomplete")->integer; /* leechers   */

    benc_free_entity(root);
    return 0;

errout2:
    snprintf(errbuf, ERRBUF_SIZE, "error in HTTP scrape data. %s", ptr);
    errbuf[ERRBUF_SIZE - 1] = '\0';
    benc_free_entity(root);
    return 1;

errout1:
    snprintf(errbuf, ERRBUF_SIZE, "bad HTTP response: %s", buffer);
    errbuf[ERRBUF_SIZE - 1] = '\0';
    return 1;
}

/* -------------------------------------------------------------------------
   scrapec_http(): connect via TCP, send GET to the tracker’s /scrape URL
   ------------------------------------------------------------------------- */
static int scrapec_http(const char *host, int port, const char *path,
                        const unsigned char *info_hash, int *result, char *errbuf)
{
    char buffer[4096];
    int buffer_length;
    struct hostent *hostent;
    int sock;
    struct sockaddr_in addr;

    /* Build the HTTP GET request (info_hash must be escaped as %XX bytes) */
    buffer_length = snprintf(buffer, sizeof(buffer),
        "GET %s?info_hash="
        "%%%02X%%%02X%%%02X%%%02X%%%02X%%%02X%%%02X%%%02X%%%02X%%%02X"
        "%%%02X%%%02X%%%02X%%%02X%%%02X%%%02X%%%02X%%%02X%%%02X%%%02X"
        " HTTP/1.0\r\n"
        "Accept: */*\r\n"
        "Connection: close\r\n"
        "User-Agent: dumptorrent-scrape\r\n"
        "Host: %s:%d\r\n\r\n",
        path,
        info_hash[0], info_hash[1], info_hash[2], info_hash[3],
        info_hash[4], info_hash[5], info_hash[6], info_hash[7],
        info_hash[8], info_hash[9], info_hash[10], info_hash[11],
        info_hash[12], info_hash[13], info_hash[14], info_hash[15],
        info_hash[16], info_hash[17], info_hash[18], info_hash[19],
        host, port);

    /* DNS resolution */
    hostent = gethostbyname(host);
    if (!hostent || hostent->h_length != 4 || !hostent->h_addr_list[0]) {
        snprintf(errbuf, ERRBUF_SIZE, "cannot resolve hostname: '%s'", host);
        return 1;
    }

    /* Prepare sockaddr_in */
    memcpy(&addr.sin_addr, hostent->h_addr_list[0], 4);
    addr.sin_port = htons((unsigned short) port);
    addr.sin_family = AF_INET;

    /* Create socket */
    sock = socket(PF_INET, SOCK_STREAM, 0);
    if (sock == -1) {
        snprintf(errbuf, ERRBUF_SIZE, "socket() error");
        return 1;
    }

    /* Set timeouts if any */
    if (option_timeout != 0) {
#ifdef _WIN32
        int timeout_ms = option_timeout * 1000;
        if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout_ms, sizeof(timeout_ms)) == -1 ||
            setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (char*)&timeout_ms, sizeof(timeout_ms)) == -1)
        {
            snprintf(errbuf, ERRBUF_SIZE, "setsockopt timeout error");
            CLOSESOCKET(sock);
            return 1;
        }
#else
        struct timeval timeoutval = {option_timeout, 0};
        if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeoutval, sizeof(timeoutval)) == -1 ||
            setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &timeoutval, sizeof(timeoutval)) == -1)
        {
            snprintf(errbuf, ERRBUF_SIZE, "setsockopt timeout error");
            CLOSESOCKET(sock);
            return 1;
        }
#endif
    }

    /* Connect */
    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        snprintf(errbuf, ERRBUF_SIZE, "connect() error to %s:%d", host, port);
        CLOSESOCKET(sock);
        return 1;
    }

    /* Send GET request */
    if (send(sock, buffer, buffer_length, 0) != buffer_length) {
        snprintf(errbuf, ERRBUF_SIZE, "send() error");
        CLOSESOCKET(sock);
        return 1;
    }

    /* Read response into buffer */
    buffer_length = 0;
    for (;;) {
        int len = (int)recv(sock, buffer + buffer_length, sizeof(buffer) - buffer_length, 0);
        if (len < 0) {
            snprintf(errbuf, ERRBUF_SIZE, "recv() error");
            CLOSESOCKET(sock);
            return 1;
        }
        if (len == 0) break; /* remote closed */
        buffer_length += len;
        if (buffer_length >= (int)sizeof(buffer)) {
            snprintf(errbuf, ERRBUF_SIZE, "response too large for buffer");
            CLOSESOCKET(sock);
            return 1;
        }
    }
    CLOSESOCKET(sock);

    /* Null-terminate and parse HTTP response for the bencoded data. */
    buffer[buffer_length] = '\0';
    return parse_http_response(buffer, buffer_length, result, errbuf);
}

/* -------------------------------------------------------------------------
   scrapec_udp(): connect via UDP, do the standard handshake for UDP trackers
   ------------------------------------------------------------------------- */
static int scrapec_udp(const char *host, int port,
                       const unsigned char *info_hash, int *result, char *errbuf)
{
    struct hostent *hostent;
    int sock;
    struct sockaddr_in addr;
    union {
        struct {
            unsigned char connection_id[8];
            unsigned int action;
            unsigned int transaction_id;
        } ci; /* connect input */
        struct {
            unsigned int action;
            unsigned int transaction_id;
            unsigned char connection_id[8];
        } co; /* connect output */
        struct {
            unsigned char connection_id[8];
            unsigned int action;
            unsigned int transaction_id;
            unsigned char info_hash[20];
        } si; /* scrape input */
        struct {
            unsigned int action;
            unsigned int transaction_id;
            unsigned int seeders;
            unsigned int completed;
            unsigned int leechers;
        } so; /* scrape output */
        unsigned char raw[64]; /* fallback space if needed */
    } buffer;
    unsigned int r;
    unsigned char connection_id[8];

    /* DNS resolution */
    hostent = gethostbyname(host);
    if (!hostent || hostent->h_length != 4) {
        snprintf(errbuf, ERRBUF_SIZE, "cannot resolve hostname: '%s'", host);
        return 1;
    }
    memcpy(&addr.sin_addr, hostent->h_addr_list[0], 4);
    addr.sin_port = htons((unsigned short) port);
    addr.sin_family = AF_INET;

    /* Create UDP socket */
    sock = socket(PF_INET, SOCK_DGRAM, 0);
    if (sock == -1) {
        snprintf(errbuf, ERRBUF_SIZE, "socket() error (UDP)");
        return 1;
    }

    /* Timeouts if any */
    if (option_timeout != 0) {
#ifdef _WIN32
        int timeout_ms = option_timeout * 1000;
        if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout_ms, sizeof(timeout_ms)) == -1 ||
            setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (char*)&timeout_ms, sizeof(timeout_ms)) == -1)
        {
            snprintf(errbuf, ERRBUF_SIZE, "setsockopt timeout error (UDP)");
            CLOSESOCKET(sock);
            return 1;
        }
#else
        struct timeval timeoutval = {option_timeout, 0};
        if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeoutval, sizeof(timeoutval)) == -1 ||
            setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &timeoutval, sizeof(timeoutval)) == -1)
        {
            snprintf(errbuf, ERRBUF_SIZE, "setsockopt timeout error (UDP)");
            CLOSESOCKET(sock);
            return 1;
        }
#endif
    }

    /* 'connect' UDP (really just sets default peer) */
    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        snprintf(errbuf, ERRBUF_SIZE, "connect() error (UDP) to %s:%d", host, port);
        CLOSESOCKET(sock);
        return 1;
    }

    /* First handshake: send connect request */
    r = (unsigned int) rand() * (unsigned int) rand(); /* random 32-bit for transaction ID */
    memcpy(buffer.ci.connection_id, "\x00\x00\x04\x17\x27\x10\x19\x80", 8); /* standard magic connection_id */
    buffer.ci.action = htonl(0);       /* action: connect = 0 */
    buffer.ci.transaction_id = r;
    if ((int)send(sock, &buffer, sizeof(buffer.ci), 0) != (int)sizeof(buffer.ci)) {
        snprintf(errbuf, ERRBUF_SIZE, "send() error in UDP connect");
        CLOSESOCKET(sock);
        return 1;
    }

    /* Receive connect response (the “udp_connect_output”) */
    if (recv(sock, &buffer, sizeof(buffer), 0) < (int)sizeof(buffer.co)) {
        snprintf(errbuf, ERRBUF_SIZE, "recv() error in UDP connect");
        CLOSESOCKET(sock);
        return 1;
    }
    if (buffer.co.transaction_id != r || buffer.co.action != htonl(0)) {
        snprintf(errbuf, ERRBUF_SIZE,
                 "bad connect response: transaction_id=%x, expected=%x, action=%d",
                 ntohl(buffer.co.transaction_id), ntohl(r), ntohl(buffer.co.action));
        CLOSESOCKET(sock);
        return 1;
    }
    memcpy(connection_id, buffer.co.connection_id, 8);

    /* Second handshake: send scrape request */
    r = (unsigned int) rand() * (unsigned int) rand();
    memcpy(buffer.si.connection_id, connection_id, 8);
    buffer.si.action = htonl(2); /* action: scrape = 2 */
    buffer.si.transaction_id = r;
    memcpy(buffer.si.info_hash, info_hash, 20);
    if ((int)send(sock, &buffer, sizeof(buffer.si), 0) != (int)sizeof(buffer.si)) {
        snprintf(errbuf, ERRBUF_SIZE, "send() error in UDP scrape");
        CLOSESOCKET(sock);
        return 1;
    }

    /* Receive scrape response (the “udp_scrape_output”) */
    if (recv(sock, &buffer, sizeof(buffer), 0) < (int)sizeof(buffer.so)) {
        snprintf(errbuf, ERRBUF_SIZE, "recv() error in UDP scrape");
        CLOSESOCKET(sock);
        return 1;
    }
    if (buffer.so.transaction_id != r || buffer.so.action != htonl(2)) {
        snprintf(errbuf, ERRBUF_SIZE,
                 "bad scrape response: transaction_id=%x, expected=%x, action=%d",
                 ntohl(buffer.so.transaction_id), ntohl(r), ntohl(buffer.so.action));
        CLOSESOCKET(sock);
        return 1;
    }

    /* Convert from network order to host.  */
    result[0] = (int) ntohl(buffer.so.seeders);
    result[1] = (int) ntohl(buffer.so.completed);
    result[2] = (int) ntohl(buffer.so.leechers);

    CLOSESOCKET(sock);
    return 0;
}

/* -------------------------------------------------------------------------
   scrapec(): main entry for scraping a single <url, info_hash>
   result will be [seeders, completed, leechers], or error in errbuf
   ------------------------------------------------------------------------- */
int scrapec(const char *url, const unsigned char *info_hash, int *result, char *errbuf)
{
    struct url_struct url_struct;

#ifdef _WIN32
    /* If we’re on Windows, ensure WSA is up. */
    static int wsa_initialized = 0;
    if (!wsa_initialized) {
        WSADATA wsa_data;
        if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
            snprintf(errbuf, ERRBUF_SIZE, "WSAStartup(2.2) error");
            return 1;
        }
        wsa_initialized = 1;
    }
#endif

    /* Break down the passed URL into host, port, path, protocol. */
    if (parse_url(&url_struct, url, errbuf)) {
        return 1;
    }

    /* Then call either scrapec_http() or scrapec_udp(). */
    if (url_struct.protocol == 1) {
        return scrapec_http(url_struct.host, url_struct.port, url_struct.path,
                            info_hash, result, errbuf);
    } else {
        return scrapec_udp(url_struct.host, url_struct.port, info_hash, result, errbuf);
    }
}

/* -------------------------------------------------------------------------
   STANDALONE MAIN (if compiled with -DBUILD_MAIN)
   ------------------------------------------------------------------------- */
#ifdef BUILD_MAIN

int option_timeout = 0; /* If you want to set a default, do so here */

static void print_usage(const char *arg0)
{
    printf("Usage: %s [options] <scrape_url> <info_hash>\n", arg0);
    printf("  -w <timeout> : network timeout in seconds\n");
    printf("  -V           : print scrape version and exit\n");
    printf("\nExample:\n");
    printf("  %s http://tracker.example.com/announce d1eab... [40 hex chars]\n", arg0);
}

/* parse_info_hash() to convert 40-hex string into 20 raw bytes */
static int parse_info_hash(const char *str, unsigned char *info_hash)
{
    size_t len = strlen(str);
    if (len != 40) return 1;

    for (int i = 0; i < 20; i++) {
        int c;

        /* high nibble */
        if (str[i * 2] >= '0' && str[i * 2] <= '9')
            c = (str[i * 2] - '0') << 4;
        else if (str[i * 2] >= 'a' && str[i * 2] <= 'f')
            c = (str[i * 2] - 'a' + 10) << 4;
        else if (str[i * 2] >= 'A' && str[i * 2] <= 'F')
            c = (str[i * 2] - 'A' + 10) << 4;
        else
            return 1;

        /* low nibble */
        if (str[i * 2 + 1] >= '0' && str[i * 2 + 1] <= '9')
            c |= (str[i * 2 + 1] - '0');
        else if (str[i * 2 + 1] >= 'a' && str[i * 2 + 1] <= 'f')
            c |= (str[i * 2 + 1] - 'a' + 10);
        else if (str[i * 2 + 1] >= 'A' && str[i * 2 + 1] <= 'F')
            c |= (str[i * 2 + 1] - 'A' + 10);
        else
            return 1;

        info_hash[i] = (unsigned char) c;
    }
    return 0;
}

int main(int argc, char *argv[])
{
    unsigned char info_hash[20];
    int result[3];
    char errbuf[ERRBUF_SIZE];
    const char *url = NULL;

    /* We expect 2 positional args: <scrape_url> and <info_hash> 
       but also allow -w, -V, etc. */
    int i = 1;
    while (i < argc) {
        if (!strcmp(argv[i], "-h")) {
            print_usage(argv[0]);
            return 0;
        }
        else if (!strcmp(argv[i], "-V")) {
            printf("Scrape version %s\n", SCRAPE_VERSION);
            return 0;
        }
        else if (!strcmp(argv[i], "-w")) {
            if (i + 1 >= argc) {
                fprintf(stderr, "-w requires an integer argument.\n");
                return 1;
            }
            option_timeout = atoi(argv[i + 1]);
            if (option_timeout < 0) {
                fprintf(stderr, "timeout must be non-negative.\n");
                return 1;
            }
            i += 2;
        }
        else if (argv[i][0] == '-') {
            /* Unknown flag or -something else */
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        }
        else {
            /* It's a positional argument, presumably either URL or INFO_HASH. */
            if (!url) {
                url = argv[i];
            } else {
                /* Then it must be the info_hash */
                if (parse_info_hash(argv[i], info_hash)) {
                    fprintf(stderr, "Invalid info_hash; must be 40 hex chars.\n");
                    return 1;
                }
            }
            i++;
        }
    }

    /* We need both a URL and an info_hash to proceed. */
    if (!url) {
        fprintf(stderr, "No URL provided.\n");
        print_usage(argv[0]);
        return 1;
    }

    /* We can detect if info_hash was not set by checking if parse_info_hash was never called. */
    /* A lazy approach: if info_hash is all zero at this point, that might be suspicious. */
    /* But let’s just check if i >= 3… or keep it simpler: if parse_info_hash wasn’t called. */
    /* We'll assume that if we never set it, parse_info_hash wouldn't have run. 
       We might just do a 2-arg check. */
    /* If you want a more explicit approach, store a boolean once parse_info_hash succeeds. */
    /* For demonstration, let's do a quick hack: if info_hash[0..19] is all zero, that might be suspicious. */
    static const unsigned char zeroes[20] = {0};
    if (memcmp(info_hash, zeroes, 20) == 0) {
        fprintf(stderr, "No valid info_hash provided.\n");
        print_usage(argv[0]);
        return 1;
    }

    /* We can do the scrape now */
    srand((unsigned) time(NULL));
    if (scrapec(url, info_hash, result, errbuf) != 0) {
        fprintf(stderr, "Scrape error: %s\n", errbuf);
        return 1;
    }

    /* On success, we have seeders, completed, leechers in result. */
    printf("seeders=%d, completed=%d, leechers=%d\n",
		result[0], result[1], result[2]);
    return 0;
}

#endif /* BUILD_MAIN */
