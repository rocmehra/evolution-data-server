/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-mime-part-utils : Utility for mime parsing and so on */


/* 
 *
 * Copyright (C) 1999 Bertrand Guiheneuf <Bertrand.Guiheneuf@aful.org> .
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

#ifndef CAMEL_MIME_PART_UTILS_H
#define CAMEL_MIME_PART_UTILS_H 1

#ifdef __cplusplus
extern "C" {
#pragma }
#endif /* __cplusplus }*/

#include "camel-mime-part.h"


void camel_mime_part_construct_headers_from_stream (CamelMimePart *mime_part, 
						    CamelStream *stream);

void camel_mime_part_construct_content_from_stream (CamelMimePart *mime_part, 
						    CamelStream *stream);

void camel_mime_part_store_stream_in_buffer (CamelMimePart *mime_part, 
					     CamelStream *stream);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /*  CAMEL_MIME_PART_UTILS_H  */

