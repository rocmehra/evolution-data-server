/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-nntp-store.h : class for an nntp store */

/* 
 *
 * Copyright (C) 2000 Ximian, Inc. <toshok@ximian.com>
 *
 * This program is free software; you can redistribute it and/or 
 * modify it under the terms of version 2 of the GNU General Public 
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 */


#ifndef CAMEL_NNTP_STORE_H
#define CAMEL_NNTP_STORE_H 1


#ifdef __cplusplus
extern "C" {
#pragma }
#endif /* __cplusplus */

#include <camel/camel-store.h>
#include <camel/camel-stream-mem.h>
#include <camel/camel-data-cache.h>
#include <camel/camel-exception.h>
#include <camel/camel-folder.h>

#include <camel/camel-disco-store.h>
#include <camel/camel-disco-folder.h>

#include "camel-nntp-stream.h"
#include "camel-nntp-store-summary.h"

#define CAMEL_NNTP_STORE_TYPE     (camel_nntp_store_get_type ())
#define CAMEL_NNTP_STORE(obj)     (CAMEL_CHECK_CAST((obj), CAMEL_NNTP_STORE_TYPE, CamelNNTPStore))
#define CAMEL_NNTP_STORE_CLASS(k) (CAMEL_CHECK_CLASS_CAST ((k), CAMEL_NNTP_STORE_TYPE, CamelNNTPStoreClass))
#define CAMEL_IS_NNTP_STORE(o)    (CAMEL_CHECK_TYPE((o), CAMEL_NNTP_STORE_TYPE))

#define CAMEL_NNTP_EXT_SEARCH     (1<<0)
#define CAMEL_NNTP_EXT_SETGET     (1<<1)
#define CAMEL_NNTP_EXT_OVER       (1<<2)
#define CAMEL_NNTP_EXT_XPATTEXT   (1<<3)
#define CAMEL_NNTP_EXT_XACTIVE    (1<<4)
#define CAMEL_NNTP_EXT_LISTMOTD   (1<<5)
#define CAMEL_NNTP_EXT_LISTSUBSCR (1<<6)
#define CAMEL_NNTP_EXT_LISTPNAMES (1<<7)

typedef struct _CamelNNTPStore CamelNNTPStore;
typedef struct _CamelNNTPStoreClass CamelNNTPStoreClass;

#include "camel-nntp-grouplist.h"

struct _CamelNNTPStore {
	CamelDiscoStore parent_object;	
	
	struct _CamelNNTPStorePrivate *priv;
	
	guint32 extensions;
	
	gboolean posting_allowed;
	gboolean do_short_folder_notation, folder_hierarchy_relative;

	CamelNNTPStoreSummary *summary;
	
	CamelNNTPStream *stream;
	CamelStreamMem *mem;
	
	CamelDataCache *cache;
	
	char *current_folder, *storage_path, *base_url;
};

struct _CamelNNTPStoreClass {
	CamelDiscoStoreClass parent_class;

};

/* Standard Camel function */
CamelType camel_nntp_store_get_type (void);

int camel_nntp_command(CamelNNTPStore *store, char **line, const char *fmt, ...);
int camel_nntp_store_set_folder(CamelNNTPStore *store, CamelFolder *folder, CamelFolderChangeInfo *changes, CamelException *ex);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* CAMEL_NNTP_STORE_H */


