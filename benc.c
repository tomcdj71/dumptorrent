#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>

#ifdef _WIN32
	#define snprintf _snprintf
#endif

#include "common.h"
#include "sha1.h"
#include "benc.h"

struct benc_entity *benc_new_string (int length, char *str)
{
	struct benc_entity *retval = (struct benc_entity *)malloc(sizeof(struct benc_entity));
	retval->type = BENC_STRING;
	retval->next = NULL;
	retval->string.length = length;
	retval->string.str = str;
	return retval;
}

struct benc_entity *benc_new_integer (long long int value)
{
	struct benc_entity *retval = (struct benc_entity *)malloc(sizeof(struct benc_entity));
	retval->type = BENC_INTEGER;
	retval->next = NULL;
	retval->integer = value;
	return retval;
}

struct benc_entity *benc_new_list (void)
{
	struct benc_entity *retval = (struct benc_entity *)malloc(sizeof(struct benc_entity));
	retval->type = BENC_LIST;
	retval->next = NULL;
	retval->list.head = NULL;
	return retval;
}

void benc_append_list (struct benc_entity *list, struct benc_entity *entity)
{
	assert(list != NULL && entity != NULL && list->type == BENC_LIST);

	entity->next = NULL;

	if (list->list.head == NULL)
		list->list.head = list->list.tail = entity;
	else
		list->list.tail = list->list.tail->next = entity;
}

struct benc_entity *benc_new_dictionary (void)
{
	struct benc_entity *retval = (struct benc_entity *)malloc(sizeof(struct benc_entity));
	retval->type = BENC_DICTIONARY;
	retval->next = NULL;
	retval->dictionary.head = NULL;
	return retval;
}

void benc_append_dictionary (struct benc_entity *dictionary, struct benc_entity *key, struct benc_entity *value)
{
	assert(dictionary != NULL && key != NULL && value != NULL && dictionary->type == BENC_DICTIONARY);

	key->next = value;
	value->next = NULL;

	if (dictionary->dictionary.head == NULL) {
		dictionary->dictionary.head = key;
		dictionary->dictionary.tail = value;
	} else {
		dictionary->dictionary.tail->next = key;
		dictionary->dictionary.tail = value;
	}
}

struct benc_entity *benc_lookup_string (struct benc_entity *dictionary, const char *key)
{
	struct benc_entity *curr;
	int length;

	assert(dictionary != NULL && key != NULL && dictionary->type == BENC_DICTIONARY);

	length = strlen(key);
	for (curr = dictionary->dictionary.head; curr != NULL && curr->next != NULL; curr = curr->next->next) {
		if (curr->type == BENC_STRING && length == curr->string.length && strncmp(key, curr->string.str, length) == 0)
			return curr->next;
	}
	return NULL;
}

void benc_free_entity (struct benc_entity *entity)
{
	assert(entity != NULL);

	if (entity->next)
		benc_free_entity(entity->next);

	switch (entity->type) {
	case BENC_STRING:
		free(entity->string.str);
		break;
	case BENC_INTEGER:
		break;
	case BENC_LIST:
		if (entity->list.head != NULL)
			benc_free_entity(entity->list.head);
		break;
	case BENC_DICTIONARY:
		if (entity->dictionary.head != NULL)
			benc_free_entity(entity->dictionary.head);
		break;
	default:
		assert(0);
	}

	free(entity);
}

static int parse_lldecimal_memory (const char *str, long long int *presult)
{
	long long int result = 0;
	int neg = 0;
	const char *ptr = str;

	if (*ptr == '-') {
		neg = 1;
		ptr ++;
	}

	if (*ptr < '0' || *ptr > '9')
		return 0;

	while (*ptr >= '0' && *ptr <= '9') {
		result *= 10;
		result += *ptr - '0';
		ptr ++;
	}

	*presult = neg ? -result : result;
	return ptr - str;
}

struct benc_entity *benc_parse_memory (const char *data, int length, int *peaten, char *errbuf)
{
	struct benc_entity *entity;

	if (length < 2) {
		snprintf(errbuf, ERRBUF_SIZE, "parse error: length (%d) too small.", length);
		return NULL;
	}

