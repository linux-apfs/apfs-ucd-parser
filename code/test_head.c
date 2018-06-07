#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "unicode.h"

#define ENOMEM 1
#define EINVAL 2

typedef uint8_t u8;
typedef uint16_t u16;

#define swap(a, b) \
	do { typeof(a) __tmp = (a); (a) = (b); (b) = __tmp; } while (0)

static inline void kfree(void *ptr)
{
	free(ptr);
}

#define GFP_KERNEL 0

/* The flags are just ignored */
static inline void *kmalloc(size_t size, unsigned int flags)
{
	return malloc(size);
}

/* Don't bother protecting against overflow, this is only used once, safely */
static inline void *kmalloc_array(size_t n, size_t size, unsigned int flags)
{
	return kmalloc(n * size, flags);
}

/*
 * Sample implementation from Unicode home page.
 * http://www.stonehand.com/unicode/standard/fss-utf.html
 */
struct utf8_table {
	int     cmask;
	int     cval;
	int     shift;
	long    lmask;
	long    lval;
};

static const struct utf8_table utf8_table[] =
{
    {0x80,  0x00,   0*6,    0x7F,           0,         /* 1 byte sequence */},
    {0xE0,  0xC0,   1*6,    0x7FF,          0x80,      /* 2 byte sequence */},
    {0xF0,  0xE0,   2*6,    0xFFFF,         0x800,     /* 3 byte sequence */},
    {0xF8,  0xF0,   3*6,    0x1FFFFF,       0x10000,   /* 4 byte sequence */},
    {0xFC,  0xF8,   4*6,    0x3FFFFFF,      0x200000,  /* 5 byte sequence */},
    {0xFE,  0xFC,   5*6,    0x7FFFFFFF,     0x4000000, /* 6 byte sequence */},
    {0,						       /* end of table    */}
};

#define UNICODE_MAX	0x0010ffff
#define PLANE_SIZE	0x00010000

#define SURROGATE_MASK	0xfffff800
#define SURROGATE_PAIR	0x0000d800
#define SURROGATE_LOW	0x00000400
#define SURROGATE_BITS	0x000003ff

static int utf8_to_utf32(const u8 *s, int inlen, unicode_t *pu)
{
	unsigned long l;
	int c0, c, nc;
	const struct utf8_table *t;

	nc = 0;
	c0 = *s;
	l = c0;
	for (t = utf8_table; t->cmask; t++) {
		nc++;
		if ((c0 & t->cmask) == t->cval) {
			l &= t->lmask;
			if (l < t->lval || l > UNICODE_MAX ||
					(l & SURROGATE_MASK) == SURROGATE_PAIR)
				return -1;
			*pu = (unicode_t) l;
			return nc;
		}
		if (inlen <= nc)
			return -1;
		s++;
		c = (*s ^ 0x80) & 0xFF;
		if (c & 0xC0)
			return -1;
		l = (l << 6) | c;
	}
	return -1;
}

int utf32_to_utf8(unicode_t u, u8 *s, int maxout)
{
	unsigned long l;
	int c, nc;
	const struct utf8_table *t;

	if (!s)
		return 0;

	l = u;
	if (l > UNICODE_MAX || (l & SURROGATE_MASK) == SURROGATE_PAIR)
		return -1;

	nc = 0;
	for (t = utf8_table; t->cmask && maxout; t++, maxout--) {
		nc++;
		if (l <= t->lmask) {
			c = t->shift;
			*s = (u8) (t->cval | (l >> c));
			while (c > 0) {
				c -= 6;
				s++;
				*s = (u8) (0x80 | ((l >> c) & 0x3F));
			}
			return nc;
		}
	}
	return -1;
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

/* Test if @str normalizes to @norm and print the result */
void test_normalization(unicode_t *str, unicode_t *norm)
{
	struct apfs_unicursor *cursor;
	int maxlen;
	u8 *utf8str, *utf8curr;

	maxlen = unilength(str) * 4 + 1; /* 4 UTF-8 bytes top, for each char */
	utf8str = malloc(maxlen);
	if (!utf8str) {
		printf("Memory allocation failure!\n");
		exit(1);
	}

	utf8curr = utf8str;
	for (; *str; str++) {
		int len;

		len = utf32_to_utf8(*str, utf8curr, maxlen);
		if (len < 0) /* Invalid UTF-32, ignore */
			goto out;
		utf8curr += len;
		maxlen -= len;
	}
	*utf8curr = 0;

	cursor = apfs_init_unicursor(utf8str);
	while (1) {
		unicode_t curr;
		int ret;

		ret = apfs_normalize_next(cursor, &curr);
		if (ret) {
			printf("FAIL: invalid UTF-8 for string %s\n", utf8str);
			break;
		}
		if (curr != *norm) {
			printf("FAIL: wrong NFD for string %s\n", utf8str);
			break;
		}
		if (!curr) {
			printf("Successful test for string %s\n", utf8str);
			break;
		}
		norm++;
	}
	apfs_free_unicursor(cursor);

out:
	free(utf8str);
}

/* Test if all chars between @prev and @curr normalize to themselves */
void test_unlisted_chars(unicode_t prev, unicode_t curr)
{
	unicode_t unichar[2];

	unichar[1] = 0;
	for (unichar[0] = prev + 1; unichar[0] < curr; ++unichar[0])
		test_normalization(unichar, unichar);
}

#define LINESIZE 1024
char line[LINESIZE];
char col[5][LINESIZE];

/* Parse the tests and run them */
int main()
{
	unicode_t map[5][19];
	FILE *file;
	int part = 0;

	file = fopen("ucd/NormalizationTest.txt", "r");
	if (!file) {
		printf("Failure to read test data!\n");
		exit(1);
	}

	while (fgets(line, LINESIZE, file)) {
		int ret;
		unicode_t prev;
		int i;

		/* Part1 needs additional tests for unlisted chars */
		sscanf(line, "@Part%d", &part);

		/* The docs count columns from one, so ci is col[i-1] here */
		ret = sscanf(line, "%[^#;];%[^;];%[^;];%[^;];%[^;];",
			     col[0], col[1], col[2], col[3], col[4]);
		if (ret != 5)
			continue;

		if (part == 1)
			prev = map[0][0];
		for (i = 0; i < 5; ++i) {
			int j = 0;
			char *s = col[i];

			while (*s)
				map[i][j++] = strtoul(s, &s, 16);
			map[i][j] = 0;
		}

		if (part == 1)
			test_unlisted_chars(prev, map[0][0]);

		/* Expected normalizations in the tests provided by unicode */
		test_normalization(map[0], map[2]);
		test_normalization(map[1], map[2]);
		test_normalization(map[2], map[2]);
		test_normalization(map[3], map[4]);
		test_normalization(map[4], map[4]);
	}

	fclose(file);
	return 0;
}

