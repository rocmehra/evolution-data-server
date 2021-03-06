/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2002 Ximian Inc.
 *
 * Authors: Michael Zucchi <notzed@ximian.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <libedataserver/e-memory.h>

#include "camel-file-utils.h"
#include "camel-private.h"
#include "camel-store.h"
#include "camel-utf8.h"

#include "camel-imap-store-summary.h"

#define d(x)
#define io(x)			/* io debug */

#define CAMEL_IMAP_STORE_SUMMARY_VERSION_0 (0)

#define CAMEL_IMAP_STORE_SUMMARY_VERSION (0)

#define _PRIVATE(o) (((CamelImapStoreSummary *)(o))->priv)

static void namespace_clear(CamelStoreSummary *s);

static int summary_header_load(CamelStoreSummary *, FILE *);
static int summary_header_save(CamelStoreSummary *, FILE *);

/*static CamelStoreInfo * store_info_new(CamelStoreSummary *, const char *);*/
static CamelStoreInfo * store_info_load(CamelStoreSummary *, FILE *);
static int		 store_info_save(CamelStoreSummary *, FILE *, CamelStoreInfo *);
static void		 store_info_free(CamelStoreSummary *, CamelStoreInfo *);

static const char *store_info_string(CamelStoreSummary *, const CamelStoreInfo *, int);
static void store_info_set_string(CamelStoreSummary *, CamelStoreInfo *, int, const char *);

static void camel_imap_store_summary_class_init (CamelImapStoreSummaryClass *klass);
static void camel_imap_store_summary_init       (CamelImapStoreSummary *obj);
static void camel_imap_store_summary_finalise   (CamelObject *obj);

static CamelStoreSummaryClass *camel_imap_store_summary_parent;

static void
camel_imap_store_summary_class_init (CamelImapStoreSummaryClass *klass)
{
	CamelStoreSummaryClass *ssklass = (CamelStoreSummaryClass *)klass;

	ssklass->summary_header_load = summary_header_load;
	ssklass->summary_header_save = summary_header_save;

	/*ssklass->store_info_new  = store_info_new;*/
	ssklass->store_info_load = store_info_load;
	ssklass->store_info_save = store_info_save;
	ssklass->store_info_free = store_info_free;

	ssklass->store_info_string = store_info_string;
	ssklass->store_info_set_string = store_info_set_string;
}

static void
camel_imap_store_summary_init (CamelImapStoreSummary *s)
{
	/*struct _CamelImapStoreSummaryPrivate *p;

	  p = _PRIVATE(s) = g_malloc0(sizeof(*p));*/

	((CamelStoreSummary *)s)->store_info_size = sizeof(CamelImapStoreInfo);
	s->version = CAMEL_IMAP_STORE_SUMMARY_VERSION;
}

static void
camel_imap_store_summary_finalise (CamelObject *obj)
{
	/*struct _CamelImapStoreSummaryPrivate *p;*/
	/*CamelImapStoreSummary *s = (CamelImapStoreSummary *)obj;*/

	/*p = _PRIVATE(obj);
	  g_free(p);*/
}

CamelType
camel_imap_store_summary_get_type (void)
{
	static CamelType type = CAMEL_INVALID_TYPE;

	if (type == CAMEL_INVALID_TYPE) {
		camel_imap_store_summary_parent = (CamelStoreSummaryClass *)camel_store_summary_get_type();
		type = camel_type_register((CamelType)camel_imap_store_summary_parent, "CamelImapStoreSummary",
					   sizeof (CamelImapStoreSummary),
					   sizeof (CamelImapStoreSummaryClass),
					   (CamelObjectClassInitFunc) camel_imap_store_summary_class_init,
					   NULL,
					   (CamelObjectInitFunc) camel_imap_store_summary_init,
					   (CamelObjectFinalizeFunc) camel_imap_store_summary_finalise);
	}

	return type;
}

