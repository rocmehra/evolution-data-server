/*
 *  Copyright (C) 2000 Ximian Inc.
 *
 *  Authors: Michael Zucchi <notzed@ximian.com>
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

#ifndef _CAMEL_VEE_FOLDER_H
#define _CAMEL_VEE_FOLDER_H

#include <glib.h>
#include <camel/camel-folder.h>

#define CAMEL_VEE_FOLDER(obj)         CAMEL_CHECK_CAST (obj, camel_vee_folder_get_type (), CamelVeeFolder)
#define CAMEL_VEE_FOLDER_CLASS(klass) CAMEL_CHECK_CLASS_CAST (klass, camel_vee_folder_get_type (), CamelVeeFolderClass)
#define CAMEL_IS_VEE_FOLDER(obj)      CAMEL_CHECK_TYPE (obj, camel_vee_folder_get_type ())

typedef struct _CamelVeeFolder      CamelVeeFolder;
typedef struct _CamelVeeFolderClass CamelVeeFolderClass;

/* our message info includes the parent folder */
typedef struct _CamelVeeMessageInfo {
	CamelMessageInfo info;
	CamelFolder *folder;
} CamelVeeMessageInfo;

struct _CamelVeeFolder {
	CamelFolder parent;

	struct _CamelVeeFolderPrivate *priv;

	char *expression;	/* query expression */
	char *vname;		/* local name */

	guint32 flags;		/* folder open flags */

	CamelFolderChangeInfo *changes;
	CamelFolderSearch *search;
};

struct _CamelVeeFolderClass {
	CamelFolderClass parent_class;
};

guint	      camel_vee_folder_get_type		(void);
CamelFolder  *camel_vee_folder_new		(CamelStore *parent_store, const char *name, guint32 flags);
void         camel_vee_folder_construct		(CamelVeeFolder *vf, CamelStore *parent_store, const char *name, guint32 flags);

void         camel_vee_folder_add_folder        (CamelVeeFolder *vf, CamelFolder *sub);
void         camel_vee_folder_remove_folder     (CamelVeeFolder *vf, CamelFolder *sub);
void	     camel_vee_folder_set_expression	(CamelVeeFolder *vf, const char *expr);

void	     camel_vee_folder_hash_folder	(CamelFolder *folder, char buffer[8]);

#endif /* ! _CAMEL_VEE_FOLDER_H */
