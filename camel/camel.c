/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/* 
 *
 * Author : 
 *  Bertrand Guiheneuf <bertrand@helixcode.com>
 *
 * Copyright 1999, 2000 Helix Code, Inc. (http://www.helixcode.com)
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

#include <config.h>
#include "camel.h"
#include <unicode.h>
#ifdef HAVE_NSS
#include <nspr.h>
#include <prthread.h>
#include <nss.h>
#include <ssl.h>
#endif /* HAVE_NSS */

gboolean camel_verbose_debug = FALSE;

gint
camel_init (const char *certdb_dir, gboolean nss_init)
{
#ifdef ENABLE_THREADS
#ifdef G_THREADS_ENABLED	
	/*g_thread_init (NULL);*/
#else /* G_THREADS_ENABLED */
	g_warning ("Threads are not supported by your version of glib");
#endif /* G_THREADS_ENABLED */
#endif /* ENABLE_THREADS */
	
	if (getenv ("CAMEL_VERBOSE_DEBUG"))
		camel_verbose_debug = TRUE;
	
	unicode_init ();
	
#ifdef HAVE_NSS
	if (nss_init) {
		PR_Init (PR_SYSTEM_THREAD, PR_PRIORITY_NORMAL, 10);
		
		if (NSS_Init (certdb_dir) == SECFailure) {
			g_warning ("Failed to initialize NSS");
			return -1;
		}
		
		NSS_SetDomesticPolicy ();
	}
	
	SSL_OptionSetDefault (SSL_ENABLE_SSL2, PR_TRUE);
	SSL_OptionSetDefault (SSL_ENABLE_SSL3, PR_TRUE);
	SSL_OptionSetDefault (SSL_ENABLE_TLS, PR_TRUE);
	SSL_OptionSetDefault (SSL_V2_COMPATIBLE_HELLO, PR_TRUE /* maybe? */);
#endif /* HAVE_NSS */
	
	return 0;
}

void
camel_shutdown (void)
{
#ifdef HAVE_NSS
	NSS_Shutdown ();
	
	PR_Cleanup ();
#endif /* HAVE_NSS */
}
