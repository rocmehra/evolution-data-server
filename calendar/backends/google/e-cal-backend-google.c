/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Authors :
 *  Ebby Wiselyn <ebbywiselyn@gmail.com>
 *
 * Copyright 2007, Novell, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <bonobo/bonobo-exception.h>
#include <bonobo/bonobo-moniker-util.h>

#include <glib/gstdio.h>
#include <glib/gi18n-lib.h>

#include <libgnomevfs/gnome-vfs.h>
#include <libedataserver/e-data-server-util.h>
#include <libedataserver/e-xml-hash-utils.h>


#include <libedata-cal/e-cal-backend-util.h>
#include <libedata-cal/e-cal-backend-sexp.h>

#include <libecal/e-cal-recur.h>
#include <libecal/e-cal-time-util.h>
#include <libecal/e-cal-util.h>

#include <servers/google/libgdata/gdata-entry.h>
#include <servers/google/libgdata/gdata-feed.h>
#include <servers/google/libgdata-google/gdata-google-service.h>
#include <servers/google/libgdata/gdata-service-iface.h>
#include "e-cal-backend-google-utils.h"
#include "e-cal-backend-google.h"

#ifndef O_BINARY
#define O_BINARY 0
#endif

static ECalBackendGoogleClass *parent_class = NULL;
struct _ECalBackendGooglePrivate {
	ECalBackendCache *cache;
	ESource *source;

	GDataGoogleService *service;
	GMutex *mutex;
	GDataEntry *entry;
	GSList *entries;
	icaltimezone *default_zone;
	CalMode	mode;
	EGoItem *item;

	gint timeout_id;
	gchar *username;
	gchar *password;
	gchar *uri;
	gchar *feed;

	gboolean read_only;
	gboolean mode_changed;

};

gint compare_ids (gconstpointer cache_id, gconstpointer modified_cache_id);
gchar * form_query (const char *query);

gint
compare_ids (gconstpointer cache_id, gconstpointer modified_cache_id)
{
	return strcmp (cache_id, modified_cache_id);
}

/************************************************** Calendar Backend Methods **********************************/

static ECalBackendSyncStatus
e_cal_backend_google_get_attachment_list (ECalBackendSync *backend, EDataCal *cal, const char *uid, const char *rid, GSList **list)
{
	/* TODO implement this function */
	return GNOME_Evolution_Calendar_Success;
}

static icaltimezone *
e_cal_backend_google_internal_get_default_timezone (ECalBackend *backend)
{
	ECalBackendGoogle *cbgo = E_CAL_BACKEND_GOOGLE (backend);
	return cbgo->priv->default_zone;
}

static icaltimezone *
e_cal_backend_google_internal_get_timezone (ECalBackend *backend, const char *tzid)
{
	icaltimezone *zone;

	zone = icaltimezone_get_builtin_timezone_from_tzid (tzid);
	if (!zone)
		return icaltimezone_get_utc_timezone ();

	return zone;
}

static ECalBackendSyncStatus
e_cal_backend_google_get_free_busy (ECalBackendSync *backend,
				    EDataCal *cal,
				    GList *users,
				    time_t start,
				    time_t end,
				    GList **free_busy)
{

	/*FIXME*/
	return GNOME_Evolution_Calendar_Success;
}


static ECalBackendSyncStatus
e_cal_backend_google_remove (ECalBackendSync *backend, EDataCal *cal)
{
	ECalBackendGoogle *cbgo;
	ECalBackendGooglePrivate *priv;

	cbgo = E_CAL_BACKEND_GOOGLE (backend);
	priv = cbgo->priv;

	g_mutex_lock (priv->mutex);

	/* Remove the cache */
	if (priv->cache) {
		e_file_cache_remove (E_FILE_CACHE (priv->cache));
	}

	g_mutex_unlock (priv->mutex);

	return GNOME_Evolution_Calendar_Success;
}


static gboolean
e_cal_backend_google_is_loaded (ECalBackend *backend)
{
	ECalBackendGoogle *cbgo;
	ECalBackendGooglePrivate *priv;

	cbgo = E_CAL_BACKEND_GOOGLE (backend);
	priv = cbgo->priv;

	return priv->cache ? TRUE : FALSE;
}


static ECalBackendSyncStatus
e_cal_backend_google_add_timezone (ECalBackendSync *backend, EDataCal *cal, const char *tzobj)
{
	ECalBackendGoogle *cbgo;
	ECalBackendGooglePrivate *priv;
	icalcomponent *tz_comp;

	cbgo = (ECalBackendGoogle *) backend;

	g_return_val_if_fail (E_IS_CAL_BACKEND_GOOGLE (cbgo), GNOME_Evolution_Calendar_OtherError);
	g_return_val_if_fail (tzobj != NULL, GNOME_Evolution_Calendar_OtherError);

	priv = cbgo->priv;

	tz_comp = icalparser_parse_string (tzobj);
	if (!tz_comp)
		return GNOME_Evolution_Calendar_InvalidObject;

	if (icalcomponent_isa (tz_comp) == ICAL_VTIMEZONE_COMPONENT) {
		icaltimezone *zone;
		zone = icaltimezone_new ();
		icaltimezone_set_component (zone, tz_comp);

		if (e_cal_backend_cache_put_timezone (priv->cache, zone) == FALSE) {
			icaltimezone_free (zone, 1);
			return GNOME_Evolution_Calendar_OtherError;
		}
		icaltimezone_free (zone, 1);
	}
	return GNOME_Evolution_Calendar_Success;
}