/**
 * camel_imap_store_summary_new:
 *
 * Create a new CamelImapStoreSummary object.
 *
 * Return value: A new CamelImapStoreSummary widget.
 **/
CamelImapStoreSummary *
camel_imap_store_summary_new (void)
{
	CamelImapStoreSummary *new = CAMEL_IMAP_STORE_SUMMARY ( camel_object_new (camel_imap_store_summary_get_type ()));

	return new;
}

/**
 * camel_imap_store_summary_full_name:
 * @s:
 * @path:
 *
 * Retrieve a summary item by full name.
 *
 * A referenced to the summary item is returned, which may be
 * ref'd or free'd as appropriate.
 *
 * Return value: The summary item, or NULL if the @full_name name
 * is not available.
 * It must be freed using camel_store_summary_info_free().
 **/
CamelImapStoreInfo *
camel_imap_store_summary_full_name(CamelImapStoreSummary *s, const char *full_name)
{
	int count, i;
	CamelImapStoreInfo *info;

	count = camel_store_summary_count((CamelStoreSummary *)s);
	for (i=0;i<count;i++) {
		info = (CamelImapStoreInfo *)camel_store_summary_index((CamelStoreSummary *)s, i);
		if (info) {
			if (strcmp(info->full_name, full_name) == 0)
				return info;
			camel_store_summary_info_free((CamelStoreSummary *)s, (CamelStoreInfo *)info);
		}
	}

	return NULL;
}

char *
camel_imap_store_summary_full_to_path(CamelImapStoreSummary *s, const char *full_name, char dir_sep)
{
	char *path, *p;
	int c;
	const char *f;

	if (dir_sep != '/') {
		p = path = alloca(strlen(full_name)*3+1);
		f = full_name;
		while ( (c = *f++ & 0xff) ) {
			if (c == dir_sep)
				*p++ = '/';
			else if (c == '/' || c == '%')
				p += sprintf(p, "%%%02X", c);
			else
				*p++ = c;
		}
		*p = 0;
	} else
		path = (char *)full_name;

	return g_strdup(path);
}

static guint32 hexnib(guint32 c)
{
	if (c >= '0' && c <= '9')
		return c-'0';
	else if (c>='A' && c <= 'Z')
		return c-'A'+10;
	else
		return 0;
}

char *
camel_imap_store_summary_path_to_full(CamelImapStoreSummary *s, const char *path, char dir_sep)
{
	char *full, *f;
	guint32 c, v = 0;
	const char *p;
	int state=0;
	char *subpath, *last = NULL;
	CamelStoreInfo *si;
	CamelImapStoreNamespace *ns;

	/* check to see if we have a subpath of path already defined */
	subpath = alloca(strlen(path)+1);
	strcpy(subpath, path);
	do {
		si = camel_store_summary_path((CamelStoreSummary *)s, subpath);
		if (si == NULL) {
			last = strrchr(subpath, '/');
			if (last)
				*last = 0;
		}
	} while (si == NULL && last);

	/* path is already present, use the raw version we have */
	if (si && strlen(subpath) == strlen(path)) {
		f = g_strdup(camel_imap_store_info_full_name(s, si));
		camel_store_summary_info_free((CamelStoreSummary *)s, si);
		return f;
	}

	ns = camel_imap_store_summary_namespace_find_path(s, path);

	f = full = alloca(strlen(path)*2+1);
	if (si)
		p = path + strlen(subpath);
	else if (ns)
		p = path + strlen(ns->path);
	else
		p = path;

	while ((c = camel_utf8_getc((const unsigned char **)&p))) {
		switch(state) {
		case 0:
			if (c == '%')
				state = 1;
			else {
				if (c == '/')
					c = dir_sep;
				camel_utf8_putc((unsigned char **) &f, c);
			}
			break;
		case 1:
			state = 2;
			v = hexnib(c)<<4;
			break;
		case 2:
			state = 0;
			v |= hexnib(c);
			camel_utf8_putc((unsigned char **) &f, v);
			break;
		}
	}
	camel_utf8_putc((unsigned char **) &f, c);

	/* merge old path part if required */
	f = g_strdup(full);
	if (si) {
		full = g_strdup_printf("%s%s", camel_imap_store_info_full_name(s, si), f);
		g_free(f);
		camel_store_summary_info_free((CamelStoreSummary *)s, si);
		f = full;
	} else if (ns) {
		full = g_strdup_printf("%s%s", ns->full_name, f);
		g_free(f);
		f = full;
	}

	return f;
}

