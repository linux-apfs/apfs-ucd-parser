/*
 * Copyright (C) 2018 Ernesto A. Fernández <ernesto.mnd.fernandez@gmail.com>
 *
 * A small part of the code was taken from a version of the mkutf8data script
 * by Olaf Weber (https://marc.info/?l=linux-fsdevel&m=152584896931191&w=2).
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include <assert.h>

#define LINESIZE	1024

char line[LINESIZE];
char buf0[LINESIZE];

int verbose = 0;

struct trie_node {
	unsigned int depth;		/* 5 for leaf nodes */
	unsigned int pos;		/* Position in the printed array */
	unsigned int *value;		/* NULL if not a leaf node */
	struct trie_node *parent;	/* NULL for the root node */
	unsigned int index;		/* Index among its siblings */
	struct trie_node *children[16];	/* One child for each possible nibble */
	unsigned int descendants;	/* Number of descendants */
};

static void trie_insert(struct trie_node *node, unsigned int unichar,
			unsigned int *value)
{
	struct trie_node *child;
	unsigned int shift;
	unsigned int branch;

	shift = (4 - node->depth) * 4;
	branch = (unichar >> shift) & 0xf;
	child = node->children[branch];

	if (!child) {
		child = calloc(1, sizeof(*child));
		if (!child)
			exit(1);
		node->children[branch] = child;
		child->depth = node->depth + 1;
		child->parent = node;
		child->index = branch;
		for (; node; node = node->parent)
			node->descendants++;
	}
	if (child->depth == 5) { /* Reached the leaf node */
		child->value = value;
		return;
	}

	trie_insert(child, unichar, value);
}

/* Return a description of the range covered by this node, e.g. 00001f__ */
void get_range(struct trie_node *node, char *desc)
{
	char tmp[2];
	int i;

	for (i = 4; i >= 0; --i) {
		if (i < node->depth) {
			sprintf(tmp, "%.1x", node->index);
			node = node->parent;
		} else {
			sprintf(tmp, "_");
		}
		*(desc + i) = tmp[0];
	}
}

/* Find the first child with index above @index */
static struct trie_node *first_child(struct trie_node *node, int index)
{
	int i;
	struct trie_node *child;

	for (i = index; i < 16; ++i) {
		child = node->children[i];
		if (child) {
			return child;
		}
	}
	return NULL;
}

/* Find the next trie node within this level, or NULL if this is the last one */
static struct trie_node *level_next(struct trie_node *node)
{
	struct trie_node *parent = node->parent;
	struct trie_node *next;

	if (!parent)
		return NULL;

	next = first_child(parent, node->index + 1);
	if (next)
		return next;

	parent = level_next(parent);
	if (parent)
		return first_child(parent, 0);

	return NULL;
}

/* Find the first trie node on a given level */
static struct trie_node *level_first(struct trie_node *root, int depth)
{
	struct trie_node *result = root;
	int i;

	for (i = 0; i < depth; ++i) {
		result = first_child(result, 0);
		if (!result)
			exit(1);
	}

	return result;
}

static int unilength(unsigned int *um)
{
	int length = 0;

	if (!um)
		return 0;

	for (; *um; um++)
		length++;

	return length;
}

static void trie_calculate_positions(struct trie_node *root, bool is_ccc)
{
	struct trie_node *n;
	unsigned int current;
	int i;

	/* The trie array gives the position of the children for each node */
	current = 0;
	for (i = 0; i < 5; ++i) {
		for (n = level_first(root, i); n; n = level_next(n)) {
			n->pos = current >> 4; /* Last bits are zero, ignore */
			current = current + 16;
		}
	}

	/* The value of the leaf nodes will be  stored in a separate array */
	current = 0;
	for (n = level_first(root, 5); n; n = level_next(n)) {
		int len;

		if (!n->value)
			continue;

		if (is_ccc) {
			/* no data array, the 'pos' is just the value */
			n->pos = n->value[0];
			continue;
		}

		len = unilength(n->value);

		/* Save both position and length of the value */
		assert(len < 8);
		assert(!(current & 0xe000));
		n->pos = (current << 3) + len;

		current += len;
	}
}

