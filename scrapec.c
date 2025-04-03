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

extern int option_timeout;

struct url_struct {
	int protocol; /* 1:HTTP; 2:UDP */
	char host[64]; /* RFC says 255, but ... */
	int port;
	char path[64]; /* again... */
};

static int parse_url (struct url_struct *url_struct, const char *url, char *errbuf)
{
	char *ptr, *ptr2;

	/* assign url_struct->protocol */
	if (strncmp(url, "http://", 7) == 0) {
		url_struct->protocol = 1;
		ptr = (char *)url + 7;
	} else if (strncmp(url, "udp://", 6) == 0) {
		url_struct->protocol = 2;
		ptr = (char *)url + 6;
	} else {
		snprintf(errbuf, ERRBUF_SIZE, "don't understand the protocol. %s", url);
		errbuf[ERRBUF_SIZE - 1] = '\0';
		return 1;
	}

	/* copy host:port to url_struct->host
	 * fill url_struct->path */
	ptr2 = strchr(ptr, '/');
	if (ptr2 == NULL) {
		if (strlen(ptr) >= 64) {
			snprintf(errbuf, ERRBUF_SIZE, "hostname is too long.");
			return 1;
		}
		strcpy(url_struct->host, ptr);
		strcpy(url_struct->path, "/");
	} else {
		if (strlen(ptr) >= 64) {
			snprintf(errbuf, ERRBUF_SIZE, "hostname is too long.");
			return 1;
		}
		memcpy(url_struct->host, ptr, ptr2 - ptr);
		url_struct->host[ptr2 - ptr] = '\0';
		if (strlen(ptr2) >= 64) {
			snprintf(errbuf, ERRBUF_SIZE, "path is too long.");
			return 1;
		}
		strcpy(url_struct->path, ptr2);
	}
	if (strchr(url_struct->host, '@')) {
		snprintf(errbuf, ERRBUF_SIZE, "authentication not supported.");
		return 1;
	}

	/* truncate url_struct->host to only host
	 * assign url_struct->port */
	ptr = strchr(url_struct->host, ':');
	if (ptr == NULL) {
		if (url_struct->protocol == 2) {
			snprintf(errbuf, ERRBUF_SIZE, "udp doesn't have a default port.");
			return 1;
		}
		url_struct->port = 80;
	} else {
		url_struct->port = atoi(ptr + 1);
		if (url_struct->port <= 0) {
			snprintf(errbuf, ERRBUF_SIZE, "invalid port");
			return 1;
		}
		*ptr = '\0';
	}

	/* for HTTP, change announce to scrape if needed.
	 * it's an error if it's neither announce nor scrape. */
	if (url_struct->protocol == 1) {
		ptr = strrchr(url_struct->path, '/');
		if (memcmp(ptr, "/scrape", 7) == 0) {
		} else if (memcmp(ptr, "/announce", 9) == 0) {
			int len = strlen(ptr);
			memcpy(ptr, "/scrape", 7);
			memmove(ptr + 7, ptr + 9, len - 8);
		} else {
			snprintf(errbuf, ERRBUF_SIZE, "neither announce nor scrape is found.");
			return 1;
		}
	}

	//printf("prot=%d, host=\"%s\", port=%d, path=\"%s\"\n",
	//		url_struct->protocol, url_struct->host, url_struct->port, url_struct->path);
	return 0;
}