CamelImapStoreInfo *
camel_imap_store_summary_add_from_full(CamelImapStoreSummary *s, const char *full, char dir_sep)
{
	CamelImapStoreInfo *info;
	char *pathu8, *prefix;
	int len;
	char *full_name;
	CamelImapStoreNamespace *ns;

	d(printf("adding full name '%s' '%c'\n", full, dir_sep));

	len = strlen(full);
	full_name = alloca(len+1);
	strcpy(full_name, full);
	if (full_name[len-1] == dir_sep)
		full_name[len-1] = 0;

	info = camel_imap_store_summary_full_name(s, full_name);
	if (info) {
		camel_store_summary_info_free((CamelStoreSummary *)s, (CamelStoreInfo *)info);
		d(printf("  already there\n"));
		return info;
	}

	ns = camel_imap_store_summary_namespace_find_full(s, full_name);
	if (ns) {
		d(printf("(found namespace for '%s' ns '%s') ", full_name, ns->path));
		len = strlen(ns->full_name);
		if (len >= strlen(full_name)) {
			pathu8 = g_strdup(ns->path);
		} else {
			if (full_name[len] == ns->sep)
				len++;

			prefix = camel_imap_store_summary_full_to_path(s, full_name+len, ns->sep);
			if (*ns->path) {
				pathu8 = g_strdup_printf ("%s/%s", ns->path, prefix);
				g_free (prefix);
			} else {
				pathu8 = prefix;
			}
		}
		d(printf(" (pathu8 = '%s')", pathu8));
	} else {
		d(printf("(Cannot find namespace for '%s')\n", full_name));
		pathu8 = camel_imap_store_summary_full_to_path(s, full_name, dir_sep);
	}

	info = (CamelImapStoreInfo *)camel_store_summary_add_from_path((CamelStoreSummary *)s, pathu8);
	if (info) {
		d(printf("  '%s' -> '%s'\n", pathu8, full_name));
		camel_store_info_set_string((CamelStoreSummary *)s, (CamelStoreInfo *)info, CAMEL_IMAP_STORE_INFO_FULL_NAME, full_name);

		if (g_ascii_strcasecmp(full_name, "inbox"))
			info->info.flags |= CAMEL_FOLDER_SYSTEM|CAMEL_FOLDER_TYPE_INBOX;
	} else
		d(printf("  failed\n"));

	return info;
}

/* should this be const? */
/* TODO: deprecate/merge this function with path_to_full */
char *
camel_imap_store_summary_full_from_path(CamelImapStoreSummary *s, const char *path)
{
	CamelImapStoreNamespace *ns;
	char *name = NULL;

	ns = camel_imap_store_summary_namespace_find_path(s, path);
	if (ns)
		name = camel_imap_store_summary_path_to_full(s, path, ns->sep);

	d(printf("looking up path %s -> %s\n", path, name?name:"not found"));

	return name;
}

/* TODO: this api needs some more work */
CamelImapStoreNamespace *camel_imap_store_summary_namespace_new(CamelImapStoreSummary *s, const char *full_name, char dir_sep)
{
	CamelImapStoreNamespace *ns;
	char *p, *o, c;
	int len;

	ns = g_malloc0(sizeof(*ns));
	ns->full_name = g_strdup(full_name);
	len = strlen(ns->full_name)-1;
	if (len >= 0 && ns->full_name[len] == dir_sep)
		ns->full_name[len] = 0;
	ns->sep = dir_sep;

	o = p = ns->path = camel_imap_store_summary_full_to_path(s, ns->full_name, dir_sep);
	while ((c = *p++)) {
		if (c != '#') {
			if (c == '/')
				c = '.';
			*o++ = c;
		}
	}
	*o = 0;

	return ns;
}