static ECalBackendSyncStatus
 e_cal_backend_google_discard_alarm (ECalBackendSync *backend, EDataCal *cal, const char *uid, const char *auid)
{
	return GNOME_Evolution_Calendar_OtherError;
}

static CalMode
e_cal_backend_google_get_mode (ECalBackend *backend)
{
	ECalBackendGoogle *cbgo;
	ECalBackendGooglePrivate *priv;

	cbgo = E_CAL_BACKEND_GOOGLE (backend);
	priv = cbgo->priv;

	return priv->mode;
}

static ECalBackendSyncStatus
e_cal_backend_google_get_timezone (ECalBackendSync *backend, EDataCal *cal, const char *tzid, char **object)
{
	ECalBackendGoogle *cbgo;
	ECalBackendGooglePrivate *priv;

	icaltimezone *zone;
	icalcomponent *icalcomp;

	cbgo = E_CAL_BACKEND_GOOGLE (backend);
	priv = cbgo->priv;

	g_return_val_if_fail (tzid!=NULL, GNOME_Evolution_Calendar_ObjectNotFound);

	if (!strcmp (tzid, "UTC")) {
		zone = icaltimezone_get_utc_timezone ();
	} else {
		zone = icaltimezone_get_builtin_timezone_from_tzid (tzid);
		if (!zone)
			return GNOME_Evolution_Calendar_ObjectNotFound;
	}

	icalcomp = icaltimezone_get_component (zone);
	if (!icalcomp)
		return GNOME_Evolution_Calendar_InvalidObject;

	*object = g_strdup (icalcomponent_as_ical_string (icalcomp));

	return GNOME_Evolution_Calendar_Success;
}

static ECalBackendSyncStatus
e_cal_backend_google_get_object (ECalBackendSync *backend, EDataCal *cal, const char *uid, const char *rid, char **object)
{
	ECalComponent *comp;
	ECalBackendGooglePrivate *priv;
	ECalBackendGoogle *cbgo = (ECalBackendGoogle *) backend;

	g_return_val_if_fail (E_IS_CAL_BACKEND_GOOGLE (cbgo), GNOME_Evolution_Calendar_OtherError);

	priv = cbgo->priv;

	g_mutex_lock (priv->mutex);

	/* search the object in the cache */
	comp = e_cal_backend_cache_get_component (priv->cache, uid, rid);

	if (comp) {
		g_mutex_unlock (priv->mutex);
		if (e_cal_backend_get_kind (E_CAL_BACKEND (backend)) ==
		    icalcomponent_isa (e_cal_component_get_icalcomponent (comp)))
			*object = e_cal_component_get_as_string (comp);
		else
			*object = NULL;

		g_object_unref (comp);

		return *object ? GNOME_Evolution_Calendar_Success : GNOME_Evolution_Calendar_ObjectNotFound;
	}

	g_mutex_unlock (priv->mutex);

	/* callers will never have a uid that is in server but not in cache */
	return GNOME_Evolution_Calendar_ObjectNotFound;
}

static ECalBackendSyncStatus
e_cal_backend_google_get_object_list (ECalBackendSync *backend, EDataCal *cal, const char *sexp, GList **objects)
{
	ECalBackendGoogle *cbgo;
	ECalBackendGooglePrivate *priv;
	GList *components, *l;
	ECalBackendSExp *cbsexp;
	gboolean search_needed = TRUE;

	cbgo = E_CAL_BACKEND_GOOGLE (backend);
	priv = cbgo->priv;

	g_mutex_lock (priv->mutex);

	search_needed = FALSE;
	cbsexp = e_cal_backend_sexp_new (sexp);

	if (!cbsexp) {
		g_mutex_unlock (priv->mutex);
		return GNOME_Evolution_Calendar_InvalidQuery;
	}

	*objects = NULL;
	components = e_cal_backend_cache_get_components (priv->cache);

	for (l = components; l != NULL; l = l->next) {
		ECalComponent *comp = E_CAL_COMPONENT (l->data);
		if (e_cal_backend_get_kind (E_CAL_BACKEND (backend)) ==
				icalcomponent_isa (e_cal_component_get_icalcomponent (comp))) {
			if ((!search_needed) ||
					(e_cal_backend_sexp_match_comp (cbsexp, comp, E_CAL_BACKEND (backend)))) {
				*objects = g_list_append (*objects, e_cal_component_get_as_string (comp));
			}
		}
	}

	g_object_unref (cbsexp);
	g_list_foreach (components, (GFunc) g_object_unref, NULL);
	g_list_free (components);
	g_mutex_unlock (priv->mutex);
	return GNOME_Evolution_Calendar_Success;
}

static void
e_cal_backend_google_start_query (ECalBackend *backend, EDataCalView *query)
{
	ECalBackendSyncStatus status;
	ECalBackendGoogle *cbgo;
	ECalBackendGooglePrivate *priv;

	GList *objects = NULL;
	cbgo = E_CAL_BACKEND_GOOGLE(backend);
	priv = cbgo->priv;

	status = e_cal_backend_google_get_object_list (E_CAL_BACKEND_SYNC(backend), NULL, e_data_cal_view_get_text (query), &objects);

	if (status != GNOME_Evolution_Calendar_Success) {
		e_data_cal_view_notify_done (query, status);
		return;
	}

	if (objects) {
		e_data_cal_view_notify_objects_added (query, (const GList *) objects);
		/* free memory */
		g_list_foreach (objects, (GFunc)g_free, NULL);
		g_list_free (objects);
	}

	e_data_cal_view_notify_done (query, GNOME_Evolution_Calendar_Success);
}

