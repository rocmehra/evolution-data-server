/*
 *  Copyright (C) 2000 Helix Code Inc.
 *
 *  Authors: Michael Zucchi <notzed@helixcode.com>
 *           Jeffrey Stedfast <fejj@helixcode.com>
 *
 *  This program is free software; you can redistribute it and/or 
 *  modify it under the terms of the GNU General Public License as 
 *  published by the Free Software Foundation; either version 2 of the
 *  License, or (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 *  USA
 */

#include "camel-exception.h"
#include "camel-vee-folder.h"
#include "camel-folder-summary.h"
#include "camel-mime-message.h"

#include <string.h>

/* our message info includes the parent folder */
typedef struct _CamelVeeMessageInfo {
	CamelMessageInfo info;
	CamelFolder *folder;
} CamelVeeMessageInfo;

struct _CamelVeeFolderPrivate {
	GList *folders;
};

#define _PRIVATE(o) (((CamelVeeFolder *)(o))->priv)

static void vee_sync (CamelFolder *folder, gboolean expunge, CamelException *ex);

static GPtrArray *vee_get_uids  (CamelFolder *folder);
GPtrArray *vee_get_summary (CamelFolder *folder);

static gint vee_get_message_count (CamelFolder *folder);
static gint vee_get_unread_message_count (CamelFolder *folder);
static CamelMimeMessage *vee_get_message (CamelFolder *folder, const gchar *uid, CamelException *ex);

static const CamelMessageInfo *vee_get_message_info (CamelFolder *folder, const char *uid);
static GPtrArray *vee_search_by_expression(CamelFolder *folder, const char *expression, CamelException *ex);

static guint32 vee_get_message_flags (CamelFolder *folder, const char *uid);
static void vee_set_message_flags (CamelFolder *folder, const char *uid, guint32 flags, guint32 set);
static gboolean vee_get_message_user_flag (CamelFolder *folder, const char *uid, const char *name);
static void vee_set_message_user_flag (CamelFolder *folder, const char *uid, const char *name, gboolean value);


static void camel_vee_folder_class_init (CamelVeeFolderClass *klass);
static void camel_vee_folder_init       (CamelVeeFolder *obj);
static void camel_vee_folder_finalise   (CamelObject *obj);

static void vee_folder_build(CamelVeeFolder *vf, CamelException *ex);
static void vee_folder_build_folder(CamelVeeFolder *vf, CamelFolder *source, CamelException *ex);

static CamelFolderClass *camel_vee_folder_parent;

CamelType
camel_vee_folder_get_type (void)
{
	static CamelType type = CAMEL_INVALID_TYPE;
	
	if (type == CAMEL_INVALID_TYPE) {
		type = camel_type_register (camel_folder_get_type (), "CamelVeeFolder",
					    sizeof (CamelVeeFolder),
					    sizeof (CamelVeeFolderClass),
					    (CamelObjectClassInitFunc) camel_vee_folder_class_init,
					    NULL,
					    (CamelObjectInitFunc) camel_vee_folder_init,
					    (CamelObjectFinalizeFunc) camel_vee_folder_finalise);
	}
	
	return type;
}

static void
camel_vee_folder_class_init (CamelVeeFolderClass *klass)
{
	CamelFolderClass *folder_class = (CamelFolderClass *) klass;

	camel_vee_folder_parent = CAMEL_FOLDER_CLASS(camel_type_get_global_classfuncs (camel_folder_get_type ()));

	folder_class->sync = vee_sync;

	folder_class->get_uids = vee_get_uids;
	folder_class->free_uids = camel_folder_free_deep;
	folder_class->get_summary = vee_get_summary;
	folder_class->free_summary = camel_folder_free_nop;
	folder_class->get_message = vee_get_message;

	folder_class->get_message_info = vee_get_message_info;

	folder_class->get_message_count = vee_get_message_count;
	folder_class->get_unread_message_count = vee_get_unread_message_count;
	folder_class->search_by_expression = vee_search_by_expression;

	folder_class->get_message_flags = vee_get_message_flags;
	folder_class->set_message_flags = vee_set_message_flags;
	folder_class->get_message_user_flag = vee_get_message_user_flag;
	folder_class->set_message_user_flag = vee_set_message_user_flag;
}

