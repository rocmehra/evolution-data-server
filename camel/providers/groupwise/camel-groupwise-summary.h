/*
 *  Copyright (C) 2000 Novell Inc.
 *
 *  Authors:
 *	parthasarathi susarla <sparthasarathi@novell.com>
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
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifndef _CAMEL_GW_SUMMARY_H
#define _CAMEL_GW_SUMMARY_H

#include <camel/camel-folder-summary.h>
#include <camel/camel-exception.h>
#include <camel/camel-store.h>

#define CAMEL_GROUPWISE_SUMMARY(obj)         CAMEL_CHECK_CAST (obj, camel_groupwise_summary_get_type (), CamelGroupwiseSummary)
#define CAMEL_GROUPWISE_SUMMARY_CLASS(klass) CAMEL_CHECK_CLASS_CAST (klass, camel_groupwise_summary_get_type (), CamelGroupwiseSummaryClass)
#define CAMEL_IS_GROUPWISE_SUMMARY(obj)      CAMEL_CHECK_TYPE (obj, camel_groupwise_summary_get_type ())

G_BEGIN_DECLS

typedef struct _CamelGroupwiseSummary CamelGroupwiseSummary ;
typedef struct _CamelGroupwiseSummaryClass CamelGroupwiseSummaryClass ;
typedef struct _CamelGroupwiseMessageInfo CamelGroupwiseMessageInfo ;
typedef struct _CamelGroupwiseMessageContentInfo CamelGroupwiseMessageContentInfo ;

/* extra summary flags*/
enum {
	CAMEL_GW_MESSAGE_JUNK = 1<<17,
	CAMEL_GW_MESSAGE_NOJUNK = 1<<18,
};

struct _CamelGroupwiseMessageInfo {
	CamelMessageInfoBase info;

	guint32 server_flags;
} ;


struct _CamelGroupwiseMessageContentInfo {
	CamelMessageContentInfo info ;
} ;


struct _CamelGroupwiseSummary {
	CamelFolderSummary parent ;

	char *time_string;
	guint32 version ;
	guint32 validity ;
} ;


struct _CamelGroupwiseSummaryClass {
	CamelFolderSummaryClass parent_class ;
} ;


CamelType camel_groupwise_summary_get_type (void) ;

CamelFolderSummary *camel_groupwise_summary_new (struct _CamelFolder *folder, const char *filename) ;

void camel_gw_summary_add_offline (CamelFolderSummary *summary, const char *uid, CamelMimeMessage *messgae, const CamelMessageInfo *info) ;

void camel_gw_summary_add_offline_uncached (CamelFolderSummary *summary, const char *uid, const CamelMessageInfo *info) ;
void groupwise_summary_clear (CamelFolderSummary *summary, gboolean uncache);

G_END_DECLS

#endif /*_CAMEL_GW_SUMMARY_H*/