static ECalBackendSyncStatus
e_cal_backend_google_get_default_object (ECalBackendSync *backend, EDataCal *cal, char **object)
{
	ECalComponent *comp;

        comp = e_cal_component_new ();
	switch (e_cal_backend_get_kind (E_CAL_BACKEND (backend))) {
	case ICAL_VEVENT_COMPONENT:
        	e_cal_component_set_new_vtype (comp, E_CAL_COMPONENT_EVENT);
		break;
	case ICAL_VTODO_COMPONENT:
		e_cal_component_set_new_vtype (comp, E_CAL_COMPONENT_TODO);
		break;
	default:
		g_object_unref (comp);
		return GNOME_Evolution_Calendar_ObjectNotFound;
	}

	*object = e_cal_component_get_as_string (comp);

	if (comp)
		g_object_unref (comp);
	return GNOME_Evolution_Calendar_Success;
}

static ECalBackendSyncStatus
e_cal_backend_google_set_default_zone (ECalBackendSync *backend, EDataCal *cal, const char *tzobj)
{
	icalcomponent *tz_comp;
	ECalBackendGoogle *cbgo;
	ECalBackendGooglePrivate *priv;
	icaltimezone *zone;

	cbgo = (ECalBackendGoogle *) backend;

	g_return_val_if_fail(E_IS_CAL_BACKEND_GOOGLE(cbgo), GNOME_Evolution_Calendar_OtherError);
	g_return_val_if_fail(tzobj!=NULL, GNOME_Evolution_Calendar_OtherError);

	priv = cbgo->priv;
	tz_comp = icalparser_parse_string (tzobj);
	if (!tz_comp)
		return GNOME_Evolution_Calendar_InvalidObject;

	zone = icaltimezone_new ();
	icaltimezone_set_component (zone, tz_comp);

	if (priv->default_zone)
		icaltimezone_free (priv->default_zone, 1);

	priv->default_zone = zone;
	return GNOME_Evolution_Calendar_Success;
}


static void
e_cal_backend_google_set_mode (ECalBackend *backend, CalMode mode)
{
	ECalBackendGoogle *cbgo;
	ECalBackendGooglePrivate *priv;

	cbgo = E_CAL_BACKEND_GOOGLE (backend);
	priv = cbgo->priv;

	if (!priv->mode && priv->mode == mode) {
		e_cal_backend_notify_mode (backend, GNOME_Evolution_Calendar_CalListener_MODE_SET,
				  	   cal_mode_to_corba (mode));
		return;
	}

	g_mutex_lock (priv->mutex);

	priv->mode_changed = TRUE;
	switch (mode) {
		case CAL_MODE_REMOTE:
			priv->mode = CAL_MODE_REMOTE;
			priv->read_only = FALSE;
			e_cal_backend_notify_mode (backend, GNOME_Evolution_Calendar_CalListener_MODE_SET,
					GNOME_Evolution_Calendar_MODE_REMOTE);
			e_cal_backend_notify_readonly (backend, priv->read_only);

			break;
		case CAL_MODE_LOCAL:
			priv->mode = CAL_MODE_LOCAL;
			/*FIXME close the connection */
			e_cal_backend_notify_mode (backend, GNOME_Evolution_Calendar_CalListener_MODE_SET,
					GNOME_Evolution_Calendar_MODE_REMOTE);
			e_cal_backend_notify_readonly (backend, priv->read_only);

			break;
		default:
			e_cal_backend_notify_mode (backend, GNOME_Evolution_Calendar_CalListener_MODE_NOT_SUPPORTED,
					cal_mode_to_corba (mode));
	}

	g_mutex_unlock (priv->mutex);
}

static ECalBackendSyncStatus
e_cal_backend_google_receive_objects (ECalBackendSync *backend, EDataCal *cal, const char *calobj)
{
	/* FIXME Not implemented */
	return GNOME_Evolution_Calendar_Success;
}

static ECalBackendSyncStatus
e_cal_backend_google_send_objects (ECalBackendSync *backend, EDataCal *cal, const char *calobj, GList **users, char **modified_calobj)
{
	/* FIXME Not Implemented */
	return GNOME_Evolution_Calendar_Success;
}

static ECalBackendSyncStatus
e_cal_backend_google_get_changes (ECalBackend *backend, EDataCal *cal, const char *change_id, GList **adds, GList **modifies, GList *deletes)
{
	/* FIXME Not Implemented */
	return GNOME_Evolution_Calendar_Success;
}

/* Is read only handler for the google backend */
static ECalBackendSyncStatus
e_cal_backend_google_is_read_only (ECalBackendSync *backend, EDataCal *cal, gboolean *read_only)
{
	/* FIXME */
	*read_only = FALSE;

	return GNOME_Evolution_Calendar_Success;
}


/* Returns the email address of the person who opened the calendar */
static ECalBackendSyncStatus
e_cal_backend_google_get_cal_address (ECalBackendSync *backend, EDataCal *cal, char **address)
{
	ECalBackendGoogle *cbgo;
	ECalBackendGooglePrivate *priv;

	cbgo = E_CAL_BACKEND_GOOGLE (backend);
	priv = cbgo->priv;

	/* FIXME */
	*address = g_strdup (priv->username);
	return GNOME_Evolution_Calendar_Success;
}