	switch (*data) {
	case 'i':
		{
			long long int value;
			int eaten;

			eaten = parse_lldecimal_memory(data + 1, &value);
			if (eaten == 0 || eaten + 2 > length || data[eaten + 1] != 'e') {
				snprintf(errbuf, ERRBUF_SIZE, "parse error: expecting 'e' for an integer");
				return NULL;
			}
			if (peaten != NULL)
				*peaten = eaten + 2;
			entity = benc_new_integer(value);
		}
		break;
	case 'l':
		{
			const char *ptr = data + 1;

			entity = benc_new_list();
			for (;;) {
				struct benc_entity *child_entity;
				int eaten;

				if (ptr - data >= length) {
					snprintf(errbuf, ERRBUF_SIZE, "parse error: expecting 'e' for list.");
					benc_free_entity(entity);
					return NULL;
				}
				if (*ptr == 'e')
					break;
				child_entity = benc_parse_memory(ptr, length - (int)ptr + (int)data, &eaten, errbuf);
				if (child_entity == NULL) {
					benc_free_entity(entity);
					return NULL;
				}
				ptr += eaten;
				benc_append_list(entity, child_entity);
			}
			if (peaten != NULL)
				*peaten = ptr + 1 - data;
		}
		break;
	case 'd':
		{
			const char *ptr = data + 1;

			entity = benc_new_dictionary();
			for (;;) {
				struct benc_entity *key, *value;
				int eaten;

				if (ptr - data >= length) {
					snprintf(errbuf, ERRBUF_SIZE, "parse error: expecting 'e' for dictionary.");
					benc_free_entity(entity);
					return NULL;
				}
				if (*ptr == 'e')
					break;
				key = benc_parse_memory(ptr, length - (int)ptr + (int)data, &eaten, errbuf);
				if (key == NULL) {
					benc_free_entity(entity);
					return NULL;
				}
				ptr += eaten;
				value = benc_parse_memory(ptr, length - (int)ptr + (int)data, &eaten, errbuf);
				if (value == NULL) {
					benc_free_entity(key);
					benc_free_entity(entity);
					return NULL;
				}
				ptr += eaten;
				benc_append_dictionary(entity, key, value);
			}
			if (peaten != NULL)
				*peaten = ptr + 1 - data;
		}
		break;
	default:
		{
			long long int str_length;
			int eaten;
			char *str;

			if (*data < '0' || *data > '9') {
				snprintf(errbuf, ERRBUF_SIZE, "unrecognized prefix %c", *data);
				return NULL;
			}
			eaten = parse_lldecimal_memory(data, &str_length);
			if (eaten == 0 || data[eaten] != ':') {
				snprintf(errbuf, ERRBUF_SIZE, "expecting :, but get %c", *data);
				return NULL;
			}

			if (str_length < 0 || eaten + 1 + str_length > length) {
				snprintf(errbuf, ERRBUF_SIZE, "string too long.");
				return NULL;
			}
			str = (char *)malloc((int)str_length + 1);
			memcpy(str, data + eaten + 1, (int)str_length);
			str[str_length] = '\0';

			if (peaten != NULL)
				*peaten = eaten + 1 + (int)str_length;
			entity = benc_new_string((int)str_length, str);
		}
		break;
	}

	return entity;
}

static long long int parse_lldecimal_stream (FILE *stream)
{
	long long int result = 0;
	int neg = 0;
	char c;

	c = fgetc(stream);
	if (c == '-') {
		neg = 1;
		c = fgetc(stream);
	}

	while (c >= '0' && c <= '9') {
		result *= 10;
		result += c - '0';
		c = fgetc(stream);
	}

	if (c != EOF)
		ungetc(c, stream);

	return neg ? -result : result;
}

struct benc_entity *benc_parse_stream (FILE *stream, char *errbuf)
{
	int c;
	struct benc_entity *entity;

	//printf("parse ...\n");

	c = getc(stream);
	switch (c) {
	case 'i':
		{
			long long int value = parse_lldecimal_stream(stream);
			c = getc(stream);
			if (c != 'e') {
				snprintf(errbuf, ERRBUF_SIZE, "parse error: expecting 'e' for an integer");
				return NULL;
			}
			entity = benc_new_integer(value);
		}
		break;
	case 'l':
		{
			entity = benc_new_list();
			for (;;) {
				struct benc_entity *child_entity;

				c = getc(stream);
				if (c == 'e')
					break;
				ungetc(c, stream);
				child_entity = benc_parse_stream(stream, errbuf);
				if (child_entity == NULL) {
					benc_free_entity(entity);
					return NULL;
				}
				benc_append_list(entity, child_entity);
			}
		}
		break;
	case 'd':
		{
			entity = benc_new_dictionary();
			for (;;) {
				struct benc_entity *key, *value;

				c = getc(stream);
				if (c == 'e')
					break;
				ungetc(c, stream);
				key = benc_parse_stream(stream, errbuf);
				if (key == NULL) {
					benc_free_entity(entity);
					return NULL;
				}
				value = benc_parse_stream(stream, errbuf);
				if (value == NULL) {
					benc_free_entity(key);
					benc_free_entity(entity);
					return NULL;
				}
				benc_append_dictionary(entity, key, value);
			}
		}
		break;
	case EOF:
		snprintf(errbuf, ERRBUF_SIZE, "unexpected EOF");
		entity = NULL;
		break;
	default:
		{
			int length;
			char *str;

			if (c < '0' || c > '9') {
				snprintf(errbuf, ERRBUF_SIZE, "unrecognized prefix %c", c);
				return NULL;
			}
			ungetc(c, stream);
			fscanf(stream, "%d", &length);
			c = getc(stream);
			if (c != ':') {
				snprintf(errbuf, ERRBUF_SIZE, "expecting :, but get %c", c);
				return NULL;
			}

			if (length > 1024 * 1024 || length < 0) {
				snprintf(errbuf, ERRBUF_SIZE, "string too long.");
				return NULL;
			}
			str = (char *)malloc(length + 1);
			if (length != 0) {
				if (fread(str, length, 1, stream) != 1) {
					snprintf(errbuf, ERRBUF_SIZE, "cannot read string of length %d", length);
					free(str);
					return NULL;
				}
			}
			str[length] = '\0';
			entity = benc_new_string(length, str);
		}
	}