static void
camel_vee_folder_init (CamelVeeFolder *obj)
{
	struct _CamelVeeFolderPrivate *p;
	CamelFolder *folder = (CamelFolder *)obj;

	p = _PRIVATE(obj) = g_malloc0(sizeof(*p));

	folder->can_hold_messages = TRUE;
	folder->can_hold_folders = FALSE;
	folder->has_summary_capability = TRUE;
	folder->has_search_capability = TRUE;

	/* FIXME: what to do about user flags if the subfolder doesn't support them? */
	folder->permanent_flags = CAMEL_MESSAGE_ANSWERED |
		CAMEL_MESSAGE_DELETED |
		CAMEL_MESSAGE_DRAFT |
		CAMEL_MESSAGE_FLAGGED |
		CAMEL_MESSAGE_SEEN;
}

static void
camel_vee_folder_finalise (CamelObject *obj)
{
	CamelVeeFolder *vf = (CamelVeeFolder *)obj;
	struct _CamelVeeFolderPrivate *p = _PRIVATE(vf);
	GList *node;

	node = p->folders;
	while (node) {
		CamelFolder *f = node->data;
		camel_object_unref((CamelObject *)f);
		node = g_list_next(node);
	}
}

/**
 * camel_vee_folder_new:
 * @parent_store: the parent CamelVeeStore
 * @name: the vfolder name
 * @ex: a CamelException
 *
 * Create a new CamelVeeFolder object.
 *
 * Return value: A new CamelVeeFolder widget.
 **/
CamelFolder *
camel_vee_folder_new (CamelStore *parent_store, const char *name,
		      CamelException *ex)
{
	CamelFolder *folder;
	CamelVeeFolder *vf;
	char *namepart, *searchpart;

	folder =  CAMEL_FOLDER (camel_object_new (camel_vee_folder_get_type()));
	vf = (CamelVeeFolder *)folder;

	camel_folder_construct (folder, parent_store, name, name);

	namepart = g_strdup(name);
	searchpart = strchr(namepart, '?');
	if (searchpart == NULL) {
		/* no search, no result! */
		searchpart = "(body-contains \"=some-invalid_string-sequence=xx\")";
	} else {
		*searchpart++ = 0;
	}

	vf->messages = g_ptr_array_new();
	vf->messages_uid = g_hash_table_new(g_str_hash, g_str_equal);

	vf->expression = g_strdup(searchpart);
	vf->vname = namepart;

	vee_folder_build(vf, ex);
	if (camel_exception_is_set (ex)) {
		camel_object_unref (CAMEL_OBJECT (folder));
		return NULL;
	}

	return folder;
}

static void
folder_changed(CamelFolder *sub, gpointer type, CamelVeeFolder *vf)
{
	CamelException *ex;

	ex = camel_exception_new();
	vee_folder_build_folder(vf, sub, ex);
	camel_exception_free(ex);
	/* FIXME: should only raise follow-on event if the result changed */
	camel_object_trigger_event( CAMEL_OBJECT(vf), "folder_changed", GINT_TO_POINTER(0));
}

/* track flag changes in the summary */
static void
message_changed(CamelFolder *f, const char *uid, CamelVeeFolder *mf)
{
	const CamelMessageInfo *info;
	CamelMessageInfo *vinfo;
	CamelFlag *flag;
	char *vuid;

	info = camel_folder_get_message_info(f, uid);

	vuid = g_strdup_printf("%p:%s", f, uid);
	vinfo = (CamelMessageInfo *)vee_get_message_info((CamelFolder *)mf, vuid);
	if (info && vinfo) {
		vinfo->flags = info->flags;
		camel_flag_list_free(&vinfo->user_flags);
		flag = info->user_flags;
		while (flag) {
			camel_flag_set(&vinfo->user_flags, flag->name, TRUE);
			flag = flag->next;
		}
		camel_object_trigger_event( CAMEL_OBJECT(mf), "message_changed", vinfo->uid);
	}
	g_free(vuid);
}

void
camel_vee_folder_add_folder(CamelVeeFolder *vf, CamelFolder *sub)
{
	struct _CamelVeeFolderPrivate *p = _PRIVATE(vf);
	CamelException *ex;

	camel_object_ref((CamelObject *)sub);
	p->folders = g_list_append(p->folders, sub);

	camel_object_hook_event ((CamelObject *)sub, "folder_changed", (CamelObjectEventHookFunc) folder_changed, vf);
	camel_object_hook_event ((CamelObject *)sub, "message_changed", (CamelObjectEventHookFunc) message_changed, vf);

	ex = camel_exception_new();
	vee_folder_build_folder(vf, sub, ex);
	camel_exception_free(ex);
	/* FIXME: should only raise follow-on event if the result changed */
	camel_object_trigger_event( CAMEL_OBJECT(vf), "folder_changed", GINT_TO_POINTER(0));
}


