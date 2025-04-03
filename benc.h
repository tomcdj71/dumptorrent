#ifndef BENC_H
#define BENC_H

#define BENC_STRING     1
#define BENC_INTEGER    2
#define BENC_LIST       3
#define BENC_DICTIONARY 4

struct benc_list {
	struct benc_list *next;
	struct benc_entity *entity;
};

struct benc_dictionary {
	struct benc_dictionary *next;
	struct benc_entity *key;
	struct benc_entity *value;
};

struct benc_entity {
	int type;
	union {
		struct {
			int length;
			char *str;
		} string;
		long long int integer;
		struct {
			struct benc_list *head;
			struct benc_list *tail;
		} list;
		struct {
			struct benc_dictionary *head;
			struct benc_dictionary *tail;
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
struct benc_entity *benc_parse_stream (FILE *stream);
struct benc_entity *benc_parse_file (const char *file_name);
void benc_free_entity (struct benc_entity *entity);
void benc_sha1_entity (struct benc_entity *entity, unsigned char *digest);
void benc_dump_entity (struct benc_entity *entity);

#endif