static int parse_http_response (const char *buffer, int buffer_length, int *result, char *errbuf)
{
	const char *ptr;
	struct benc_entity *root, *entity;

	if (buffer_length < 90) // a correct response should be at least that big
		goto errout1;
	if (strncmp(buffer, "HTTP/1.0 200", 12) != 0 && strncmp(buffer, "HTTP/1.1 200", 12) != 0)
		goto errout1;

	ptr = strstr(buffer, "\r\n\r\n");
	if (ptr == NULL) {
		ptr = strstr(buffer, "\n\n"); // some crapy server does not follow CRLF.
		if (ptr == NULL)
			goto errout1;
		ptr -= 2;
	}
	ptr += 4;

	root = benc_parse_memory(ptr, buffer + buffer_length - ptr, NULL, errbuf);
	if (root == NULL) {
		// we just append the http data at the end of errbuf.
		// TODO: encapsulated exception would be perfect here.
		//       this requires change the errbuf interface.
		snprintf(errbuf + strlen(errbuf), ERRBUF_SIZE - strlen(errbuf),
				"\nhttp data: %s", ptr);
		errbuf[ERRBUF_SIZE - 1] = '\0';
		return 1;
	}

	entity = benc_lookup_string(root, "files");
	if (entity == NULL || entity->dictionary.head == NULL)
		goto errout2;
	entity = entity->dictionary.head->next;
	if (benc_lookup_string(entity, "complete") == NULL ||
			benc_lookup_string(entity, "downloaded") == NULL ||
			benc_lookup_string(entity, "incomplete") == NULL)
		goto errout2;
	result[0] = benc_lookup_string(entity, "complete")->integer;
	result[1] = benc_lookup_string(entity, "downloaded")->integer;
	result[2] = benc_lookup_string(entity, "incomplete")->integer;

	benc_free_entity(root);
	return 0;

errout2:
	snprintf(errbuf, ERRBUF_SIZE, "error in scrape data. %s", ptr);
	errbuf[ERRBUF_SIZE - 1] = '\0';
	benc_free_entity(root);
	return 1;
errout1:
	snprintf(errbuf, ERRBUF_SIZE, "error in http response: %s", buffer);
	errbuf[ERRBUF_SIZE - 1] = '\0';
	return 1;
}

static int scrapec_http (const char *host, int port, const char *path, const unsigned char *info_hash, int *result, char *errbuf)
{
	char buffer[4096];
	int buffer_length;
	struct hostent *hostent;
	int sock;
	struct sockaddr_in addr;

	buffer_length = snprintf(buffer, sizeof(buffer),
			"GET %s?info_hash="
			"%%%02X%%%02X%%%02X%%%02X%%%02X%%%02X%%%02X%%%02X%%%02X%%%02X"
			"%%%02X%%%02X%%%02X%%%02X%%%02X%%%02X%%%02X%%%02X%%%02X%%%02X"
			" HTTP/1.0\r\n"
			"Accept: */*\r\n"
			"Connection: close\r\n"
			"User-Agent: dumptorrent\r\n"
			"Host: %s:%d\r\n\r\n",
			path,
			info_hash[0], info_hash[1], info_hash[2], info_hash[3],
			info_hash[4], info_hash[5], info_hash[6], info_hash[7],
			info_hash[8], info_hash[9], info_hash[10], info_hash[11],
			info_hash[12], info_hash[13], info_hash[14], info_hash[15],
			info_hash[16], info_hash[17], info_hash[18], info_hash[19],
			host, port);

	hostent = gethostbyname(host);
	if (hostent == NULL || hostent->h_length != 4 || hostent->h_addr_list[0] == NULL) {
		snprintf(errbuf, ERRBUF_SIZE, "cannot resolve hostname.");
		return 1;
	}
	memcpy(&addr.sin_addr, hostent->h_addr_list[0], 4);
	addr.sin_port = htons(port);
	addr.sin_family = AF_INET;

	sock = socket(PF_INET, SOCK_STREAM, 0);
	if (sock == -1) {
		snprintf(errbuf, ERRBUF_SIZE, "socket() error");
		return 1;
	}

	if (option_timeout != 0) {
#ifdef _WIN32
		int timeout = option_timeout * 1000;
#else
		struct timeval timeout = {option_timeout, 0};
#endif
		if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) == -1 ||
				setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) == -1) {
			snprintf(errbuf, ERRBUF_SIZE, "setsockopt(SO_RCVTIMEO/SO_SNDTIMEO) error");
			CLOSESOCKET(sock);
			return 1;
		}
	}

	if (connect(sock, (struct sockaddr *)&addr, sizeof(struct sockaddr_in)) != 0) {
		snprintf(errbuf, ERRBUF_SIZE, "connect() error");
		CLOSESOCKET(sock);
		return 1;
	}

	if (send(sock, buffer, buffer_length, 0) != buffer_length) {
		snprintf(errbuf, ERRBUF_SIZE, "send() error");
		CLOSESOCKET(sock);
		return 1;
	}

	buffer_length = 0;
	for (;;) {
		int len = recv(sock, buffer + buffer_length, sizeof(buffer) - buffer_length, 0);
		if (len < 0) {
			snprintf(errbuf, ERRBUF_SIZE, "recv() error");
			CLOSESOCKET(sock);
			return 1;
		}
		if (len == 0)
			break;
		buffer_length += len;
		if (buffer_length >= sizeof(buffer)) {
			snprintf(errbuf, ERRBUF_SIZE, "buffer too small.");
			CLOSESOCKET(sock);
			return 1;
		}
	}
	CLOSESOCKET(sock);

	buffer[buffer_length] = '\0';
	return parse_http_response(buffer, buffer_length, result, errbuf);
}