static ECalBackendSyncStatus
e_cal_backend_google_get_alarm_email_address (ECalBackendSync *backend, EDataCal *cal, char **address)
{
	/* Support email based alarms ? */

	/* FIXME */
	*address = NULL;
	return GNOME_Evolution_Calendar_Success;
}

static ECalBackendSyncStatus
e_cal_backend_google_modify_object (ECalBackendSync *backend, EDataCal *cal, const char *calobj, CalObjModType mod, char **old_object, char **new_object)
{
	ECalBackendGoogle *cbgo;
	ECalBackendGooglePrivate *priv;
	icalcomponent *icalcomp;
	ECalComponent *comp = NULL, *cache_comp = NULL;
	EGoItem *item;
	const char *uid=NULL, *rid=NULL;
	GDataEntry *entry, *entry_from_server=NULL;
	gchar *edit_link;

	*old_object = NULL;
	cbgo = E_CAL_BACKEND_GOOGLE (backend);
	priv = cbgo->priv;

	g_return_val_if_fail (E_IS_CAL_BACKEND_GOOGLE (cbgo), GNOME_Evolution_Calendar_InvalidObject);
	g_return_val_if_fail (calobj != NULL, GNOME_Evolution_Calendar_InvalidObject);

	if (priv->mode == CAL_MODE_LOCAL) {
		/* FIXME */
		return GNOME_Evolution_Calendar_RepositoryOffline;
	}

	icalcomp = icalparser_parse_string (calobj);
	if (!icalcomp)
		return GNOME_Evolution_Calendar_InvalidObject;

	comp = e_cal_component_new ();
	e_cal_component_set_icalcomponent (comp, icalcomp);
	e_cal_component_get_uid (comp, &uid);

	/* Check if object exists */
	GSList *l;
	switch (priv->mode) {
		case CAL_MODE_ANY:
		case CAL_MODE_REMOTE:
			cache_comp = e_cal_backend_cache_get_component (priv->cache, uid, rid);

			if (!cache_comp) {
				g_message ("CRITICAL: Could not find the object in cache ");
				g_object_unref (comp);
				return GNOME_Evolution_Calendar_ObjectNotFound;
			}

			item = e_go_item_from_cal_component (cbgo, comp);
			item->feed = gdata_service_get_feed (GDATA_SERVICE(priv->service), priv->uri);
			entry = item->entry;

			if (!item->feed) {
				g_message ("CRITICAL: Could not find feed in EGoItem %s", G_STRLOC);
				g_object_unref (comp);
				return GNOME_Evolution_Calendar_OtherError;
			}

			l = gdata_feed_get_entries (item->feed);
			entry_from_server = gdata_entry_get_entry_by_id (l, uid);

			if (!GDATA_IS_ENTRY(entry_from_server)) {
				g_object_unref (comp);
				return GNOME_Evolution_Calendar_OtherError;
			}

			edit_link = gdata_entry_get_edit_link (entry_from_server);
			gdata_service_update_entry_with_link (GDATA_SERVICE (priv->service), entry, edit_link);
			break;
		case CAL_MODE_LOCAL:
			e_cal_backend_cache_put_component (priv->cache, comp);
			break;
		default:
			break;
	}

	*old_object = e_cal_component_get_as_string (cache_comp);
	*new_object = e_cal_component_get_as_string (comp);
	g_object_unref (cache_comp);
	g_object_unref (comp);
	return GNOME_Evolution_Calendar_Success;
}


static ECalBackendSyncStatus
e_cal_backend_google_remove_object (ECalBackendSync *backend, EDataCal *cal,
				       const char *uid, const char *rid,
				       CalObjModType mod, char **old_object,
				       char **object)
{
	ECalBackendGoogle *cbgo;
	GDataEntry *entry;
	ECalBackendGooglePrivate *priv;
	ECalComponent *comp = NULL;
	char *calobj = NULL;
	GSList *entries = NULL;
	EGoItem *item;

	cbgo = E_CAL_BACKEND_GOOGLE (backend);
	priv = cbgo->priv;
	item = priv->item;

	*old_object = *object = NULL;
	/* FIXME */
	item->feed = gdata_service_get_feed (GDATA_SERVICE(priv->service), priv->uri);

	entries = gdata_feed_get_entries (item->feed);

	if (priv->mode == CAL_MODE_REMOTE) {
		ECalBackendSyncStatus status;
		icalcomponent *icalcomp;
		ECalComponentId *id;

		status = e_cal_backend_google_get_object (backend, cal, uid, rid, &calobj);

		if (status != GNOME_Evolution_Calendar_Success)
			return status;

		comp = e_cal_backend_cache_get_component (priv->cache, uid, rid);
		id = e_cal_component_get_id (comp);

		icalcomp = icalparser_parse_string (calobj);

		if (!icalcomp) {
			g_free (calobj);
			return GNOME_Evolution_Calendar_InvalidObject;
		}

		e_cal_backend_cache_remove_component (priv->cache, uid, rid);
		e_cal_backend_notify_object_removed (E_CAL_BACKEND (cbgo), id, e_cal_component_get_as_string (comp), NULL);

		entry = gdata_entry_get_entry_by_id (entries, uid);

		if (!entry)
			return GNOME_Evolution_Calendar_InvalidObject;

	        gdata_service_delete_entry (GDATA_SERVICE(priv->service), entry);
		*object = NULL;
		*old_object = strdup (calobj);

	}

	if (calobj)
		g_free (calobj);
	if (entries)
		g_slist_free (entries);

	return GNOME_Evolution_Calendar_Success;
}

