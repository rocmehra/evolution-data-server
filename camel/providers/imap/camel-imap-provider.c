/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-imap-provider.c: imap provider registration code */

/*
 *  Authors: Jeffrey Stedfast <fejj@helixcode.com>
 *
 *  Copyright 2000 Helix Code, Inc. (www.helixcode.com)
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


#include "config.h"
#include "camel-imap-store.h"
#include "camel-provider.h"
#include "camel-session.h"

static CamelProvider imap_provider = {
	"imap",
	"IMAPv4",

	"For reading and storing mail on IMAP servers.",

	"mail",

	0,

	{ 0, 0 }
};

void
camel_provider_module_init (CamelSession *session)
{
	imap_provider.object_types[CAMEL_PROVIDER_STORE] =
		camel_imap_store_get_type();

	camel_session_register_provider (session, &imap_provider);
}