void camel_imap_store_summary_namespace_set(CamelImapStoreSummary *s, CamelImapStoreNamespace *ns)
{
	d(printf("Setting namesapce to '%s' '%c' -> '%s'\n", ns->full_name, ns->sep, ns->path));
	namespace_clear((CamelStoreSummary *)s);
	s->namespace = ns;
	camel_store_summary_touch((CamelStoreSummary *)s);
}

CamelImapStoreNamespace *
camel_imap_store_summary_namespace_find_path(CamelImapStoreSummary *s, const char *path)
{
	int len;
	CamelImapStoreNamespace *ns;

	/* NB: this currently only compares against 1 namespace, in future compare against others */
	ns = s->namespace;
	while (ns) {
		len = strlen(ns->path);
		if (len == 0
		    || (strncmp(ns->path, path, len) == 0
			&& (path[len] == '/' || path[len] == 0)))
			break;
		ns = NULL;
	}

	/* have a default? */
	return ns;
}

CamelImapStoreNamespace *
camel_imap_store_summary_namespace_find_full(CamelImapStoreSummary *s, const char *full)
{
	int len;
	CamelImapStoreNamespace *ns;

	/* NB: this currently only compares against 1 namespace, in future compare against others */
	ns = s->namespace;
	while (ns) {
		len = strlen(ns->full_name);
		d(printf("find_full: comparing namespace '%s' to name '%s'\n", ns->full_name, full));
		if (len == 0
		    || (strncmp(ns->full_name, full, len) == 0
			&& (full[len] == ns->sep || full[len] == 0)))
			break;
		ns = NULL;
	}

	/* have a default? */
	return ns;
}

static void
namespace_free(CamelStoreSummary *s, CamelImapStoreNamespace *ns)
{
	g_free(ns->path);
	g_free(ns->full_name);
	g_free(ns);
}

static void
namespace_clear(CamelStoreSummary *s)
{
	CamelImapStoreSummary *is = (CamelImapStoreSummary *)s;

	if (is->namespace)
		namespace_free(s, is->namespace);
	is->namespace = NULL;
}

static CamelImapStoreNamespace *
namespace_load(CamelStoreSummary *s, FILE *in)
{
	CamelImapStoreNamespace *ns;
	guint32 sep = '/';

	ns = g_malloc0(sizeof(*ns));
	if (camel_file_util_decode_string(in, &ns->path) == -1
	    || camel_file_util_decode_string(in, &ns->full_name) == -1
	    || camel_file_util_decode_uint32(in, &sep) == -1) {
		namespace_free(s, ns);
		ns = NULL;
	} else {
		ns->sep = sep;
	}

	return ns;
}

static int
namespace_save(CamelStoreSummary *s, FILE *in, CamelImapStoreNamespace *ns)
{
	if (camel_file_util_encode_string(in, ns->path) == -1
	    || camel_file_util_encode_string(in, ns->full_name) == -1
	    || camel_file_util_encode_uint32(in, (guint32)ns->sep) == -1)
		return -1;

	return 0;
}

static int
summary_header_load(CamelStoreSummary *s, FILE *in)
{
	CamelImapStoreSummary *is = (CamelImapStoreSummary *)s;
	gint32 version, capabilities, count;

	namespace_clear(s);

	if (camel_imap_store_summary_parent->summary_header_load((CamelStoreSummary *)s, in) == -1
	    || camel_file_util_decode_fixed_int32(in, &version) == -1)
		return -1;

	is->version = version;

	if (version < CAMEL_IMAP_STORE_SUMMARY_VERSION_0) {
		g_warning("Store summary header version too low");
		return -1;
	}

	/* note file format can be expanded to contain more namespaces, but only 1 at the moment */
	if (camel_file_util_decode_fixed_int32(in, &capabilities) == -1
	    || camel_file_util_decode_fixed_int32(in, &count) == -1
	    || count > 1)
		return -1;

	is->capabilities = capabilities;
	if (count == 1) {
		if ((is->namespace = namespace_load(s, in)) == NULL)
			return -1;
	}

	return 0;
}

