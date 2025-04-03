#ifndef BENC_H
#define BENC_H

#define BENC_STRING     1
#define BENC_INTEGER    2
#define BENC_LIST       3
#define BENC_DICTIONARY 4

struct benc_entity {
	int type;
	struct benc_entity *next;
	union {
		struct {
			int length;
			char *str;
		} string;
		long long int integer;
		struct {
			struct benc_entity *head;
			struct benc_entity *tail;
		} list;
		struct {
			struct benc_entity *head;
			struct benc_entity *tail;
		} dictionary;
	};
};

struct benc_entity *benc_new_string (int length, char *str);
struct benc_entity *benc_new_integer (long long int value);
struct benc_entity *benc_new_list (void);
void benc_append_list (struct benc_entity *list, struct benc_entity *entity);
struct benc_entity *benc_new_dictionary (void);
void benc_append_dictionary (struct benc_entity *dictionary, struct benc_entity *key, struct benc_entity *value);
struct benc_entity *benc_lookup_string (struct benc_entity *dictionary, const char *key);

void benc_free_entity (struct benc_entity *entity);

struct benc_entity *benc_parse_memory (const char *data, int length, int *peaten, char *errbuf);
struct benc_entity *benc_parse_stream (FILE *stream, char *errbuf);
struct benc_entity *benc_parse_file (const char *file_name, char *errbuf);
void benc_sha1_entity (struct benc_entity *entity, unsigned char *digest);
void benc_dump_entity (struct benc_entity *entity);

#endif