static void nfdi_init(struct trie_node *nfd_root)
{
	FILE *file;
	unsigned int unichar;
	unsigned int mapping[19]; /* Magic - guaranteed not to be exceeded. */
	char *s;
	unsigned int *um;
	int count;
	int i;
	int ret;

	if (verbose > 0)
		printf("Parsing UnicodeData.txt\n");
	file = fopen("ucd/UnicodeData.txt", "r");
	if (!file)
		exit(1);

	count = 0;
	while (fgets(line, LINESIZE, file)) {
		ret = sscanf(line, "%X;%*[^;];%*[^;];%*[^;];%*[^;];%[^;];",
			     &unichar, buf0);
		if (ret != 2)
			continue;

		s = buf0;
		/* canonical decompositions are the ones without a <tag> */
		if (*s == '<')
			continue;
		/* decode the decomposition into UTF-32 */
		i = 0;
		while (*s) {
			mapping[i] = strtoul(s, &s, 16);
			i++;
		}
		mapping[i++] = 0;

		um = malloc(i * sizeof(unsigned int));
		if (!um)
			exit(1);
		memcpy(um, mapping, i * sizeof(unsigned int));
		trie_insert(nfd_root, unichar, um);

		count++;
	}
	fclose(file);
	if (verbose > 0)
		printf("Found %d entries\n", count);
	if (count == 0)
		exit(1);
}

static void cf_init(struct trie_node *cf_root)
{
	FILE *file;
	unsigned int unichar;
	unsigned int mapping[19]; /* Magic - guaranteed not to be exceeded. */
	char status[2];
	char *s;
	unsigned int *um;
	int count;
	int i;
	int ret;

	if (verbose > 0)
		printf("Parsing CaseFolding.txt\n");
	file = fopen("ucd/CaseFolding.txt", "r");
	if (!file)
		exit(1);

	count = 0;
	while (fgets(line, LINESIZE, file)) {
		ret = sscanf(line, "%X; %[^;];%[^;];",
			     &unichar, &status, buf0);
		if (ret != 3)
			continue;
		if (status[0] != 'C' && status[0] != 'F')
			/* We are doing full case folding */
			continue;

		s = buf0;

		/* decode the case folding into UTF-32 */
		i = 0;
		while (*s) {
			mapping[i] = strtoul(s, &s, 16);
			i++;
		}
		mapping[i++] = 0;

		um = malloc(i * sizeof(unsigned int));
		if (!um)
			exit(1);
		memcpy(um, mapping, i * sizeof(unsigned int));
		trie_insert(cf_root, unichar, um);

		count++;
	}
	fclose(file);
	if (verbose > 0)
		printf("Found %d entries\n", count);
	if (count == 0)
		exit(1);
}

static void ccc_init(struct trie_node *ccc_root)
{
	FILE *file;
	unsigned int unichar;
	unsigned int *ccc;
	int count;
	int ret;

	if (verbose > 0)
		printf("Parsing UnicodeData.txt\n");
	file = fopen("ucd/UnicodeData.txt", "r");
	if (!file)
		exit(1);

	count = 0;
	while (fgets(line, LINESIZE, file)) {
		ccc = malloc(sizeof(*ccc));
		ret = sscanf(line, "%X;%*[^;];%*[^;];%d", &unichar, ccc);
		if (ret != 2)
			continue;
		if (*ccc == 0) /* This is the default value */
			continue;

		trie_insert(ccc_root, unichar, ccc);

		count++;
	}
	fclose(file);
	if (verbose > 0)
		printf("Found %d entries\n", count);
	if (count == 0)
		exit(1);
}

