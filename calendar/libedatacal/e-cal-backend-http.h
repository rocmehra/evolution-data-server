/* Evolution calendar - iCalendar file backend
 *
 * Copyright (C) 2000 Ximian, Inc.
 * Copyright (C) 2000 Ximian, Inc.
 *
 * Author: Federico Mena-Quintero <federico@ximian.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307, USA.
 */

#ifndef E_CAL_BACKEND_HTTP_H
#define E_CAL_BACKEND_HTTP_H

#include <libedatacal/e-cal-backend-sync.h>

G_BEGIN_DECLS



#define E_TYPE_CAL_BACKEND_HTTP            (e_cal_backend_http_get_type ())
#define E_CAL_BACKEND_HTTP(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), E_TYPE_CAL_BACKEND_HTTP,		\
					  ECalBackendHttp))
#define E_CAL_BACKEND_HTTP_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), E_TYPE_CAL_BACKEND_HTTP,	\
					  ECalBackendHttpClass))
#define E_IS_CAL_BACKEND_HTTP(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), E_TYPE_CAL_BACKEND_HTTP))
#define E_IS_CAL_BACKEND_HTTP_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), E_TYPE_CAL_BACKEND_HTTP))

typedef struct _ECalBackendHttp ECalBackendHttp;
typedef struct _ECalBackendHttpClass ECalBackendHttpClass;

typedef struct _ECalBackendHttpPrivate ECalBackendHttpPrivate;

struct _ECalBackendHttp {
	ECalBackendSync backend;

	/* Private data */
	ECalBackendHttpPrivate *priv;
};

struct _ECalBackendHttpClass {
	ECalBackendSyncClass parent_class;
};

GType       e_cal_backend_http_get_type      (void);



G_END_DECLS

#endif