static void
vee_sync (CamelFolder *folder, gboolean expunge, CamelException *ex)
{
	;
}

static gint vee_get_message_count (CamelFolder *folder)
{
	CamelVeeFolder *vf = (CamelVeeFolder *)folder;

	return vf->messages->len;
}

static gint
vee_get_unread_message_count (CamelFolder *folder)
{
	CamelVeeFolder *vee_folder = CAMEL_VEE_FOLDER (folder);
	CamelMessageInfo *info;
	GPtrArray *infolist;
	gint i, count = 0;

	g_return_val_if_fail (folder != NULL, -1);

	infolist = vee_folder->messages;
	
	for (i = 0; i < infolist->len; i++) {
		info = (CamelMessageInfo *) g_ptr_array_index (infolist, i);
		if (!(info->flags & CAMEL_MESSAGE_SEEN))
			count++;
	}
	
	return count;
}

static gboolean
get_real_message(CamelFolder *folder, const char *uid, CamelFolder **out_folder, const char **out_uid)
{
	CamelVeeMessageInfo *mi;

	mi = (CamelVeeMessageInfo *)vee_get_message_info(folder, uid);
	g_return_val_if_fail(mi != NULL, FALSE);

	*out_folder = mi->folder;
	*out_uid = strchr(mi->info.uid, ':')+1;
	return TRUE;
}

static CamelMimeMessage *vee_get_message(CamelFolder *folder, const gchar *uid, CamelException *ex)
{
	const char *real_uid;
	CamelFolder *real_folder;

	if (!get_real_message(folder, uid, &real_folder, &real_uid)) {
		camel_exception_setv(ex, CAMEL_EXCEPTION_FOLDER_INVALID_UID,
				     "No such message %s in %s", uid,
				     folder->name);
		return NULL;
	}

	return camel_folder_get_message (real_folder, real_uid, ex);
}

GPtrArray *vee_get_summary(CamelFolder *folder)
{
	CamelVeeFolder *vf = (CamelVeeFolder *)folder;

	return vf->messages;
}

static const CamelMessageInfo *vee_get_message_info(CamelFolder *f, const char *uid)
{
	CamelVeeFolder *vf = (CamelVeeFolder *)f;

	return g_hash_table_lookup(vf->messages_uid, uid);
}

static GPtrArray *vee_get_uids (CamelFolder *folder)
{
	GPtrArray *result;
	int i;
	CamelVeeFolder *vf = (CamelVeeFolder *)folder;

	result = g_ptr_array_new ();
	g_ptr_array_set_size (result, vf->messages->len);
	for (i=0;i<vf->messages->len;i++) {
		CamelMessageInfo *mi = g_ptr_array_index(vf->messages, i);
		result->pdata[i] = g_strdup(mi->uid);
	}

	return result;
}

static GPtrArray *
vee_search_by_expression(CamelFolder *folder, const char *expression, CamelException *ex)
{
	GList *node;
	GPtrArray *matches, *result = g_ptr_array_new ();
	char *expr;
	CamelVeeFolder *vf = (CamelVeeFolder *)folder;
	struct _CamelVeeFolderPrivate *p = _PRIVATE(vf);

	expr = g_strdup_printf("(and %s %s)", vf->expression, expression);
	node = p->folders;
	while (node) {
		CamelFolder *f = node->data;
		int i;

		matches = camel_folder_search_by_expression(f, expression, ex);
		for (i = 0; i < matches->len; i++) {
			char *uid = matches->pdata[i];
			g_ptr_array_add(result, g_strdup_printf("%p:%s", f, uid));
		}
		camel_folder_search_free(f, matches);
		node = g_list_next(node);
	}
	return result;
}

static guint32
vee_get_message_flags(CamelFolder *folder, const char *uid)
{
	const char *real_uid;
	CamelFolder *real_folder;

	if (!get_real_message (folder, uid, &real_folder, &real_uid))
		return 0;

	return camel_folder_get_message_flags(real_folder, real_uid);
}

