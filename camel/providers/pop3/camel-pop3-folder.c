/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-pop3-folder.c : class for a pop3 folder */

/* 
 * Authors:
 *   Dan Winship <danw@helixcode.com>
 *
 * Copyright (C) 2000 Helix Code, Inc. (www.helixcode.com)
 *
 * This program is free software; you can redistribute it and/or 
 * modify it under the terms of the GNU General Public License as 
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
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

#include "camel-pop3-folder.h"
#include "camel-pop3-store.h"
#include "camel-exception.h"
#include "camel-stream-mem.h"
#include "camel-mime-message.h"

#include <stdlib.h>

#define CF_CLASS(o) (CAMEL_FOLDER_CLASS (GTK_OBJECT (o)->klass))
static CamelFolderClass *parent_class;

static void pop3_open (CamelFolder *folder, CamelFolderOpenMode mode,
		       CamelException *ex);
static void pop3_close (CamelFolder *folder, gboolean expunge,
			CamelException *ex);
static gboolean delete_messages (CamelFolder *folder, CamelException *ex);
static gboolean has_message_number_capability (CamelFolder *folder);
static CamelMimeMessage *get_message_by_number (CamelFolder *folder, 
						gint number, 
						CamelException *ex);
static void delete_message_by_number (CamelFolder *folder, gint number, 
				      CamelException *ex);
static gint get_message_count (CamelFolder *folder, CamelException *ex);


static void
camel_pop3_folder_class_init (CamelPop3FolderClass *camel_pop3_folder_class)
{
	CamelFolderClass *camel_folder_class =
		CAMEL_FOLDER_CLASS (camel_pop3_folder_class);

	parent_class = gtk_type_class (camel_folder_get_type ());

	/* virtual method overload */
	camel_folder_class->open = pop3_open;
	camel_folder_class->close = pop3_close;
	camel_folder_class->delete_messages = delete_messages;
	camel_folder_class->has_message_number_capability =
		has_message_number_capability;
	camel_folder_class->get_message_by_number =
		get_message_by_number;
	camel_folder_class->delete_message_by_number =
		delete_message_by_number;
	camel_folder_class->get_message_count =
		get_message_count;
}



static void
camel_pop3_folder_init (gpointer object, gpointer klass)
{
	CamelFolder *folder = CAMEL_FOLDER (object);

	folder->can_hold_messages = TRUE;
	folder->can_hold_folders = FALSE;

	/* Hi. I'm CamelPop3Folder. I'm useless. */
	folder->has_summary_capability = FALSE;
	folder->has_uid_capability = FALSE;
	folder->has_search_capability = FALSE;
}




GtkType
camel_pop3_folder_get_type (void)
{
	static GtkType camel_pop3_folder_type = 0;

	if (!camel_pop3_folder_type) {
		GtkTypeInfo camel_pop3_folder_info =	
		{
			"CamelPop3Folder",
			sizeof (CamelPop3Folder),
			sizeof (CamelPop3FolderClass),
			(GtkClassInitFunc) camel_pop3_folder_class_init,
			(GtkObjectInitFunc) camel_pop3_folder_init,
				/* reserved_1 */ NULL,
				/* reserved_2 */ NULL,
			(GtkClassInitFunc) NULL,
		};

		camel_pop3_folder_type = gtk_type_unique (CAMEL_FOLDER_TYPE, &camel_pop3_folder_info);
	}

	return camel_pop3_folder_type;
}


CamelFolder *camel_pop3_folder_new (CamelStore *parent, CamelException *ex)
{
	CamelFolder *folder =
		CAMEL_FOLDER (gtk_object_new (camel_pop3_folder_get_type (),
					      NULL));

	CF_CLASS (folder)->init (folder, parent, NULL, "inbox", '/', ex);
	return folder;
}

static void
pop3_open (CamelFolder *folder, CamelFolderOpenMode mode, CamelException *ex)
{
	camel_pop3_store_open (CAMEL_POP3_STORE (folder->parent_store), ex);
	if (camel_exception_get_id (ex) == CAMEL_EXCEPTION_NONE)
		parent_class->open (folder, mode, ex);
}

