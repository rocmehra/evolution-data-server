/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  Authors: Jeffrey Stedfast <fejj@novell.com>
 *
 *  Copyright 2004 Novell, Inc. (www.novell.com)
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Street #330, Boston, MA 02111-1307, USA.
 *
 */


#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <ctype.h>
#include <errno.h>

#include <camel/camel-i18n.h>
#include <camel/camel-folder.h>
#include <camel/camel-file-utils.h>
#include <camel/camel-folder-summary.h>
#include <camel/camel-data-cache.h>

#include "camel-offline-journal.h"


#define d(x) x


static void camel_offline_journal_class_init (CamelOfflineJournalClass *klass);
static void camel_offline_journal_init (CamelOfflineJournal *journal, CamelOfflineJournalClass *klass);
static void camel_offline_journal_finalize (CamelObject *object);


static CamelObjectClass *parent_class = NULL;


CamelType
camel_offline_journal_get_type (void)
{
	static CamelType type = 0;
	
	if (!type) {
		type = camel_type_register (camel_object_get_type (),
					    "CamelOfflineJournal",
					    sizeof (CamelOfflineJournal),
					    sizeof (CamelOfflineJournalClass),
					    (CamelObjectClassInitFunc) camel_offline_journal_class_init,
					    NULL,
					    (CamelObjectInitFunc) camel_offline_journal_init,
					    (CamelObjectFinalizeFunc) camel_offline_journal_finalize);
	}
	
	return type;
}

static void
camel_offline_journal_class_init (CamelOfflineJournalClass *klass)
{
	parent_class = camel_type_get_global_classfuncs (CAMEL_OBJECT_TYPE);
}

static void
camel_offline_journal_init (CamelOfflineJournal *journal, CamelOfflineJournalClass *klass)
{
	journal->folder = NULL;
	journal->filename = NULL;
	e_dlist_init (&journal->queue);
}

static void
camel_offline_journal_finalize (CamelObject *object)
{
	CamelOfflineJournal *journal = (CamelOfflineJournal *) object;
	EDListNode *entry;
	
	g_free (journal->filename);
	
	while ((entry = e_dlist_remhead (&journal->queue)))
		CAMEL_OFFLINE_JOURNAL_CLASS (journal)->entry_free (journal, entry);
}


void
camel_offline_journal_construct (CamelOfflineJournal *journal, CamelFolder *folder, const char *filename)
{
	EDListNode *entry;
	FILE *fp;
	
	journal->filename = g_strdup (filename);
	journal->folder = folder;
	
	if ((fp = fopen (filename, "r"))) {
		while ((entry = CAMEL_OFFLINE_JOURNAL_CLASS (journal)->entry_load (journal, fp)))
			e_dlist_addtail (&journal->queue, entry);
		
		fclose (fp);
	}
}


void
camel_offline_journal_set_filename (CamelOfflineJournal *journal, const char *filename)
{
	g_return_if_fail (CAMEL_IS_OFFLINE_JOURNAL (journal));
	
	g_free (journal->filename);
	journal->filename = g_strdup (filename);
}


int
camel_offline_journal_write (CamelOfflineJournal *journal, CamelException *ex)
{
	EDListNode *entry;
	FILE *fp;
	int fd;
	
	if ((fd = open (journal->filename, O_CREAT | O_TRUNC | O_WRONLY, 0666)) == -1) {
		camel_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM,
				      _("Cannot write offline journal for folder `%s': %s"),
				      journal->folder->full_name, g_strerror (errno));
		return -1;
	}
	
	fp = fdopen (fd, "w");
	entry = journal->queue.head;
	while (entry->next) {
		if (CAMEL_OFFLINE_JOURNAL_CLASS (journal)->entry_write (journal, entry, fp) == -1)
			goto exception;
		entry = entry->next;
	}
	
	if (fsync (fd) == -1)
		goto exception;
	
	fclose (fp);
	
	return 0;
	
 exception:
	
	camel_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM,
			      _("Cannot write offline journal for folder `%s': %s"),
			      journal->folder->full_name, g_strerror (errno));
	
	fclose (fp);
	
	return -1;
}


int
camel_offline_journal_replay (CamelOfflineJournal *journal, CamelException *ex)
{
	EDListNode *entry, *next;
	CamelException lex;
	int failed = 0;
	
	camel_exception_init (&lex);
	
	entry = journal->queue.head;
	while (entry->next) {
		next = entry->next;
		if (CAMEL_OFFLINE_JOURNAL_CLASS (journal)->entry_play (journal, entry, &lex) == -1) {
			if (failed == 0)
				camel_exception_xfer (ex, &lex);
			camel_exception_clear (&lex);
			failed++;
		} else {
			e_dlist_remove (entry);
		}
		entry = next;
	}
	
	if (failed > 0)
		return -1;
	
	return 0;
}