static int
summary_header_save(CamelStoreSummary *s, FILE *out)
{
	CamelImapStoreSummary *is = (CamelImapStoreSummary *)s;
	guint32 count;

	count = is->namespace?1:0;

	/* always write as latest version */
	if (camel_imap_store_summary_parent->summary_header_save((CamelStoreSummary *)s, out) == -1
	    || camel_file_util_encode_fixed_int32(out, CAMEL_IMAP_STORE_SUMMARY_VERSION) == -1
	    || camel_file_util_encode_fixed_int32(out, is->capabilities) == -1
	    || camel_file_util_encode_fixed_int32(out, count) == -1)
		return -1;

	if (is->namespace && namespace_save(s, out, is->namespace) == -1)
		return -1;

	return 0;
}

static CamelStoreInfo *
store_info_load(CamelStoreSummary *s, FILE *in)
{
	CamelImapStoreInfo *mi;

	mi = (CamelImapStoreInfo *)camel_imap_store_summary_parent->store_info_load(s, in);
	if (mi) {
		if (camel_file_util_decode_string(in, &mi->full_name) == -1) {
			camel_store_summary_info_free(s, (CamelStoreInfo *)mi);
			mi = NULL;
		} else {
			/* NB: this is done again for compatability */
			if (g_ascii_strcasecmp(mi->full_name, "inbox") == 0)
				mi->info.flags |= CAMEL_FOLDER_SYSTEM|CAMEL_FOLDER_TYPE_INBOX;
		}
	}

	return (CamelStoreInfo *)mi;
}

static int
store_info_save(CamelStoreSummary *s, FILE *out, CamelStoreInfo *mi)
{
	CamelImapStoreInfo *isi = (CamelImapStoreInfo *)mi;

	if (camel_imap_store_summary_parent->store_info_save(s, out, mi) == -1
	    || camel_file_util_encode_string(out, isi->full_name) == -1)
		return -1;

	return 0;
}

static void
store_info_free(CamelStoreSummary *s, CamelStoreInfo *mi)
{
	CamelImapStoreInfo *isi = (CamelImapStoreInfo *)mi;

	g_free(isi->full_name);
	camel_imap_store_summary_parent->store_info_free(s, mi);
}

static const char *
store_info_string(CamelStoreSummary *s, const CamelStoreInfo *mi, int type)
{
	CamelImapStoreInfo *isi = (CamelImapStoreInfo *)mi;

	/* FIXME: Locks? */

	g_assert (mi != NULL);

	switch (type) {
	case CAMEL_IMAP_STORE_INFO_FULL_NAME:
		return isi->full_name;
	default:
		return camel_imap_store_summary_parent->store_info_string(s, mi, type);
	}
}

static void
store_info_set_string(CamelStoreSummary *s, CamelStoreInfo *mi, int type, const char *str)
{
	CamelImapStoreInfo *isi = (CamelImapStoreInfo *)mi;

	g_assert(mi != NULL);

	switch(type) {
	case CAMEL_IMAP_STORE_INFO_FULL_NAME:
		d(printf("Set full name %s -> %s\n", isi->full_name, str));
		CAMEL_STORE_SUMMARY_LOCK(s, summary_lock);
		g_free(isi->full_name);
		isi->full_name = g_strdup(str);
		CAMEL_STORE_SUMMARY_UNLOCK(s, summary_lock);
		break;
	default:
		camel_imap_store_summary_parent->store_info_set_string(s, mi, type, str);
		break;
	}
}