static ECalBackendSyncStatus
e_cal_backend_google_create_object (ECalBackendSync *backend, EDataCal *cal, char **calobj, char **uid)
{
	ECalBackendGoogle *cbgo;
	ECalBackendGooglePrivate *priv;
	icalcomponent *icalcomp;
	ECalComponent *comp;
	EGoItem *item;
	GDataEntry *entry;
	const gchar *id;

	cbgo = E_CAL_BACKEND_GOOGLE (backend);

	g_return_val_if_fail (E_IS_CAL_BACKEND_GOOGLE(cbgo), GNOME_Evolution_Calendar_InvalidObject);
	g_return_val_if_fail (calobj != NULL && *calobj!=NULL,GNOME_Evolution_Calendar_InvalidObject);
	priv = cbgo->priv;
	if (priv->mode == CAL_MODE_LOCAL) {
		/*FIXME call offline method */
		return GNOME_Evolution_Calendar_RepositoryOffline;
	}

	icalcomp = icalparser_parse_string (*calobj);
	if (!icalcomp)	{
		return GNOME_Evolution_Calendar_InvalidObject;
	}

	if (e_cal_backend_get_kind(E_CAL_BACKEND(backend)) != icalcomponent_isa (icalcomp)) {
		icalcomponent_free (icalcomp);
		return GNOME_Evolution_Calendar_InvalidObject;
	}

	comp = e_cal_component_new ();
	e_cal_component_set_icalcomponent (comp, icalcomp);

	/* Check if object exists */
	switch (priv->mode) {

		case CAL_MODE_ANY:
		case CAL_MODE_REMOTE:
			/* Create an appointment */

			item = e_go_item_from_cal_component (cbgo, comp);
			entry = e_go_item_get_entry (item);

			GDataEntry *updated_entry;
			updated_entry = gdata_service_insert_entry (GDATA_SERVICE(priv->service), priv->uri, entry);
			if (!GDATA_IS_ENTRY (updated_entry)) {
				g_message ("\n Entry Insertion Failed %s \n", G_STRLOC);
			}

			id = gdata_entry_get_id (updated_entry);
			e_cal_component_set_uid (comp, id);

			break;
		default:
			break;
	}

	/* Go through the uid list to create the objects */
	e_cal_component_commit_sequence (comp);
	e_cal_backend_cache_put_component (priv->cache, comp);
	*calobj = e_cal_component_get_as_string (comp);
	e_cal_backend_notify_object_created (E_CAL_BACKEND(cbgo), *calobj);

	g_object_unref (comp);
	return GNOME_Evolution_Calendar_Success;
}

static ECalBackendSyncStatus
e_cal_backend_google_get_ldap_attribute (ECalBackendSync *backend, EDataCal *cal, char **attribute)
{
	/* ldap attribute is specific to Sun ONE connector to get free busy information*/
	/* return NULL here as google backend know how to get free busy information */

	/* FIXME */
	*attribute = NULL;
	return GNOME_Evolution_Calendar_Success;
}


static ECalBackendSyncStatus
e_cal_backend_google_get_static_capabilities (ECalBackendSync *backend, EDataCal *cal, char **capabilities)
{
	/* FIXME */
	*capabilities = NULL;
	return GNOME_Evolution_Calendar_Success;
}

static ECalBackendSyncStatus
e_cal_backend_google_open (ECalBackendSync *backend, EDataCal *cal, gboolean only_if_exists,const char *username, const char *password)
{
	ECalBackendGoogle *cbgo;
	ECalBackendGooglePrivate *priv;
	ECalBackendSyncStatus status;
	ECalSourceType source_type;
	gchar *source = NULL;

	cbgo = E_CAL_BACKEND_GOOGLE(backend);
	priv = cbgo->priv;
	g_mutex_lock (priv->mutex);
	cbgo->priv->read_only = FALSE;

	switch (e_cal_backend_get_kind (E_CAL_BACKEND (backend))) {
		case ICAL_VEVENT_COMPONENT:
			source_type = E_CAL_SOURCE_TYPE_EVENT;
			source = "calendar";
			break;
		case ICAL_VTODO_COMPONENT:
			source_type = E_CAL_SOURCE_TYPE_TODO;
			source = "tasks";
			break;
		case ICAL_VJOURNAL_COMPONENT:
			source_type = E_CAL_SOURCE_TYPE_JOURNAL;
			source = "journal";
			break;
		default:
			source_type = E_CAL_SOURCE_TYPE_EVENT;
	}

	/* Not for remote */
	if (priv->mode == CAL_MODE_LOCAL) {
		ESource *esource;
		const char *display_contents = NULL;

		cbgo->priv->read_only = TRUE;
		esource = e_cal_backend_get_source (E_CAL_BACKEND(cbgo));
		display_contents = e_source_get_property (esource, "offline-sync");

		if(!display_contents || !g_str_equal (display_contents, "1")) {
			g_mutex_unlock(priv->mutex);
			return GNOME_Evolution_Calendar_RepositoryOffline;
		}

		/* Cache created here for the first time */
		if (!priv->cache) {
			priv->cache = e_cal_backend_cache_new (e_cal_backend_get_uri (E_CAL_BACKEND (cbgo)), source_type);
			if (!priv->cache) {
				g_mutex_unlock(priv->mutex);
				e_cal_backend_notify_error (E_CAL_BACKEND (cbgo), _("Could not create cache file"));
				return GNOME_Evolution_Calendar_OtherError;
			}
		}

		/* Timezone */
		e_cal_backend_cache_put_default_timezone (priv->cache, priv->default_zone);
		g_mutex_unlock (priv->mutex);
		return GNOME_Evolution_Calendar_Success;
	}

	priv->username = g_strdup(username);
	priv->password = g_strdup(password);

	/*FIXME */
	status = e_cal_backend_google_utils_connect (cbgo);
	g_mutex_unlock (priv->mutex);
	return status;
}

