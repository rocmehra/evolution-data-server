/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-pop3-folder.h : Class for a POP3 folder */

/* 
 * Author:
 *   Dan Winship <danw@ximian.com>
 *
 * Copyright (C) 2000 Ximian, Inc. (www.ximian.com)
 *
 * This program is free software; you can redistribute it and/or 
 * modify it under the terms of version 2 of the GNU General Public 
 * published by the Free Software Foundation; either version 2 of the
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


#ifndef CAMEL_POP3_FOLDER_H
#define CAMEL_POP3_FOLDER_H 1


#ifdef __cplusplus
extern "C" {
#pragma }
#endif /* __cplusplus }*/

#include "camel-folder.h"

#define CAMEL_POP3_FOLDER_TYPE     (camel_pop3_folder_get_type ())
#define CAMEL_POP3_FOLDER(obj)     (CAMEL_CHECK_CAST((obj), CAMEL_POP3_FOLDER_TYPE, CamelPop3Folder))
#define CAMEL_POP3_FOLDER_CLASS(k) (CAMEL_CHECK_CLASS_CAST ((k), CAMEL_POP3_FOLDER_TYPE, CamelPop3FolderClass))
#define CAMEL_IS_POP3_FOLDER(o)    (CAMEL_CHECK_TYPE((o), CAMEL_POP3_FOLDER_TYPE))


typedef struct {
	CamelFolder parent_object;

	GPtrArray *uids;
	guint32 *flags;

} CamelPop3Folder;



typedef struct {
	CamelFolderClass parent_class;

	/* Virtual methods */	
	
} CamelPop3FolderClass;


/* public methods */
CamelFolder *camel_pop3_folder_new (CamelStore *parent, CamelException *ex);

/* Standard Camel function */
CamelType camel_pop3_folder_get_type (void);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* CAMEL_POP3_FOLDER_H */
