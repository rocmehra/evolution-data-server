/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */


/* 
 *
 * Author : 
 *  Bertrand Guiheneuf <bertrand@helixcode.com>
 *
 * Copyright 1999, 2000 HelixCode (http://www.helixcode.com) .
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
#include "camel-stream-b64.h"




static CamelStreamClass *parent_class = NULL;

static guchar char_to_six_bits [256] = {
	66, 66, 66, 66, 66, 66, 66, 66, /*   0 .. 7   */
	66, 66, 66, 66, 66, 66, 66, 66, /*   8 .. 15  */
	66, 66, 66, 66, 66, 66, 66, 66, /*  16 .. 23  */
	66, 66, 66, 66, 66, 66, 66, 66, /*  24 .. 31  */
	66, 66, 66, 66, 66, 66, 66, 66, /*  32 .. 39  */
	66, 66, 66, 62, 66, 66, 66, 63, /*  40 .. 47  */
	52, 53, 54, 55, 56, 57, 58, 59, /*  48 .. 55  */
	60, 61, 66, 66, 66, 65, 66, 66, /*  56 .. 63  */
	66,  0,  1,  2,  3,  4,  5,  6, /*  64 .. 71  */
	 7,  8,  9, 10, 11, 12, 13, 14, /*  72 .. 79  */
	15, 16, 17, 18, 19, 20, 21, 22, /*  80 .. 87  */
	23, 24, 25, 66, 66, 66, 66, 66, /*  88 .. 95  */
	66, 26, 27, 28, 29, 30, 31, 32, /*  96 .. 103 */
	33, 34, 35, 36, 37, 38, 39, 40, /* 104 .. 111 */
	41, 42, 43, 44, 45, 46, 47, 48, /* 112 .. 119 */
	49, 50, 51, 66, 66, 66, 66, 66, /* 120 .. 127 */
	66, 66, 66, 66, 66, 66, 66, 66, /* 128 .. 135 */
	66, 66, 66, 66, 66, 66, 66, 66, /* 136 .. 143 */
	66, 66, 66, 66, 66, 66, 66, 66, /* 144 .. 151 */
	66, 66, 66, 66, 66, 66, 66, 66, /* 152 .. 159 */
	66, 66, 66, 66, 66, 66, 66, 66, /* 160 .. 167 */
	66, 66, 66, 66, 66, 66, 66, 66, /* 168 .. 175 */
	66, 66, 66, 66, 66, 66, 66, 66, /* 176 .. 183 */
	66, 66, 66, 66, 66, 66, 66, 66, /* 184 .. 191 */
	66, 66, 66, 66, 66, 66, 66, 66, /* 192 .. 199 */
	66, 66, 66, 66, 66, 66, 66, 66, /* 200 .. 207 */
	66, 66, 66, 66, 66, 66, 66, 66, /* 208 .. 215 */
	66, 66, 66, 66, 66, 66, 66, 66, /* 216 .. 223 */
	66, 66, 66, 66, 66, 66, 66, 66, /* 224 .. 231 */
	66, 66, 66, 66, 66, 66, 66, 66, /* 232 .. 239 */
	66, 66, 66, 66, 66, 66, 66, 66, /* 240 .. 247 */
	66, 66, 66, 66, 66, 66, 66, 66  /* 248 .. 255 */
};


static gchar six_bits_to_char[65] = 
"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/=";

/* Returns the class for a CamelStreamB64 */
#define CSB64_CLASS(so) CAMEL_STREAM_B64_CLASS (GTK_OBJECT(so)->klass)

static void           init_with_input_stream__static         (CamelStreamB64 *stream_b64, 
							       CamelStream *input_stream);

static gint           read__static                           (CamelStream *stream, 
							       gchar *buffer, 
							       gint n);

static void           reset__static                          (CamelStream *stream);

static gint           read_decode__static                    (CamelStream *stream, 
							       gchar *buffer, 
							       gint n);
static gboolean       eos__static                            (CamelStream *stream);

static void
camel_stream_b64_class_init (CamelStreamB64Class *camel_stream_b64_class)
{
	CamelStreamClass *camel_stream_class = CAMEL_STREAM_CLASS (camel_stream_b64_class);


	parent_class = gtk_type_class (camel_stream_get_type ());

	/* virtual method definition */
	camel_stream_b64_class->init_with_input_stream = init_with_input_stream__static;


	/* virtual method overload */
	camel_stream_class->read     = read__static;
	camel_stream_class->eos      = eos__static; 
	camel_stream_class->reset    = reset__static; 

	/* signal definition */
	
}

GtkType
camel_stream_b64_get_type (void)
{
	static GtkType camel_stream_b64_type = 0;
	
	if (!camel_stream_b64_type)	{
		GtkTypeInfo camel_stream_b64_info =	
		{
			"CamelStreamB64",
			sizeof (CamelStreamB64),
			sizeof (CamelStreamB64Class),
			(GtkClassInitFunc) camel_stream_b64_class_init,
			(GtkObjectInitFunc) NULL,
				/* reserved_1 */ NULL,
				/* reserved_2 */ NULL,
			(GtkClassInitFunc) NULL,
		};
		
		camel_stream_b64_type = gtk_type_unique (camel_stream_get_type (), &camel_stream_b64_info);
	}
	
	return camel_stream_b64_type;
}