/* Dipose handler for google backend */
static void
e_cal_backend_google_dispose (GObject *object)
{
	ECalBackendGoogle *cbgo;
	ECalBackendGooglePrivate *priv;

	cbgo = E_CAL_BACKEND_GOOGLE (object);
	priv = cbgo->priv;

	if (G_OBJECT_CLASS (parent_class)->dispose)
		(* G_OBJECT_CLASS (parent_class)->dispose) (object);
}

/* Finalize handler for google backend */
static void
e_cal_backend_google_finalize (GObject *object)
{
	ECalBackendGoogle *cbgo;
	ECalBackendGooglePrivate *priv;

	g_return_if_fail (object != NULL);
	g_return_if_fail (E_IS_CAL_BACKEND_GOOGLE (object));

	cbgo = E_CAL_BACKEND_GOOGLE (object);
	priv = cbgo->priv;

	/* clean up */

	if (priv->mutex) {
		g_mutex_free (priv->mutex);
		priv->mutex = NULL;
	}

	if (priv->username) {
		g_free (priv->username);
		priv->username = NULL;
	}

	if (priv->password) {
		g_free (priv->password);
		priv->password = NULL;
	}

	if (priv->uri) {
		g_free (priv->uri);
		priv->uri = NULL;
	}

	if (priv->cache) {
		g_object_unref (priv->cache);
		priv->cache = NULL;
	}


	if (priv->default_zone) {
		icaltimezone_free (priv->default_zone, 1);
		priv->default_zone = NULL;
	}

	if (priv->timeout_id) {
		g_source_remove (priv->timeout_id);
		priv->timeout_id = 0;
	}

	g_free (priv);
	cbgo->priv = NULL;

	if (G_OBJECT_CLASS (parent_class)->finalize) {
		(* G_OBJECT_CLASS (parent_class)->finalize) (object);
	}
}

/* Object initialisation function for google backend */
static void
e_cal_backend_google_init (ECalBackendGoogle *cbgo, ECalBackendGoogleClass *class)
{
	ECalBackendGooglePrivate *priv;

	priv = g_new0 (ECalBackendGooglePrivate, 1);

	priv->mutex = g_mutex_new ();
	priv->username = NULL;
	priv->password = NULL;
	priv->entry = NULL;
	priv->service = NULL;
	priv->timeout_id = 0;
	cbgo->priv = priv;

	/* FIXME set a lock */
	e_cal_backend_sync_set_lock (E_CAL_BACKEND_SYNC (cbgo), TRUE);
}

icaltimezone *
e_cal_backend_google_get_default_zone (ECalBackendGoogle *cbgo)
{
	g_return_val_if_fail (E_IS_CAL_BACKEND_GOOGLE(cbgo), NULL);
	return cbgo->priv->default_zone;
}

/* Class initialisation function for the google backend */
static void
e_cal_backend_google_class_init (ECalBackendGoogleClass *class)
{
	GObjectClass *object_class;
	ECalBackendClass *backend_class;
	ECalBackendSyncClass *sync_class;

	object_class = (GObjectClass *) class;
	backend_class = (ECalBackendClass *) class;
	sync_class = (ECalBackendSyncClass *) class;

	parent_class = g_type_class_peek_parent (class);

	object_class->dispose = e_cal_backend_google_dispose;
	object_class->finalize = e_cal_backend_google_finalize;

	sync_class->is_read_only_sync = e_cal_backend_google_is_read_only;
	sync_class->get_cal_address_sync = e_cal_backend_google_get_cal_address;
	sync_class->get_alarm_email_address_sync = e_cal_backend_google_get_alarm_email_address;
	sync_class->get_ldap_attribute_sync = e_cal_backend_google_get_ldap_attribute;
	sync_class->get_static_capabilities_sync = e_cal_backend_google_get_static_capabilities;
	sync_class->open_sync = e_cal_backend_google_open;
	sync_class->remove_sync = e_cal_backend_google_remove;
	sync_class->create_object_sync = e_cal_backend_google_create_object;
	sync_class->modify_object_sync = e_cal_backend_google_modify_object;
	sync_class->remove_object_sync = e_cal_backend_google_remove_object;
	sync_class->discard_alarm_sync = e_cal_backend_google_discard_alarm;
	sync_class->receive_objects_sync = e_cal_backend_google_receive_objects;
	sync_class->send_objects_sync = e_cal_backend_google_send_objects;
	sync_class->get_default_object_sync = e_cal_backend_google_get_default_object;
	sync_class->get_object_sync = e_cal_backend_google_get_object;
	sync_class->get_object_list_sync = e_cal_backend_google_get_object_list;
	sync_class->get_attachment_list_sync = e_cal_backend_google_get_attachment_list;
	sync_class->get_timezone_sync = e_cal_backend_google_get_timezone;
	sync_class->add_timezone_sync = e_cal_backend_google_add_timezone;
	sync_class->set_default_zone_sync = e_cal_backend_google_set_default_zone;
	sync_class->get_freebusy_sync = e_cal_backend_google_get_free_busy;
	sync_class->get_changes_sync = e_cal_backend_google_get_changes;
	backend_class->is_loaded = e_cal_backend_google_is_loaded;
	backend_class->start_query = e_cal_backend_google_start_query;
	backend_class->get_mode = e_cal_backend_google_get_mode;
	backend_class->set_mode = e_cal_backend_google_set_mode;
	backend_class->internal_get_default_timezone = e_cal_backend_google_internal_get_default_timezone;
	backend_class->internal_get_timezone = e_cal_backend_google_internal_get_timezone;
}

