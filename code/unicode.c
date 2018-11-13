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

/**
 * apfs_init_unicursor - Initialize an apfs_unicursor structure
 * @cursor:	cursor to initialize
 * @utf8str:	string to normalize
 */
void apfs_init_unicursor(struct apfs_unicursor *cursor, const char *utf8str)
{
	cursor->utf8curr = utf8str;
	cursor->length = -1;
	cursor->last_pos = -1;
	cursor->last_ccc = 0;
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
 * apfs_is_precomposed_hangul - Check if a character is a Hangul syllable
 * @utf32char:	character to check
 *
 * This function was adapted from sample code in section 3.12 of the
 * Unicode Standard, version 9.0.
 *
 * Copyright (C) 1991-2018 Unicode, Inc.  All rights reserved.  Distributed
 * under the Terms of Use in http://www.unicode.org/copyright.html.
 */
static bool apfs_is_precomposed_hangul(unicode_t utf32char)
{
	int index;

	index = utf32char - HANGUL_S_BASE;
	return (index >= 0 && index < HANGUL_S_COUNT);
}

/* Signals the end of the normalization for a single character */
#define NORM_END	(unicode_t)(-1)

/**
 * apfs_decompose_hangul - Decompose a Hangul syllable
 * @utf32char:	Hangul syllable to decompose
 * @off:	offset of the wanted character from the decomposition
 *
 * Returns the single character at offset @off in the decomposition of
 * @utf32char, or NORM_END if this offset is past the end.
 *
 * This function was adapted from sample code in section 3.12 of the
 * Unicode Standard, version 9.0.
 *
 * Copyright (C) 1991-2018 Unicode, Inc.  All rights reserved.  Distributed
 * under the Terms of Use in http://www.unicode.org/copyright.html.
 */
static unicode_t apfs_decompose_hangul(unicode_t utf32char, int off)
{
	int index;
	int l, v, t;

	index = utf32char - HANGUL_S_BASE;

	l = HANGUL_L_BASE + index / HANGUL_N_COUNT;
	if (off == 0)
		return l;

	v = HANGUL_V_BASE + (index % HANGUL_N_COUNT) / HANGUL_T_COUNT;
	if (off == 1)
		return v;

	t = HANGUL_T_BASE + index % HANGUL_T_COUNT;
	if (off == 2 && t != HANGUL_T_BASE)
		return t;

	return NORM_END;
}

/**
 * apfs_normalize_char - Normalize a unicode character
 * @utf32char:	character to normalize
 * @off:	offset of the wanted character from the normalization
 * @case_fold:	case fold the char?
 *
 * Returns the single character at offset @off in the normalization of
 * @utf32char, or NORM_END if this offset is past the end.
 */
static unicode_t apfs_normalize_char(unicode_t utf32char, int off,
				     bool case_fold)
{
	int nfd_len;
	unicode_t *nfd, *cf;
	u16 pos;
	int ret;

	if (apfs_is_precomposed_hangul(utf32char)) /* Hangul has no case */
		return apfs_decompose_hangul(utf32char, off);

	ret = apfs_trie_find(apfs_nfd_trie, utf32char,
			     &pos, false /* is_ccc */);
	if (!ret) {
		/* The decomposition is just the same character */
		nfd_len = 1;
		nfd = &utf32char;
	} else {
		nfd_len = ret;
		nfd = &apfs_nfd[pos];
	}

	if (!case_fold) {
		if (off < nfd_len)
			return nfd[off];
		return NORM_END;
	}

	for (; nfd_len > 0; nfd++, nfd_len--) {
		int cf_len;

		ret = apfs_trie_find(apfs_cf_trie, *nfd,
				     &pos, false /* is_ccc */);
		if (!ret) {
			/* The case folding is just the same character */
			cf_len = 1;
			cf = nfd;
		} else {
			cf_len = ret;
			cf = &apfs_cf[pos];
		}

		if (off < cf_len)
			return cf[off];
		off -= cf_len;
	}

	return NORM_END;
}

/**
 * apfs_get_normalization_length - Count the characters until the next starter
 * @utf8str:	string to normalize, may begin with several starters
 * @case_fold:	true if the count should consider case folding
 *
 * Returns the number of unicode characters in the normalization of the
 * substring that begins at @utf8str and ends at the first nonconsecutive
 * starter. Or 0 if the substring has invalid UTF-8.
 */
static int apfs_get_normalization_length(const char *utf8str, bool case_fold)
{
	int utf8len, pos, norm_len = 0;
	bool starters_over = false;
	unicode_t utf32char;

	while (1) {
		if (!*utf8str)
			return norm_len;
		utf8len = utf8_to_utf32(utf8str, 4, &utf32char);
		if (utf8len < 0) /* Invalid unicode; don't normalize anything */
			return 0;

		for (pos = 0;; pos++, norm_len++) {
			unicode_t utf32norm;
			u8 ccc;

			utf32norm = apfs_normalize_char(utf32char, pos,
							case_fold);
			if (utf32norm == NORM_END)
				break;

			apfs_trie_find(apfs_ccc_trie, utf32norm, &ccc,
				       true /* is_ccc */);

			if (ccc != 0)
				starters_over = true;
			else if (starters_over) /* Reached the next starter */
				return norm_len;
		}
		utf8str += utf8len;
	}
}

/**
 * apfs_normalize_next - Return the next normalized character from a string
 * @cursor:	unicode cursor for the string
 * @case_fold:	case fold the string?
 *
 * Sets @cursor->length to the length of the normalized substring between
 * @cursor->utf8curr and the first nonconsecutive starter. Returns a single
 * normalized character, setting @cursor->last_ccc and @cursor->last_pos to
 * its CCC and position in the substring. When the end of the substring is
 * reached, updates @cursor->utf8curr to point to the beginning of the next
 * one.
 *
 * Returns 0 if the substring has invalid UTF-8.
 */
unicode_t apfs_normalize_next(struct apfs_unicursor *cursor, bool case_fold)
{
	const char *utf8str = cursor->utf8curr;
	int str_pos, min_pos = -1;
	unicode_t utf32min = 0;
	u8 min_ccc;

new_starter:
	if (likely(isascii(*utf8str))) {
		cursor->utf8curr = utf8str + 1;
		if (case_fold)
			return tolower(*utf8str);
		return *utf8str;
	}

	if (cursor->length < 0) {
		cursor->length = apfs_get_normalization_length(utf8str,
							       case_fold);
		if (cursor->length == 0)
			return 0;
	}

	str_pos = 0;
	min_ccc = 0xFF;	/* Above all possible ccc's */

	while (1) {
		unicode_t utf32char;
		int utf8len, pos;

		utf8len = utf8_to_utf32(utf8str, 4, &utf32char);
		for (pos = 0;; pos++, str_pos++) {
			unicode_t utf32norm;
			u8 ccc;

			utf32norm = apfs_normalize_char(utf32char, pos,
							case_fold);
			if (utf32norm == NORM_END)
				break;

			apfs_trie_find(apfs_ccc_trie, utf32norm, &ccc,
				       true /* is_ccc */);

			if (ccc >= min_ccc || ccc < cursor->last_ccc)
				continue;
			if (ccc > cursor->last_ccc ||
			    str_pos > cursor->last_pos) {
				utf32min = utf32norm;
				min_ccc = ccc;
				min_pos = str_pos;
			}
		}

		utf8str += utf8len;
		if (str_pos == cursor->length) {
			/* Reached the following starter */
			if (min_ccc != 0xFF) {
				/* Not done with this substring yet */
				cursor->last_ccc = min_ccc;
				cursor->last_pos = min_pos;
				return utf32min;
			}
			/* Continue from the next starter */
			apfs_init_unicursor(cursor, utf8str);
			goto new_starter;
		}
	}
}

/*
 * The following arrays were built with data provided by the Unicode Standard,
 * version 9.0.
 *
 * Copyright (C) 1991-2018 Unicode, Inc.  All rights reserved.  Distributed
 * under the Terms of Use in http://www.unicode.org/copyright.html.
 */