static void trie_print(struct trie_node *root, char *trie_name, FILE *file,
		       bool is_ccc)
{
	struct trie_node *n = root;
	char range[5];
	int i;
	unsigned int value_pos = 0;
	int count;

	if (verbose > 0)
		printf("Printing to unicode.c\n");

	trie_calculate_positions(root, is_ccc);

	if (is_ccc)
		fprintf(file, "static u8 apfs_%s_trie[] = {\n", trie_name);
	else
		fprintf(file, "static u16 apfs_%s_trie[] = {\n", trie_name);

	for (i = 0; i < 5; ++i) {
		for (n = level_first(root, i); n; n = level_next(n)) {
			int j;

			get_range(n, range);
			fprintf(file, "\t/* Node for range 0x%.5s */\n",
				range);

			for (j = 0; j < 16; j++) {
				unsigned int pos = 0;

				if (j % 8 == 0)
					fprintf(file, "\t");
				if (n->children[j])
					pos = n->children[j]->pos;

				if (is_ccc)
					fprintf(file, "0x%.2x,", pos);
				else
					fprintf(file, "0x%.4x,", pos);

				if (j % 8 != 7)
					fprintf(file, " ");
				else
					fprintf(file, "\n");
			}
		}
	}
	fseek(file, -1, SEEK_CUR); /* Remove the final space or newline */
	fprintf(file, "\n};\n");

	if (is_ccc) /* No data array for ccc */
		return;

	fprintf(file, "\nstatic unicode_t apfs_%s[] = {\n", trie_name);
	count = 0;
	for (n = level_first(root, 5); n; n = level_next(n)) {
		unsigned int *curr;

		for (curr = n->value; *curr; curr++) {
			if (count % 6 == 0)
				fprintf(file, "\t");
			fprintf(file, "0x%.6x,", *curr);
			count++;
			if (count % 6 != 0)
				fprintf(file, " ");
			else
				fprintf(file, "\n");
		}
	}
	fseek(file, -1, SEEK_CUR); /* Remove the final space or newline */
	fprintf(file, "\n};\n");
}

/* Get the partial nfd decomposition for @unichar in the current iteration */
static unsigned int *get_current_nfd(struct trie_node *nfdi_root,
				     unsigned int unichar)
{
	struct trie_node *node = nfdi_root;
	int off;
	int i;

	for (i = 4; i >= 0; --i) {
		int index = (unichar >> (4 * i)) & 0xf;
		node = node->children[index];
		if (!node)
			return NULL;
	}
	return node->value;
}

/* Iterate the unicode decompositions as much as needed */
static void nfdi_iterate(struct trie_node *nfdi_root)
{
	struct trie_node *n;
	unsigned int *unichar;
	unsigned int mapping[19]; /* Magic - guaranteed not to be exceeded. */
	bool unchanged = true;

	for (n = level_first(nfdi_root, 5); n; n = level_next(n)) {
		unsigned int *map_cursor = mapping;
		unsigned int map_size;

		for (unichar = n->value; *unichar; ++unichar) {
			unsigned int *decomp;
			int len;

			decomp = get_current_nfd(nfdi_root, *unichar);
			if (decomp) {
				unchanged = false;
				len = unilength(decomp);
			} else {
				decomp = unichar;
				len = 1;
			}
			memcpy(map_cursor, decomp, len * sizeof(unsigned int));
			map_cursor += len;
		}
		*map_cursor = 0;
		++map_cursor;
		map_size = (map_cursor - mapping) * sizeof(unsigned int);

		free(n->value);
		n->value = malloc(map_size);
		if (!n->value)
			exit(1);
		memcpy(n->value, mapping, map_size);
	}

	if (!unchanged)
		nfdi_iterate(nfdi_root);
}

int main()
{
	struct trie_node *nfd_root, *cf_root, *ccc_root;
	FILE *out;

	out = fopen("unicode.c.tmp", "w");
	if (!out)
		exit(1);

	nfd_root = calloc(1, sizeof(*nfd_root));
	if (!nfd_root)
		exit(1);
	nfd_root->depth = 0;
	nfdi_init(nfd_root);
	nfdi_iterate(nfd_root);
	trie_print(nfd_root, "nfd", out, false /* is_ccc */);

	fprintf(out, "\n");

	#ifdef CONFIG_TEST
	/* The tests are only for NFD, case folding data should be empty */
	fprintf(out, "static u16 apfs_cf_trie[] = {"
		     "0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0"
		     "};\n");
	fprintf(out, "static unicode_t apfs_cf[] = {};\n");
	#else /* CONFIG_TEST */
	cf_root = calloc(1, sizeof(*cf_root));
	if (!cf_root)
		exit(1);
	cf_root->depth = 0;
	cf_init(cf_root);
	trie_print(cf_root, "cf", out, false /* is_ccc */);
	#endif /* CONFIG_TEST */

	fprintf(out, "\n");

	ccc_root = calloc(1, sizeof(*ccc_root));
	if (!ccc_root)
		exit(1);
	ccc_root->depth = 0;
	ccc_init(ccc_root);
	trie_print(ccc_root, "ccc", out, true /* is_ccc */);

	return 0;
}

