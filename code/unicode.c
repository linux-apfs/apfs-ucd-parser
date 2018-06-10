/* The arrays of unicode data are defined at the bottom of the file */
/* TODO: would a single trie with all the data be more efficient? */
static u16 apfs_nfd_trie[];
static unicode_t apfs_nfd[];
static u16 apfs_cf_trie[];
static unicode_t apfs_cf[];
static u8 apfs_ccc_trie[];

#define TRIE_HEIGHT		5

/* A trie node has one child for each possible nibble in the key */
#define TRIE_CHILD_SHIFT	4
#define TRIE_CHILD_MASK		((1 << TRIE_CHILD_SHIFT) - 1)

/* A trie value length is stored in the last three bits of its position */
#define TRIE_POS_SHIFT		3
#define TRIE_SIZE_MASK		((1 << TRIE_POS_SHIFT) - 1)

/**
 * apfs_trie_find - Look up a trie value
 * @trie:	trie to search
 * @key:	search key (a unicode character)
 * @result:	on return, this either holds the value (on a ccc lookup) or its
 *		position in the value array (on a cf or nfd lookup).
 * @is_ccc:	true if this a ccc (canonical combining class) lookup
 *
 * Returns the length of the value (0 if it doesn't exist).
 */
static int apfs_trie_find(void *trie, unicode_t key, void *result, bool is_ccc)
{
	int node = 0;
	int h;

	for (h = TRIE_HEIGHT - 1; h >= 0; --h) {
		int child = (key >> (TRIE_CHILD_SHIFT * h)) & TRIE_CHILD_MASK;
		int child_index = (node << TRIE_CHILD_SHIFT) + child;

		if (is_ccc)
			node = ((u8 *)trie)[child_index];
		else
			node = ((u16 *)trie)[child_index];

		if (node == 0) {
			*(u8 *)result = 0;
			return 0;
		}
	}

	if (is_ccc) {
		/* ccc values fit in one byte, so no need for a value array */
		*(u8 *)result = node;
		return 1;
	}

	*(u16 *)result = node >> TRIE_POS_SHIFT;
	return node & TRIE_SIZE_MASK;
}

#define HANGUL_S_BASE	0xac00
#define HANGUL_L_BASE	0x1100
#define HANGUL_V_BASE	0x1161
#define HANGUL_T_BASE	0x11a7
#define HANGUL_L_COUNT	19
#define HANGUL_V_COUNT	21
#define HANGUL_T_COUNT	28
#define HANGUL_N_COUNT	(HANGUL_V_COUNT * HANGUL_T_COUNT)
#define HANGUL_S_COUNT	(HANGUL_L_COUNT * HANGUL_N_COUNT)

/**
 * apfs_try_decompose_hangul - Try to decompose a unicode character as Hangul
 * @utf32char:	character to decompose
 * @buf:	buffer to store the result. Must be large enough!
 *
 * Returns 0 if @utf32char is not Hangul, otherwise returns the length of the
 * decomposition. Can be called with @buf == NULL to compute the size of the
 * buffer required.
 *
 * This function was adapted from sample code in section 3.12 of the
 * Unicode Standard, version 9.0.
 *
 * Copyright (C) 1991-2018 Unicode, Inc.  All rights reserved.  Distributed
 * under the Terms of Use in http://www.unicode.org/copyright.html.
 */
static int apfs_try_decompose_hangul(unicode_t utf32char, unicode_t *buf)
{
	int index, len;
	int l, v, t;

	index = utf32char - HANGUL_S_BASE;
	if (index < 0 || index >= HANGUL_S_COUNT)
		return 0;

	l = HANGUL_L_BASE + index / HANGUL_N_COUNT;
	v = HANGUL_V_BASE + (index % HANGUL_N_COUNT) / HANGUL_T_COUNT;
	t = HANGUL_T_BASE + index % HANGUL_T_COUNT;

	len = (t == HANGUL_T_BASE) ? 2 : 3;
	if (buf) {
		buf[0] = l;
		buf[1] = v;
		if (len == 3)
			buf[2] = t;
	}

	return len;
}

/**
 * apfs_init_unicursor - Allocate and initialize an apfs_unicursor structure
 * @utf8str:	the string to normalize
 *
 * Returns a unicode cursor that can be passed to apfs_normalize_next(); or
 * NULL in case of failure. Remember to call apfs_free_unicursor() afterwards.
 */
struct apfs_unicursor *apfs_init_unicursor(const char *utf8str)
{
	struct apfs_unicursor *cursor;

	cursor = kmalloc(sizeof(*cursor), GFP_KERNEL);
	if (!cursor)
		return NULL;

	cursor->utf8next = utf8str;
	cursor->buf = NULL;
	cursor->buf_len = 0;
	return cursor;
}

/**
 * apfs_free_unicursor - Free an apfs_unicursor structure and its buffer
 * @cursor:	cursor to free. Can be NULL
 */
void apfs_free_unicursor(struct apfs_unicursor *cursor)
{
	if (!cursor)
		return;
	kfree(cursor->buf);
	kfree(cursor);
}

/**
 * apfs_normalize_char - Normalize a unicode character
 * @utf32char:	character to normalize
 * @buf:	buffer to store the result. Must be large enough!
 *
 * Can be called with @buf == NULL to compute the size of the buffer
 * required. Returns the length of the normalization.
 */
