#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "sha1.h"
#include "benc.h"

struct benc_entity *benc_new_string (int length, char *str)
{
	struct benc_entity *retval = (struct benc_entity *)malloc(sizeof(struct benc_entity));
	retval->type = BENC_STRING;
	retval->string.length = length;
	retval->string.str = str;
	return retval;
}

struct benc_entity *benc_new_integer (long long int value)
{
	struct benc_entity *retval = (struct benc_entity *)malloc(sizeof(struct benc_entity));
	retval->type = BENC_INTEGER;
	retval->integer = value;
	return retval;
}

struct benc_entity *benc_new_list (void)
{
	struct benc_entity *retval = (struct benc_entity *)malloc(sizeof(struct benc_entity));
	retval->type = BENC_LIST;
	retval->list.head = NULL;
	return retval;
}

void benc_append_list (struct benc_entity *list, struct benc_entity *entity)
{
	struct benc_list *new_node;

	if (list == NULL || entity == NULL || list->type != BENC_LIST) {
		printf("benc_append_list(): not a list. aborted\n");
		return;
	}

	new_node = (struct benc_list *)malloc(sizeof(struct benc_list));
	new_node->next = NULL;
	new_node->entity = entity;

	if (list->list.head == NULL) {
		list->list.head = list->list.tail = new_node;
	} else {
		list->list.tail->next = new_node;
		list->list.tail = new_node;
	}
}

struct benc_entity *benc_new_dictionary (void)
{
	struct benc_entity *retval = (struct benc_entity *)malloc(sizeof(struct benc_entity));
	retval->type = BENC_DICTIONARY;
	retval->dictionary.head = NULL;
	return retval;
}

void benc_append_dictionary (struct benc_entity *dictionary, struct benc_entity *key, struct benc_entity *value)
{
	struct benc_dictionary *new_node;

	if (dictionary == NULL || key == NULL || key == NULL || dictionary->type != BENC_DICTIONARY) {
		printf("benc_append_dictionary(): not a dictionary. aborted\n");
		return;
	}

	new_node = (struct benc_dictionary *)malloc(sizeof(struct benc_dictionary));
	new_node->next = NULL;
	new_node->key = key;
	new_node->value = value;

	if (dictionary->dictionary.head == NULL) {
		dictionary->dictionary.head = dictionary->dictionary.tail = new_node;
	} else {
		dictionary->dictionary.tail->next = new_node;
		dictionary->dictionary.tail = new_node;
	}
}

struct benc_entity *benc_lookup_string (struct benc_entity *dictionary, const char *key)
{
	struct benc_dictionary *curr;
	int length;

	if (dictionary == NULL || key == NULL || dictionary->type != BENC_DICTIONARY) {
		printf("benc_append_dictionary(): not a dictionary. ignored\n");
		return NULL;
	}

	length = strlen(key);
	for (curr = dictionary->dictionary.head; curr != NULL; curr = curr->next) {
		if (curr->key->type == BENC_STRING && length == curr->key->string.length && strncmp(key, curr->key->string.str, length) == 0)
			return curr->value;
	}
	return NULL;
}

void benc_free_entity (struct benc_entity *entity)
{
	switch (entity->type) {
	case BENC_STRING:
		free(entity->string.str);
		break;
	case BENC_INTEGER:
		break;
	case BENC_LIST:
		{
			struct benc_list *curr;
			for (curr = entity->list.head; curr != NULL;) {
				struct benc_list *tmp = curr;
				benc_free_entity(curr->entity);
				curr = curr->next;
				free(tmp);
			}
		}
		break;
	case BENC_DICTIONARY:
		{
			struct benc_dictionary *curr;
			for (curr = entity->dictionary.head; curr != NULL;) {
				struct benc_dictionary *tmp = curr;
				benc_free_entity(curr->key);
				benc_free_entity(curr->value);
				curr = curr->next;
				free(tmp);
			}
		}
		break;
	default:
		printf("freeing unknown unknown entity type %d\n", entity->type);
	}

	free(entity);
}

static int parse_lldecimal (const char *str, long long int *presult)
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

	*presult = result;
	return ptr - str;
}

struct benc_entity *benc_parse_memory (const char *data, int length, int *peaten)
{
	struct benc_entity *entity;

	if (length < 2) {
		printf("parse error: length (%d) too small.\n", length);
		return NULL;
	}