static void
reemit_available_signal__static (CamelStream *parent_stream, gpointer user_data)
{
	gtk_signal_emit_by_name (GTK_OBJECT (user_data), "data_available");
}

static void           
init_with_input_stream__static (CamelStreamB64 *stream_b64, 
				 CamelStream *input_stream)
{
	g_assert (stream_b64);
	g_assert (input_stream);



	/* by default, the stream is in decode mode */	
	stream_b64->mode = CAMEL_STREAM_B64_DECODER;

	stream_b64->eos = FALSE;
	stream_b64->decode_status.keep = 0;
	stream_b64->decode_status.state = 0;
	

	stream_b64->input_stream = input_stream;
	
	gtk_object_ref (GTK_OBJECT (input_stream));
	
	/* 
	 *  connect to the parent stream "data_available"
	 *  stream so that we can reemit the signal on the 
	 *  seekable substream in case some data would 
	 *  be available for us 
	 */
	gtk_signal_connect (GTK_OBJECT (input_stream),
			    "data_available", 
			    reemit_available_signal__static,
			    stream_b64);
	
	
	/* bootstrapping signal */
	gtk_signal_emit_by_name (GTK_OBJECT (stream_b64), "data_available");


}



CamelStream *
camel_stream_b64_new_with_input_stream (CamelStream *input_stream)
{
	CamelStreamB64 *stream_b64;
	
	stream_b64 = gtk_type_new (camel_stream_b64_get_type ());
	CSB64_CLASS (stream_b64)->init_with_input_stream (stream_b64, input_stream);

	return CAMEL_STREAM (stream_b64);
}




void       
camel_stream_b64_set_mode (CamelStreamB64 *stream_b64,
			   CamelStreamB64Mode mode)
{
	g_assert (stream_b64);
	stream_b64->mode = mode;
}




static gint 
read__static (CamelStream *stream, 
	      gchar *buffer, 
	      gint n)
{
	CamelStreamB64 *stream_b64 = CAMEL_STREAM_B64 (stream);
	
	g_assert (stream);
        

	if (stream_b64->mode == CAMEL_STREAM_B64_DECODER)
		return read_decode__static (stream, buffer, n);
	
	return 0;
}



static gint read_decode__static (CamelStream *stream, 
				  gchar *buffer, 
				  gint n)
{
	CamelStreamB64 *stream_b64 = CAMEL_STREAM_B64 (stream);
	CamelStream64DecodeStatus *status;
	CamelStream    *input_stream;
	guchar six_bits_value;
	gint nb_read_in_input;
	guchar c;
	gint j = 0;
	
	g_assert (stream);
	input_stream = stream_b64->input_stream;

	g_assert (input_stream);
	status = &(stream_b64->decode_status);
	
	
	/* state = (CamelStream64DecodeState *)
	   ((gchar *)stream_b64  + G_STRUCT_OFFSET (CamelStreamB64, decode_state)) */
	
	nb_read_in_input = camel_stream_read (input_stream, &c, 1);
	
	while ((nb_read_in_input >0 ) && (j<n)) {
		
		six_bits_value = char_to_six_bits[c];
		
		/* if we encounter an '=' we can assume the end of the stream 
		   has been found */
		if (six_bits_value == 65) {
			stream_b64->eos = TRUE;
			break;
		}
		
		/* test if we must ignore the character */
		if (six_bits_value != 66) {
			
			switch (status->state % 4){
			case 0:
				status->keep =  six_bits_value << 2;
				break;
			case 1:
				buffer [j++] = status->keep | (six_bits_value >> 4);
				status->keep = (six_bits_value & 0xf) << 4;
				break;
			case 2:
				buffer [j++] = status->keep | (six_bits_value >> 2);
				status->keep = (six_bits_value & 0x3) << 6;
				break;
			case 3:
				buffer [j++] = status->keep | six_bits_value;
				break;
			}
			
			status->state = (status->state + 1) % 4;
		}
		
		
		nb_read_in_input = camel_stream_read (input_stream, &c, 1);
		
	}
	
	return j;
	
}






	
static gboolean
eos__static (CamelStream *stream)
{
	CamelStreamB64 *stream_b64 = CAMEL_STREAM_B64 (stream);
	
	g_assert (stream);
	g_assert (stream_b64->input_stream);

	return (stream_b64->eos || camel_stream_eos (stream_b64->input_stream));
}





static void 
reset__static (CamelStream *stream)
{
	CamelStreamB64 *stream_b64 = CAMEL_STREAM_B64 (stream);
	
	g_assert (stream);
	g_assert (stream_b64->input_stream);

	stream_b64->decode_status.keep = 0;
	stream_b64->decode_status.state = 0;
	
	camel_stream_reset (stream_b64->input_stream);
}