struct udp_connect_input {
	unsigned char connection_id[8];
	unsigned int action;
	unsigned int transaction_id;
};

struct udp_connect_output {
	unsigned int action;
	unsigned int transaction_id;
	unsigned char connection_id[8];
};

struct udp_scrape_input {
	unsigned char connection_id[8];
	unsigned int action;
	unsigned int transaction_id;
	unsigned char info_hash[20];
};

struct udp_scrape_output {
	unsigned int action;
	unsigned int transaction_id;
	unsigned int seeders;
	unsigned int completed;
	unsigned int leechers;
};

static int scrapec_udp (const char *host, int port, const unsigned char *info_hash, int *result, char *errbuf)
{
	struct hostent *hostent;
	int sock;
	struct sockaddr_in addr;
	union {
		struct udp_connect_input ci;
		struct udp_connect_output co;
		struct udp_scrape_input si;
		struct udp_scrape_output so;
	} buffer;
	unsigned int r;
	unsigned char connection_id[8];

	hostent = gethostbyname(host);
	if (hostent == NULL || hostent->h_length != 4) {
		snprintf(errbuf, ERRBUF_SIZE, "cannot resolve hostname.");
		return 1;
	}
	memcpy(&addr.sin_addr, hostent->h_addr_list[0], 4);
	addr.sin_port = htons(port);
	addr.sin_family = AF_INET;

	sock = socket(PF_INET, SOCK_DGRAM, 0);
	if (sock == -1) {
		snprintf(errbuf, ERRBUF_SIZE, "socket() error");
		return 1;
	}

	if (option_timeout != 0) {
#ifdef _WIN32
		int timeout = option_timeout * 1000;
#else
		struct timeval timeout = {option_timeout, 0};
#endif
		if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) == -1 ||
				setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) == -1) {
			snprintf(errbuf, ERRBUF_SIZE, "setsockopt(SO_RCVTIMEO/SO_SNDTIMEO) error");
			CLOSESOCKET(sock);
			return 1;
		}
	}

	if (connect(sock, (struct sockaddr *)&addr, sizeof(struct sockaddr_in)) != 0) {
		snprintf(errbuf, ERRBUF_SIZE, "connect() error");
		CLOSESOCKET(sock);
		return 1;
	}

	r = (unsigned int)rand() * (unsigned int)rand();
	memcpy(buffer.ci.connection_id, "\x00\x00\x04\x17\x27\x10\x19\x80", 8);
	buffer.ci.action = htonl(0);
	buffer.ci.transaction_id = r;
	if (send(sock, &buffer, sizeof(struct udp_connect_input), 0) != sizeof(struct udp_connect_input)) {
		snprintf(errbuf, ERRBUF_SIZE, "send() error");
		CLOSESOCKET(sock);
		return 1;
	}

	if (recv(sock, &buffer, sizeof(buffer), 0) < (int)sizeof(struct udp_connect_output)) {
		snprintf(errbuf, ERRBUF_SIZE, "recv() error");
		perror(NULL);
		CLOSESOCKET(sock);
		return 1;
	}
	if (buffer.co.transaction_id != r || buffer.co.action != htonl(0)) {
		snprintf(errbuf, ERRBUF_SIZE, "recv_1() error: transaction_id=%x, r=%x, action=%d",
				ntohl(buffer.co.transaction_id), ntohl(r), ntohl(buffer.co.action));
		CLOSESOCKET(sock);
		return 1;
	}
	memcpy(connection_id, buffer.co.connection_id, 8);

	r = (unsigned int)rand() * (unsigned int)rand();
	memcpy(buffer.si.connection_id, connection_id, 8);
	buffer.si.action = htonl(2);
	buffer.si.transaction_id = r;
	memcpy(buffer.si.info_hash, info_hash, 20);
	if (send(sock, &buffer, sizeof(struct udp_scrape_input), 0) != sizeof(struct udp_scrape_input)) {
		snprintf(errbuf, ERRBUF_SIZE, "send_2() error");
		CLOSESOCKET(sock);
		return 1;
	}

	if (recv(sock, &buffer, sizeof(buffer), 0) < (int)sizeof(struct udp_scrape_output)) {
		snprintf(errbuf, ERRBUF_SIZE, "recv() error");
		CLOSESOCKET(sock);
		return 1;
	}
	if (buffer.so.transaction_id != r || buffer.so.action != htonl(2)) {
		snprintf(errbuf, ERRBUF_SIZE, "recv_2() error: transaction_id=%x, r=%x, action=%d",
				ntohl(buffer.so.transaction_id), ntohl(r), ntohl(buffer.so.action));
		CLOSESOCKET(sock);
		return 1;
	}
	result[0] = ntohl(buffer.so.seeders);
	result[1] = ntohl(buffer.so.completed);
	result[2] = ntohl(buffer.so.leechers);

	CLOSESOCKET(sock);
	return 0;
}