	switch (*data) {
	case 'i':
		{
			long long int value;
			int eaten;

			eaten = parse_lldecimal(data + 1, &value);
			if (eaten == 0 || eaten + 2 > length || data[eaten + 1] != 'e') {
				printf("parse error: expecting 'e' for an integer\n");
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
					printf("parse error: expecting 'e' for list.\n");
					benc_free_entity(entity);
					return NULL;
				}
				if (*ptr == 'e')
					break;
				child_entity = benc_parse_memory(ptr, length - (int)ptr + (int)data, &eaten);
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
					printf("parse error: expecting 'e' for dictionary.\n");
					benc_free_entity(entity);
					return NULL;
				}
				if (*ptr == 'e')
					break;
				key = benc_parse_memory(ptr, length - (int)ptr + (int)data, &eaten);
				if (key == NULL) {
					benc_free_entity(entity);
					return NULL;
				}
				ptr += eaten;
				value = benc_parse_memory(ptr, length - (int)ptr + (int)data, &eaten);
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
				printf("unrecognized prefix %c\n", *data);
				return NULL;
			}
			eaten = parse_lldecimal(data, &str_length);
			if (eaten == 0 || data[eaten] != ':') {
				printf("expecting :, but get %c\n", *data);
				return NULL;
			}

			if (str_length < 0 || eaten + 1 + str_length > length) {
				printf("string too long.\n");
				return NULL;
			}
			str = (char *)malloc(str_length + 1);
			memcpy(str, data + eaten + 1, str_length);
			str[str_length] = '\0';

			if (peaten != NULL)
				*peaten = eaten + 1 + str_length;
			entity = benc_new_string(str_length, str);
		}
		break;
	}

	return entity;
}

struct benc_entity *benc_parse_stream (FILE *stream)
{
	int c;
	struct benc_entity *entity;

	//printf("parse ...\n");

	c = getc(stream);
	switch (c) {
	case 'i':
		{
			long long int value;
#ifdef _WIN32
			fscanf(stream, "%I64d", &value);
#else
			fscanf(stream, "%Ld", &value);
#endif
			c = getc(stream);
			if (c != 'e') {
				printf("parse error: expecting 'e' for an integer\n");
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
				child_entity = benc_parse_stream(stream);
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
				key = benc_parse_stream(stream);
				if (key == NULL) {
					benc_free_entity(entity);
					return NULL;
				}
				value = benc_parse_stream(stream);
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
		printf("unexpected EOF\n");
		entity = NULL;
		break;
	default:
		{
			int length;
			char *str;

			if (c < '0' || c > '9') {
				printf("unrecognized prefix %c\n", c);
				return NULL;
			}
			ungetc(c, stream);
			fscanf(stream, "%d", &length);
			c = getc(stream);
			if (c != ':') {
				printf("expecting :, but get %c\n", c);
				return NULL;
			}

			if (length > 1024 * 1024 || length < 0) {
				printf("string too long.\n");
				return NULL;
			}
			str = (char *)malloc(length + 1);
			if (length != 0) {
				if (fread(str, length, 1, stream) != 1) {
					printf("cannot read string of length %d\n", length);
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

struct benc_entity *benc_parse_file (const char *file_name)
{
	FILE *fp;
	struct benc_entity *entity;

	fp = fopen(file_name, "rb");
	if (fp == NULL) {
		perror(NULL);
		return NULL;
	}

	entity = benc_parse_stream(fp);

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
			struct benc_list *curr;

			SHAUpdate(ctx, (unsigned char *)"l", 1);
			for (curr = entity->list.head; curr != NULL; curr = curr->next)
				benc_sha1_entity_rec(curr->entity, ctx);
			SHAUpdate(ctx, (unsigned char *)"e", 1);
		}
		break;
	case BENC_DICTIONARY:
		{
			struct benc_dictionary *curr;

			SHAUpdate(ctx, (unsigned char *)"d", 1);
			for (curr = entity->dictionary.head; curr != NULL; curr = curr->next) {
				benc_sha1_entity_rec(curr->key, ctx);
				benc_sha1_entity_rec(curr->value, ctx);
			}
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
			struct benc_list *curr;
			for (curr = entity->list.head; curr != NULL; curr = curr->next)
				benc_dump_entity(curr->entity);
		}
		depth -= 4;
		break;
	case BENC_DICTIONARY:
		printf("<dictionary>\n");
		depth += 4;
		{
			struct benc_dictionary *curr;
			for (curr = entity->dictionary.head; curr != NULL; curr = curr->next) {
				benc_dump_entity(curr->key);
				benc_dump_entity(curr->value);
			}
		}
		depth -= 4;
		break;
	default:
		printf("unknown\n");
	}
}
