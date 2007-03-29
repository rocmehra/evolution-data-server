/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* 
 * Copyright (C) 2005 Novell, Inc.
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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include <config.h>

#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#ifndef O_BINARY
#define O_BINARY 0
#endif

#include <libxml/parser.h>
#include <libxml/tree.h>

#include <glib.h>
#include <glib/gstdio.h>

#include "e-xml-utils.h"

#ifdef G_OS_WIN32
#define fsync(fd) 0
#endif

xmlDocPtr
e_xml_parse_file (const char *filename)
{
	xmlDocPtr result = NULL;

	GMappedFile *mapped_file;

	mapped_file = g_mapped_file_new (filename, FALSE, NULL);
	if (mapped_file) {
		result = xmlParseMemory (g_mapped_file_get_contents (mapped_file),
					 g_mapped_file_get_length (mapped_file));
		g_mapped_file_free (mapped_file);
	}
	return result;
}

int
e_xml_save_file (const char *filename, xmlDocPtr doc)
{
	char *filesave, *xmlbuf;
	size_t n, written = 0;
	int ret, fd, size;
	int errnosave;
	ssize_t w;
	gchar *dirname = g_path_get_dirname (filename);
	gchar *basename = g_path_get_basename (filename);
	gchar *savebasename = g_strconcat (".#", basename, NULL);

	g_free (basename);
	filesave = g_build_filename (dirname, savebasename, NULL);
	g_free (savebasename);
	g_free (dirname);
	
	fd = g_open (filesave, O_WRONLY | O_CREAT | O_TRUNC | O_BINARY, 0600);
	if (fd == -1) {
		g_free (filesave);
		return -1;
	}
	
	xmlDocDumpFormatMemory (doc, (xmlChar **) &xmlbuf, &size, TRUE);
	if (size <= 0) {
		close (fd);
		g_unlink (filesave);
		g_free (filesave);
		errno = ENOMEM;
		return -1;
	}
	
	n = (size_t) size;
	do {
		do {
			w = write (fd, xmlbuf + written, n - written);
		} while (w == -1 && errno == EINTR);
		
		if (w > 0)
			written += w;
	} while (w != -1 && written < n);
	
	xmlFree (xmlbuf);
	
	if (written < n || fsync (fd) == -1) {
		errnosave = errno;
		close (fd);
		g_unlink (filesave);
		g_free (filesave);
		errno = errnosave;
		return -1;
	}
	
	while ((ret = close (fd)) == -1 && errno == EINTR)
		;
	
	if (ret == -1) {
		g_free (filesave);
		return -1;
	}
	
	if (g_rename (filesave, filename) == -1) {
		errnosave = errno;
		g_unlink (filesave);
		g_free (filesave);
		errno = errnosave;
		return -1;
	}
	g_free (filesave);
	
	return 0;
}

xmlNode *
e_xml_get_child_by_name (const xmlNode *parent, const xmlChar *child_name)
{
	xmlNode *child;

	g_return_val_if_fail (parent != NULL, NULL);
	g_return_val_if_fail (child_name != NULL, NULL);
	
	for (child = parent->xmlChildrenNode; child != NULL; child = child->next) {
		if (xmlStrcmp (child->name, child_name) == 0) {
			return child;
		}
	}
	return NULL;
}