/**
 * e_cal_backend_google_get_type:
 * @void:
 * Registers the #ECalBackendGoogle class if necessary and returns the type ID
 * associated with it
 * Return value: The type id of the #ECalBackendGoogle class.
 **/
GType
e_cal_backend_google_get_type ()
{
	static	GType e_cal_backend_google_type = 0;

	if (!e_cal_backend_google_type) {
		static GTypeInfo info = {
			sizeof (ECalBackendGoogleClass),
			(GBaseInitFunc) NULL,
			(GBaseFinalizeFunc) NULL,
			(GClassInitFunc) e_cal_backend_google_class_init,
			NULL, NULL,
			sizeof (ECalBackendGoogle),
			0,
			(GInstanceInitFunc) e_cal_backend_google_init
		};
		e_cal_backend_google_type = g_type_register_static (E_TYPE_CAL_BACKEND_SYNC,
				"ECalBackendGoogle", &info, 0);
	}
	return e_cal_backend_google_type;
}


/***************************************** Helper Functions ****************************************************/

/**
 * e_cal_backend_google_set_cache:
 * @cbgo a #ECalBackendGoogle object
 * @cache a #ECalBackendCache
 *
 **/

void
e_cal_backend_google_set_cache (ECalBackendGoogle *cbgo, ECalBackendCache *cache)
{
	ECalBackendGooglePrivate *priv;

	g_return_if_fail (cbgo != NULL);
	g_return_if_fail (E_IS_CAL_BACKEND_GOOGLE(cbgo));

	priv = cbgo->priv;
	priv->cache = cache;
}

/**
 * e_cal_backend_google_set_item:
 * @cbgo a #ECalBackendGoogle object
 * @cache a #EGoItem *item
 *
 **/
void
e_cal_backend_google_set_item (ECalBackendGoogle *cbgo, EGoItem *item)
{
	ECalBackendGooglePrivate *priv;

	g_return_if_fail (cbgo != NULL);
	g_return_if_fail (E_IS_CAL_BACKEND_GOOGLE(cbgo));

	priv = cbgo->priv;
	priv->item = item;
}

/**
 * e_cal_backend_google_set_item:
 * @cbgo a #ECalBackendGoogle object
 * @cache a #EGoItem *item
 * Sets the #EGoItem item on object
 *
 **/
void
e_cal_backend_google_set_service (ECalBackendGoogle *cbgo, GDataGoogleService *service)
{
	ECalBackendGooglePrivate *priv;

	g_return_if_fail (cbgo != NULL);
	g_return_if_fail (E_IS_CAL_BACKEND_GOOGLE(cbgo));

	priv = cbgo->priv;
	priv->service = service;
}

/**
 * e_cal_backend_google_set_uri:
 * @cbgo a #ECalBackendGoogle cbgo
 * @uri  Private uri , for accessing google calendar
 * Sets the uri on cbgo
 *
 **/
void
e_cal_backend_google_set_uri (ECalBackendGoogle *cbgo, gchar *uri)
{
	ECalBackendGooglePrivate *priv;

	g_return_if_fail (cbgo != NULL);
	g_return_if_fail (E_IS_CAL_BACKEND_GOOGLE(cbgo));

	priv = cbgo->priv;
	priv->uri = uri;
}

/**
 * e_cal_backend_google_set_entry:
 * @cbgo a #ECalBackendGoogle object
 * @entry a #GDataEntry entry
 * Sets the entry on object
 *
 **/
void
e_cal_backend_google_set_entry (ECalBackendGoogle *cbgo, GDataEntry *entry)
{
	ECalBackendGooglePrivate *priv;

	g_return_if_fail (cbgo != NULL);
	g_return_if_fail (E_IS_CAL_BACKEND_GOOGLE(cbgo));

	priv = cbgo->priv;
	priv->entry = entry;
}

/**
 * e_cal_backend_google_set_timeout_id:
 * @cbgo a #ECalBackendGoogle object
 * @timeout_id a time out id
 * Sets the timeout id on object
 *
 **/
void
e_cal_backend_google_set_timeout_id (ECalBackendGoogle *cbgo,gint timeout_id )
{
	ECalBackendGooglePrivate *priv;
	priv = cbgo->priv;

	g_return_if_fail (cbgo != NULL);
	g_return_if_fail (E_IS_CAL_BACKEND_GOOGLE(cbgo));

	priv->timeout_id = timeout_id;
}

