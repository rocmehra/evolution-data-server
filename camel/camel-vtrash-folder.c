/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  Authors: Jeffrey Stedfast <fejj@ximian.com>
 *
 *  Copyright 2001 Ximian, Inc. (www.ximian.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 */

#include <config.h>

#include "camel-exception.h"
#include "camel-vtrash-folder.h"
#include "camel-store.h"
#include "camel-vee-store.h"
#include "camel-mime-message.h"

#include <string.h>

/* Returns the class for a CamelFolder */
#define CF_CLASS(so) CAMEL_FOLDER_CLASS (CAMEL_OBJECT_GET_CLASS(so))

static CamelVeeFolderClass *camel_vtrash_folder_parent;

static void vtrash_append_message (CamelFolder *folder, CamelMimeMessage *message,
				   const CamelMessageInfo *info, CamelException *ex);
static void vtrash_transfer_messages_to (CamelFolder *folder, GPtrArray *uids, CamelFolder *dest, gboolean delete_originals, CamelException *ex);

static void
camel_vtrash_folder_class_init (CamelVTrashFolderClass *klass)
{
	CamelFolderClass *folder_class = (CamelFolderClass *) klass;
	
	camel_vtrash_folder_parent =
		CAMEL_VEE_FOLDER_CLASS (camel_type_get_global_classfuncs (camel_folder_get_type ()));
	
	folder_class->append_message = vtrash_append_message;
	folder_class->transfer_messages_to = vtrash_transfer_messages_to;
}

static void
camel_vtrash_folder_init (CamelVTrashFolder *vtrash)
{
	CamelFolder *folder = CAMEL_FOLDER (vtrash);

	folder->folder_flags |= CAMEL_FOLDER_IS_TRASH;
}

CamelType
camel_vtrash_folder_get_type (void)
{
	static CamelType type = CAMEL_INVALID_TYPE;
	
	if (type == CAMEL_INVALID_TYPE) {
		type = camel_type_register (camel_vee_folder_get_type (),
					    "CamelVTrashFolder",
					    sizeof (CamelVTrashFolder),
					    sizeof (CamelVTrashFolderClass),
					    (CamelObjectClassInitFunc) camel_vtrash_folder_class_init,
					    NULL,
					    (CamelObjectInitFunc) camel_vtrash_folder_init,
					    NULL);
	}
	
	return type;
}

/**
 * camel_vtrash_folder_new:
 * @parent_store: the parent CamelVeeStore
 * @name: the vfolder name
 * @ex: a CamelException
 *
 * Create a new CamelVeeFolder object.
 *
 * Return value: A new CamelVeeFolder widget.
 **/
CamelFolder *
camel_vtrash_folder_new (CamelStore *parent_store, const char *name)
{
	CamelFolder *vtrash;
	
	vtrash = (CamelFolder *)camel_object_new (camel_vtrash_folder_get_type ());
	camel_vee_folder_construct (CAMEL_VEE_FOLDER (vtrash), parent_store, name,
				    CAMEL_STORE_FOLDER_PRIVATE | CAMEL_STORE_FOLDER_CREATE | CAMEL_STORE_VEE_FOLDER_AUTO);
	camel_vee_folder_set_expression((CamelVeeFolder *)vtrash, "(match-all (system-flag \"Deleted\"))");

	return vtrash;
}

static void
vtrash_append_message (CamelFolder *folder, CamelMimeMessage *message, const CamelMessageInfo *info, CamelException *ex)
{
	/* no-op */
}

struct _transfer_data {
	CamelFolder *folder;
	CamelFolder *dest;
	GPtrArray *uids;
	gboolean delete;
};

static void
transfer_messages(CamelFolder *folder, struct _transfer_data *md, CamelException *ex)
{
	int i;

	if (!camel_exception_is_set (ex))
		camel_folder_transfer_messages_to(md->folder, md->uids, md->dest, md->delete, ex);

	for (i=0;i<md->uids->len;i++)
		g_free(md->uids->pdata[i]);
	g_ptr_array_free(md->uids, TRUE);
	camel_object_unref((CamelObject *)md->folder);
	g_free(md);
}

static void
vtrash_transfer_messages_to (CamelFolder *source, GPtrArray *uids, CamelFolder *dest, gboolean delete_originals, CamelException *ex)
{
	CamelVeeMessageInfo *mi;
	int i;
	GHashTable *batch = NULL;
	const char *tuid;
	struct _transfer_data *md;

	/* This is a special case of transfer_messages_to: Either the
	 * source or the destination is a vtrash folder (but not both
	 * since a store should never have more than one).
	 */

	if (CAMEL_IS_VTRASH_FOLDER (dest)) {
		/* Copy to trash is meaningless. */
		if (!delete_originals)
			return;

		/* Move to trash is the same as deleting the message */
		for (i = 0; i < uids->len; i++)
			camel_folder_delete_message (source, uids->pdata[i]);
		return;
	}

	g_return_if_fail (CAMEL_IS_VTRASH_FOLDER (source));

	/* Moving/Copying from the trash to the original folder = undelete.
	 * Moving/Copying from the trash to a different folder = move/copy.
	 *
	 * Need to check this uid by uid, but we batch up the copies.
	 */

	for (i = 0; i < uids->len; i++) {
		mi = (CamelVeeMessageInfo *)camel_folder_get_message_info (source, uids->pdata[i]);
		if (mi == NULL) {
			g_warning ("Cannot find uid %s in source folder during transfer", (char *) uids->pdata[i]);
			continue;
		}
		
		if (dest == mi->folder) {
			/* Just undelete the original message */
			camel_folder_set_message_flags (source, uids->pdata[i], CAMEL_MESSAGE_DELETED, 0);
		} else {
			if (batch == NULL)
				batch = g_hash_table_new(NULL, NULL);
			md = g_hash_table_lookup(batch, mi->folder);
			if (md == NULL) {
				md = g_malloc0(sizeof(*md));
				md->folder = mi->folder;
				camel_object_ref((CamelObject *)md->folder);
				md->uids = g_ptr_array_new();
				md->dest = dest;
				g_hash_table_insert(batch, mi->folder, md);
			}

			tuid = uids->pdata[i];
			if (strlen(tuid)>8)
				tuid += 8;
			g_ptr_array_add(md->uids, g_strdup(tuid));
		}
		camel_folder_free_message_info (source, (CamelMessageInfo *)mi);
	}

	if (batch) {
		g_hash_table_foreach(batch, (GHFunc)transfer_messages, ex);
		g_hash_table_destroy(batch);
	}
}
