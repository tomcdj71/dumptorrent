#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <stdio.h>

#include "benc.h"

static int option_dump = 0;

static char *human_readable_number (long long int n)
{
	static char buff[32];
	char *ptr;

#ifdef _WIN32
	ptr = buff + sprintf(buff, "%I64d", n);
#else
	ptr = buff + sprintf(buff, "%lld", n);
#endif

	if (n < 1000) {
	} else if (n < 1024 * 1000) {
		sprintf(ptr, " (%.3gK)", n / 1024.0);
	} else if (n < 1024 * 1024 * 1000) {
		sprintf(ptr, " (%.3gM)", n / (1024.0 * 1024.0));
	} else {
		sprintf(ptr, " (%.3gG)", n / (1024.0 * 1024.0 * 1024.0));
	}
	return buff;
}

static void show_torrent_info (struct benc_entity *root)
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
		struct benc_entity *files;
		struct benc_list *fileslist;

		files = benc_lookup_string(info, "files");
		if (files == NULL) {
			printf("can't find neither \"length\" nor \"files\" entry in \"info\".\n");
			return;
		}
		total_length = 0;
		max_filename_length = 0;
		for (fileslist = files->list.head; fileslist != NULL; fileslist = fileslist->next) {
			struct benc_entity *path;
			struct benc_list *pathlist;
			int filename_length;

			length = benc_lookup_string(fileslist->entity, "length");
			if (length == NULL) {
				printf("can't find \"length\" entry in \"files\" list entry.\n");
				return;
			}
			path = benc_lookup_string(fileslist->entity, "path");
			if (path == NULL) {
				printf("can't find \"path\" entry in \"files\" list entry.\n");
				return;
			}

			total_length += length->integer;
			filename_length = -1;
			for (pathlist = path->list.head; pathlist != NULL; pathlist = pathlist->next) {
				filename_length += pathlist->entity->string.length + 1;
			}
			if (max_filename_length < filename_length)
				max_filename_length = filename_length;
		}
	}

	printf("Name:           %s\n", name->string.str);
	printf("Size:           %s\n", human_readable_number(total_length));

	benc_sha1_entity(info, info_hash);
	printf("Info Hash:      %02x%02x%02x%02x%02x%02x%02x%02x%02x%02x"
			"%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x\n",
			info_hash[0], info_hash[1], info_hash[2], info_hash[3],
			info_hash[4], info_hash[5], info_hash[6], info_hash[7],
			info_hash[8], info_hash[9], info_hash[10], info_hash[11],
			info_hash[12], info_hash[13], info_hash[14], info_hash[15],
			info_hash[16], info_hash[17], info_hash[18], info_hash[19]);

	printf("Piece Length:   %s\n", human_readable_number(piece_length->integer));
	printf("Announce:       %s\n", announce->string.str);
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
	if (benc_lookup_string(root, "created by")) {
		printf("Created By:     %s\n", benc_lookup_string(root, "created by")->string.str);
	}
	if (benc_lookup_string(root, "encoding")) {
		printf("Encoding:       %s\n", benc_lookup_string(root, "encoding")->string.str);
	}
	if (benc_lookup_string(root, "announce-list")) {
		struct benc_list *tierlist;
		printf("Announce List:\n");
		for (tierlist = benc_lookup_string(root, "announce-list")->list.head; tierlist != NULL; tierlist = tierlist->next) {
			struct benc_list *backuplist;
			printf("                ");
			for (backuplist = tierlist->entity->list.head; backuplist != NULL; backuplist = backuplist->next) {
				printf("%s", backuplist->entity->string.str);
				if (backuplist->next != NULL)
					printf(", ");
			}
			printf("\n");
		}
	}
	printf("Files:\n");
	if (benc_lookup_string(info, "length") != NULL) {
		printf("                %s %s\n", name->string.str, human_readable_number(total_length));
	} else {
		struct benc_list *fileslist;
		for (fileslist = benc_lookup_string(info, "files")->list.head; fileslist != NULL; fileslist = fileslist->next) {
			struct benc_list *pathlist;
			int filename_length = -1;
			printf("                ");
			for (pathlist = benc_lookup_string(fileslist->entity, "path")->list.head; pathlist != NULL; pathlist = pathlist->next) {
				filename_length += pathlist->entity->string.length + 1;
				printf("%s", pathlist->entity->string.str);
				if (pathlist->next != NULL)
					printf("/");
			}
			while (filename_length ++ <= max_filename_length)
				printf(" ");
			puts(human_readable_number(benc_lookup_string(fileslist->entity, "length")->integer));
		}
	}
}

static void dump_torrent (const char *file_name)
{
	struct benc_entity *root;

	root = benc_parse_file(file_name);
	if (root == NULL) {
		printf("torrent file parse error.\n");
		return;
	}

	printf("%s:\n", file_name);

	if (option_dump) {
		benc_dump_entity(root);
	} else {
		show_torrent_info(root);
	}

	benc_free_entity(root);
}

static void print_help (const char *prog)
{
	printf("Dump Torrent Information:\n"
			"Usage: %s [-d] files.torrent ...\n"
			"Options:\n"
			"  -d: raw hierarchical dump\n"
			, prog);
}

int main (int argc, char *argv[])
{
	int count;
	int printed = 0;

	for (count = 1; count < argc; count ++) {
		if (strcmp(argv[count], "-h") == 0) {
			print_help(argv[0]);
			return 0;
		} else if (strcmp(argv[count], "-d") == 0) {
			option_dump = 1;
		} else {
			if (printed)
				printf("\n");
			printed = 1;
			dump_torrent(argv[count]);
		}
	}

	return 0;
}