static void
vee_set_message_flags(CamelFolder *folder, const char *uid, guint32 flags, guint32 set)
{
	const char *real_uid;
	CamelFolder *real_folder;

	if (!get_real_message(folder, uid, &real_folder, &real_uid))
		return;

	camel_folder_set_message_flags(real_folder, real_uid, flags, set);
}

static gboolean
vee_get_message_user_flag(CamelFolder *folder, const char *uid, const char *name)
{
	const char *real_uid;
	CamelFolder *real_folder;

	if (!get_real_message(folder, uid, &real_folder, &real_uid))
		return FALSE;

	return camel_folder_get_message_user_flag(real_folder, real_uid, name);
}

static void
vee_set_message_user_flag(CamelFolder *folder, const char *uid,
			  const char *name, gboolean value)
{
	const char *real_uid;
	CamelFolder *real_folder;

	if (!get_real_message(folder, uid, &real_folder, &real_uid))
		return;

	return camel_folder_set_message_user_flag(real_folder, real_uid, name, value);
}


/*
  need incremental update, based on folder.
  Need to watch folders for changes and update accordingly.
*/

/* this does most of the vfolder magic */
static void
vee_folder_build(CamelVeeFolder *vf, CamelException *ex)
{
	struct _CamelVeeFolderPrivate *p = _PRIVATE(vf);
	GList *node;
	int i;
	GPtrArray *messages;
	GHashTable *messages_uid;

	for (i=0;i<vf->messages->len;i++) {
		CamelMessageInfo *mi = g_ptr_array_index(vf->messages, i);
		camel_message_info_free(mi);
	}

	messages = g_ptr_array_new();
	messages_uid = g_hash_table_new(g_str_hash, g_str_equal);

	node = p->folders;
	while (node) {
		GPtrArray *matches;
		CamelFolder *f = node->data;
		CamelVeeMessageInfo *mi;
		const CamelMessageInfo *info;
		int i;

		matches = camel_folder_search_by_expression(f, vf->expression, ex);
		for (i = 0; i < matches->len; i++) {
			info = camel_folder_get_message_info(f, matches->pdata[i]);
			if (info) {
				mi = g_malloc0(sizeof(*mi));
				camel_message_info_dup_to(info, (CamelMessageInfo *)mi);
				g_free (mi->info.uid);
				mi->info.uid = g_strdup_printf("%p:%s", f, info->uid);
				mi->folder = f;
				g_ptr_array_add(messages, mi);
				g_hash_table_insert(messages_uid, mi->info.uid, mi);
			}
		}
		camel_folder_search_free(f, matches);
		node = g_list_next(node);
	}

	g_ptr_array_free(vf->messages, TRUE);
	vf->messages = messages;
	g_hash_table_destroy(vf->messages_uid);
	vf->messages_uid = messages_uid;
}


/* build query contents for a single folder */
static void
vee_folder_build_folder(CamelVeeFolder *vf, CamelFolder *source, CamelException *ex)
{
	GPtrArray *matches;
	CamelFolder *f = source;
	CamelVeeMessageInfo *mi;
	const CamelMessageInfo *info;

	GPtrArray *messages;
	GHashTable *messages_uid;
	int i;

	for (i=0;i<vf->messages->len;i++) {
		CamelVeeMessageInfo *mi = g_ptr_array_index(vf->messages, i);
		if (mi->folder == source) {
			g_hash_table_remove(vf->messages_uid, mi->info.uid);
			g_ptr_array_remove_index_fast(vf->messages, i);

			camel_message_info_free((CamelMessageInfo *)mi);
			i--;
		}
	}

	messages = vf->messages;
	messages_uid = vf->messages_uid;
	
	matches = camel_folder_search_by_expression(f, vf->expression, ex);
	for (i = 0; i < matches->len; i++) {
		info = camel_folder_get_message_info(f, matches->pdata[i]);
		if (info) {
			mi = g_malloc0(sizeof(*mi));
			camel_message_info_dup_to(info, (CamelMessageInfo*)mi);
			g_free (mi->info.uid);
			mi->info.uid = g_strdup_printf("%p:%s", f, info->uid);
			mi->folder = f;
			g_ptr_array_add(messages, mi);
			g_hash_table_insert(messages_uid, mi->info.uid, mi);
		}
	}
	camel_folder_search_free(f, matches);
}


/*

  (match-folder "folder1" "folder2")

 */
