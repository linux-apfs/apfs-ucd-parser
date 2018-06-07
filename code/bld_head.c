// SPDX-License-Identifier: GPL-2.0
/*
 *  linux/fs/apfs/unicode.c
 *
 * Copyright (C) 2018 Ernesto A. Fernández <ernesto.mnd.fernandez@gmail.com>
 *
 * Routines and data for the normalization of unicode strings.
 * Somewhat based on linux/fs/hfsplus/unicode.c
 */

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/nls.h>
#include "unicode.h"