static void
pop3_close (CamelFolder *folder, gboolean expunge, CamelException *ex)
{
	camel_pop3_store_close (CAMEL_POP3_STORE (folder->parent_store),
				expunge, ex);
	if (camel_exception_get_id (ex) == CAMEL_EXCEPTION_NONE)
		parent_class->close (folder, expunge, ex);
}
				
static gboolean
delete_messages (CamelFolder *folder, CamelException *ex)
{
	int msgs;
	gboolean status;

	msgs = get_message_count (folder, ex);
	if (camel_exception_get_id (ex) != CAMEL_EXCEPTION_NONE)
		return FALSE;

	status = TRUE;
	for (; msgs > 0; msgs--) {
		status = status &&
			(camel_pop3_command (CAMEL_POP3_STORE (folder->parent_store),
					     NULL, "DELE %d", msgs) ==
			 CAMEL_POP3_OK);
	}

	if (!status) {
		camel_exception_setv (ex, CAMEL_EXCEPTION_SERVICE_UNAVAILABLE,
				      "Unable to delete all messages.");
	}

	return status;
}


static gboolean
has_message_number_capability (CamelFolder *folder)
{
	return TRUE;
}

static CamelMimeMessage *
get_message_by_number (CamelFolder *folder, gint number, CamelException *ex)
{
	int status;
	char *result, *body;
	CamelStream *msgstream;
	CamelMimeMessage *msg;

	status = camel_pop3_command (CAMEL_POP3_STORE (folder->parent_store),
				     &result, "RETR %d", number);
	if (status != CAMEL_POP3_OK) {
		CamelService *service = CAMEL_SERVICE (folder->parent_store);
		camel_exception_setv (ex, CAMEL_EXCEPTION_SERVICE_UNAVAILABLE,
				      "Could not retrieve message from POP "
				      "server %s: %s.", service->url->host,
				      status == CAMEL_POP3_ERR ? result :
				      "Unknown error");
		g_free (result);
		return NULL;
	}
	g_free (result);

	body = camel_pop3_command_get_additional_data (CAMEL_POP3_STORE (folder->parent_store));
	if (!body) {
		CamelService *service = CAMEL_SERVICE (folder->parent_store);
		camel_exception_setv (ex, CAMEL_EXCEPTION_SERVICE_UNAVAILABLE,
				      "Could not retrieve message from POP "
				      "server %s.", service->url->host);
		return NULL;
	}

	msgstream = camel_stream_mem_new_with_buffer (body, strlen (body),
						      CAMEL_STREAM_MEM_READ);
	msg = camel_mime_message_new_with_session (camel_service_get_session (CAMEL_SERVICE (folder->parent_store)));
	camel_data_wrapper_set_input_stream (CAMEL_DATA_WRAPPER (msg),
					     msgstream);

	return msg;
}

static void
delete_message_by_number (CamelFolder *folder, gint number, CamelException *ex)
{
	int status;
	char *resp;

	status = camel_pop3_command (CAMEL_POP3_STORE (folder->parent_store),
				     &resp, "DELE %d", number);
	if (status != CAMEL_POP3_OK) {
		camel_exception_setv (ex, CAMEL_EXCEPTION_FOLDER_INVALID_UID,
				      "Unable to delete message %d%s%s",
				      number, resp ? ": " : "",
				      resp ? resp : "");
	}
	g_free (resp);
}

static gint
get_message_count (CamelFolder *folder, CamelException *ex)
{
	int status, count;
	char *result;

	status = camel_pop3_command (CAMEL_POP3_STORE (folder->parent_store),
				     &result, "STAT");
	if (status != CAMEL_POP3_OK) {
		CamelService *service = CAMEL_SERVICE (folder->parent_store);
		camel_exception_setv (ex, CAMEL_EXCEPTION_SERVICE_UNAVAILABLE,
				      "Could not get message count from POP "
				      "server %s: %s.", service->url->host,
				      status == CAMEL_POP3_ERR ? result :
				      "Unknown error");
		g_free (result);
		return -1;
	}

	count = atoi (result);
	g_free (result);
	return count;
}