	//printf("parse return %d\n", entity->type);

	return entity;
}

struct benc_entity *benc_parse_file (const char *file_name, char *errbuf)
{
	FILE *fp;
	struct benc_entity *entity;

	fp = fopen(file_name, "rb");
	if (fp == NULL) {
		snprintf(errbuf, ERRBUF_SIZE, "can't open file %s", file_name);
		errbuf[ERRBUF_SIZE - 1] = '\0';
		return NULL;
	}

	entity = benc_parse_stream(fp, errbuf);

	fclose(fp);
	return entity;
}

static void benc_sha1_entity_rec (struct benc_entity *entity, SHA_CTX *ctx)
{
	switch (entity->type) {
	case BENC_STRING:
		{
			char size[16];
			int len_size = sprintf(size, "%d:", entity->string.length);
			SHAUpdate(ctx, (unsigned char *)size, len_size);
			SHAUpdate(ctx, (unsigned char *)entity->string.str, entity->string.length);
		}
		break;
	case BENC_INTEGER:
		{
			char size[16];
#ifdef _WIN32
			int len_size = sprintf(size, "i%I64de", entity->integer);
#else
			int len_size = sprintf(size, "i%llde", entity->integer);
#endif
			SHAUpdate(ctx, (unsigned char *)size, len_size);
		}
		break;
	case BENC_LIST:
		{
			struct benc_entity *curr;

			SHAUpdate(ctx, (unsigned char *)"l", 1);
			for (curr = entity->list.head; curr != NULL; curr = curr->next)
				benc_sha1_entity_rec(curr, ctx);
			SHAUpdate(ctx, (unsigned char *)"e", 1);
		}
		break;
	case BENC_DICTIONARY:
		{
			struct benc_entity *curr;

			SHAUpdate(ctx, (unsigned char *)"d", 1);
			for (curr = entity->dictionary.head; curr != NULL; curr = curr->next)
				benc_sha1_entity_rec(curr, ctx);
			SHAUpdate(ctx, (unsigned char *)"e", 1);
		}
	}
}

void benc_sha1_entity (struct benc_entity *entity, unsigned char *digest)
{
	SHA_CTX ctx;

	SHAInit(&ctx);
	benc_sha1_entity_rec(entity, &ctx);
	SHAFinal(digest, &ctx);
}

static int is_ascii (const char *str, int length)
{
	while (--length >= 0) {
		if (str[length] <= 0)
			return 0;
	}
	return 1;
}

void benc_dump_entity (struct benc_entity *entity)
{
	static int depth = 0;
	int i;

	assert(entity != NULL);

	for (i = 0; i < depth; i ++)
		printf(" ");

	switch (entity->type) {
	case BENC_STRING:
		if (is_ascii(entity->string.str, entity->string.length)) {
			puts(entity->string.str);
		} else {
			printf("<string of length %d>\n", entity->string.length);
		}
		break;
	case BENC_INTEGER:
#ifdef _WIN32
		printf("%I64d\n", entity->integer);
#else
		printf("%lld\n", entity->integer);
#endif
		break;
	case BENC_LIST:
		printf("<list>\n");
		depth += 4;
		{
			struct benc_entity *curr;
			for (curr = entity->list.head; curr != NULL; curr = curr->next)
				benc_dump_entity(curr);
		}
		depth -= 4;
		break;
	case BENC_DICTIONARY:
		printf("<dictionary>\n");
		depth += 4;
		{
			struct benc_entity *curr;
			for (curr = entity->dictionary.head; curr != NULL; curr = curr->next)
				benc_dump_entity(curr);
		}
		depth -= 4;
		break;
	default:
		printf("benc_dump_entity(unknown)\n");
		*(int *)0 = 0;
	}
}