static int apfs_normalize_char(unicode_t utf32char, unicode_t *buf)
{
	int nfd_len, norm_len;
	unicode_t *nfd, *cf;
	u16 off;
	int ret;

	norm_len = apfs_try_decompose_hangul(utf32char, buf);
	if (norm_len) /* Capitalization is not a concern for Hangul */
		return norm_len;

	ret = apfs_trie_find(apfs_nfd_trie, utf32char,
			     &off, false /* is_ccc */);
	if (!ret) {
		/* The decomposition is just the same character */
		nfd_len = 1;
		nfd = &utf32char;
	} else {
		nfd_len = ret;
		nfd = &apfs_nfd[off];
	}

	for (; nfd_len > 0; nfd++, nfd_len--) {
		int cf_len;

		ret = apfs_trie_find(apfs_cf_trie, *nfd,
				     &off, false /* is_ccc */);
		if (!ret) {
			/* The case folding is just the same character */
			cf_len = 1;
			cf = nfd;
		} else {
			cf_len = ret;
			cf = &apfs_cf[off];
		}

		if (buf)
			memcpy(buf + norm_len, cf, cf_len * sizeof(*cf));

		norm_len += cf_len;
	}

	return norm_len;
}

/* apfs_normalize_str() temporarily keeps the ccc in the top byte of a char */
#define TMP_CCC_SHIFT	24
#define TMP_CCC_MASK	(0xFFU << TMP_CCC_SHIFT)
#define TMP_CHAR_MASK	((1 << TMP_CCC_SHIFT) - 1)

/**
 * apfs_normalize_str - Normalize a UTF-8 string and convert it to UTF-32
 * @utf8str:	string to normalize. Must be valid, and have only one starter
 * @utf8end:	first char after the end of the string
 * @buf:	buffer to store the result. Must be large enough!
 *
 * This function is meant to normalize the substring between two starter
 * characters, which is the scope where reordering may be needed.
 */
static void apfs_normalize_str(const char *utf8str, const char *utf8end,
			       unicode_t *buf)
{
	bool ordered;
	int off = 0;
	int i;

	while (utf8str < utf8end) {
		unicode_t utf32;

		utf8str += utf8_to_utf32(utf8str, 4, &utf32);
		off += apfs_normalize_char(utf32, buf + off);
	}

	/* Remember the ccc's for now, to prevent repetition of trie searches */
	for (i = 0; i < off; ++i) {
		u8 ccc;

		apfs_trie_find(apfs_ccc_trie, buf[i], &ccc, true /* is_ccc */);
		buf[i] = buf[i] + (ccc << TMP_CCC_SHIFT);
	}

	/* Now run the Canonical Ordering Algorithm (bubble sort by ccc) */
	do {
		ordered = true;
		for (i = 0; i < off - 1; ++i) {
			if ((buf[i] & TMP_CCC_MASK) >
			    (buf[i + 1] & TMP_CCC_MASK)) {
				swap(buf[i], buf[i + 1]);
				ordered = false;
			}
		}
	} while (!ordered);

	/* The ccc values no longer matter, forget them */
	for (i = 0; i < off; ++i)
		buf[i] = buf[i] & TMP_CHAR_MASK;
	buf[off] = 0;	/* NULL-terminate the buffer */
}

/**
 * apfs_normalize_next - Get the next normalized character from a cursor
 * @cursor:	the unicode cursor for the string
 * @next:	on return, the next normalized character
 *
 * This function places one UTF-32 character on @next each time it's called,
 * after doing any needed normalization and case folding work. Reordering is
 * sometimes necessary, so all characters until the next starter will be
 * normalized at once; this is not visible to the caller.
 *
 * Returns 0, or a negative error code in case of failure.
 */
int apfs_normalize_next(struct apfs_unicursor *cursor, unicode_t *next)
{
	const char *utf8str = cursor->utf8next;
	int buflen = 1; /* An extra char for the NULL termination */

	if (cursor->buf && *(cursor->buf + cursor->buf_off))
		goto out;

	if (likely(isascii(*utf8str))) {
		*next = tolower(*utf8str);
		cursor->utf8next++;
		return 0;
	}

	cursor->buf_off = 0;
	while (*cursor->utf8next != 0) {
		int charlen;
		unicode_t utf32;
		u8 ccc;

		charlen = utf8_to_utf32(cursor->utf8next, 4, &utf32);
		if (charlen < 0) /* Invalid unicode */
			return -EINVAL;

		apfs_trie_find(apfs_ccc_trie, utf32, &ccc, true /* is_ccc */);
		if (buflen > 1 && ccc == 0) /* Never reorder starter chars */
			break;

		cursor->utf8next += charlen;
		buflen += apfs_normalize_char(utf32, NULL);
	}

	if (buflen > cursor->buf_len) {
		/* We need a bigger buffer for this sequence of chars */
		kfree(cursor->buf);
		cursor->buf = kmalloc_array(buflen, sizeof(unicode_t),
					    GFP_KERNEL);
		if (!cursor->buf)
			return -ENOMEM;
		cursor->buf_len = buflen;
	}
	apfs_normalize_str(utf8str, cursor->utf8next, cursor->buf);

out:
	*next = *(cursor->buf + cursor->buf_off++);
	return 0;
}

/*
 * The following arrays were built with data provided by the Unicode Standard,
 * version 9.0.
 *
 * Copyright (C) 1991-2018 Unicode, Inc.  All rights reserved.  Distributed
 * under the Terms of Use in http://www.unicode.org/copyright.html.
 */