int scrapec (const char *url, const unsigned char *info_hash, int *result, char *errbuf)
{
	struct url_struct url_struct;

#ifdef _WIN32
	static int wsa_initiallized = 0;
	if (!wsa_initiallized) {
		WSADATA wsa_data;

		if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
			snprintf(errbuf, ERRBUF_SIZE, "WSAStartup(2.2) error");
			return 1;
		}
		wsa_initiallized = 1;
	}
#endif

	if (parse_url(&url_struct, url, errbuf))
		return 1;

	return url_struct.protocol == 1 ?
		scrapec_http(url_struct.host, url_struct.port, url_struct.path, info_hash, result, errbuf) :
		scrapec_udp(url_struct.host, url_struct.port, info_hash, result, errbuf);
}

#ifdef BUILD_MAIN

int option_timeout = 0;

static void print_usage (const char *arg0)
{
	printf("%s: scrape_url info_hash", arg0);
}

static int parse_info_hash (const char *str, unsigned char *info_hash)
{
	int i;
	for (i = 0; i < 20; i ++) {
		int c;
		if (str[i*2] >= '0' && str[i*2] <= '9')
			c = (str[i*2] - '0') << 4;
		else if (str[i*2] >= 'a' && str[i*2] <= 'f')
			c = (str[i*2] - 'a' + 10) << 4;
		else if (str[i*2] >= 'A' && str[i*2] <= 'F')
			c = (str[i*2] - 'A' + 10) << 4;
		else
			return 1;

		if (str[i*2+1] >= '0' && str[i*2+1] <= '9')
			c |= str[i*2+1] - '0';
		else if (str[i*2+1] >= 'a' && str[i*2+1] <= 'f')
			c |= str[i*2+1] - 'a' + 10;
		else if (str[i*2+1] >= 'A' && str[i*2+1] <= 'F')
			c |= str[i*2+1] - 'A' + 10;
		else
			return 1;

		info_hash[i] = c;
	}

	return 0;
}

int main (int argc, char *argv[])
{
	unsigned char info_hash[20];
	int result[3];
	char errbuf[ERRBUF_SIZE];

	if (argc != 3) {
		print_usage(argv[0]);
		return 1;
	}

	srand(time(NULL));

	if (parse_info_hash(argv[2], info_hash)) {
		fprintf(stderr, "invalid info_hash value. make sure it's 40 hexadecimal characters.\n");
		return 1;
	}

	if (scrapec(argv[1], info_hash, result, errbuf) != 0) {
		fputs(errbuf, stderr);
		fputc('\n', stderr);
		return 1;
	}

	printf("seeders=%d, completed=%d, leechers=%d\n", result[0], result[1], result[2]);

	return 0;
}
#endif