/**
 * e_cal_backend_google_set_username:
 * @cbgo a #ECalBackendGoogle object
 * @username username
 * Sets the username on object .
 *
 **/
void
e_cal_backend_google_set_username (ECalBackendGoogle *cbgo,gchar *username)
{
	ECalBackendGooglePrivate *priv;
	priv = cbgo->priv;

	g_return_if_fail (cbgo != NULL);
	g_return_if_fail (E_IS_CAL_BACKEND_GOOGLE(cbgo));

	priv->username = username;
}

/**
 * e_cal_backend_google_set_password:
 * @cbgo a #ECalBackendGoogle object
 * @cache a #EGoItem *item
 * Sets the #EGoItem item on object
 *
 **/

void
e_cal_backend_google_set_password (ECalBackendGoogle *cbgo,gchar *password)
{
	ECalBackendGooglePrivate *priv;
	priv = cbgo->priv;

	g_return_if_fail (cbgo != NULL);
	g_return_if_fail (E_IS_CAL_BACKEND_GOOGLE(cbgo));

	priv->password = password;
}

/**
 * e_cal_backend_google_get_cache:
 * @cbgo a #ECalBackendGoogle object
 * Gets the cache .
 *
 **/
ECalBackendCache *
e_cal_backend_google_get_cache (ECalBackendGoogle *cbgo)
{
	ECalBackendGooglePrivate *priv;

	g_return_val_if_fail (cbgo != NULL, NULL);
	g_return_val_if_fail (E_IS_CAL_BACKEND_GOOGLE(cbgo), NULL);


	priv = cbgo->priv;
	return priv->cache;
}

/**
 * e_cal_backend_google_get_item:
 * @cbgo a #ECalBackendGoogle object
 * Gets the #EGoItem . from cbgo
 **/
EGoItem *
e_cal_backend_google_get_item (ECalBackendGoogle *cbgo)
{
	ECalBackendGooglePrivate *priv;

	g_return_val_if_fail (cbgo != NULL, NULL);
	g_return_val_if_fail (E_IS_CAL_BACKEND_GOOGLE(cbgo), NULL);

	priv = cbgo->priv;
	return priv->item;
}

/**
 * e_cal_backend_google_get_service:
 * @cbgo a #ECalBackendGoogle object
 * Gets the #GDataGoogleService service object .
 *
 **/
GDataGoogleService *
e_cal_backend_google_get_service (ECalBackendGoogle *cbgo)
{
	ECalBackendGooglePrivate *priv;

	g_return_val_if_fail (cbgo != NULL, NULL);
	g_return_val_if_fail (E_IS_CAL_BACKEND_GOOGLE(cbgo), NULL);

	priv = cbgo->priv;
	return priv->service;
}

/**
 * e_cal_backend_google_set_item:
 * @cbgo a #ECalBackendGoogle object
 * Gets the uri
 **/
gchar *
e_cal_backend_google_get_uri (ECalBackendGoogle *cbgo)
{
	ECalBackendGooglePrivate *priv;

	g_return_val_if_fail (cbgo != NULL, NULL);
	g_return_val_if_fail (E_IS_CAL_BACKEND_GOOGLE(cbgo), NULL);

	priv = cbgo->priv;
	return priv->uri;
}

/**
 * e_cal_backend_google_get_entry:
 * @cbgo a #ECalBackendGoogle object
 * Gets the #GDataEntry object.
 *
 **/
GDataEntry *
e_cal_backend_google_get_entry (ECalBackendGoogle *cbgo)
{
	ECalBackendGooglePrivate *priv;

	g_return_val_if_fail (cbgo != NULL, NULL);
	g_return_val_if_fail (E_IS_CAL_BACKEND_GOOGLE(cbgo), NULL);

	priv = cbgo->priv;
	return priv->entry;
}

/**
 * e_cal_backend_google_get_timeout_id:
 * @cbgo a #ECalBackendGoogle object
 * Gets the timeout id.
 *
 **/
gint
e_cal_backend_google_get_timeout_id (ECalBackendGoogle *cbgo)
{
	ECalBackendGooglePrivate *priv;

	g_return_val_if_fail (cbgo != NULL, NULL);
	g_return_val_if_fail (E_IS_CAL_BACKEND_GOOGLE(cbgo), NULL);

	priv = cbgo->priv;
	return priv->timeout_id;
}

/**
 * e_cal_backend_google_get_username:
 * @cbgo a #ECalBackendGoogle object
 * Gets the username
 *
 **/
gchar *
e_cal_backend_google_get_username (ECalBackendGoogle *cbgo)
{
	ECalBackendGooglePrivate *priv;

	g_return_val_if_fail (cbgo != NULL, NULL);
	g_return_val_if_fail (E_IS_CAL_BACKEND_GOOGLE(cbgo), NULL);

	priv = cbgo->priv;
	return priv->username;
}

/**
 * e_cal_backend_google_get_password:
 * @cbgo a #ECalBackendGoogle object
 * Gets the password
 **/
gchar *
e_cal_backend_google_get_password (ECalBackendGoogle *cbgo)
{
	ECalBackendGooglePrivate *priv;

	g_return_val_if_fail (cbgo != NULL, NULL);
	g_return_val_if_fail (E_IS_CAL_BACKEND_GOOGLE(cbgo), NULL);

	/* FIXME Encrypt ? */
	priv = cbgo->priv;
	return priv->password;
}



