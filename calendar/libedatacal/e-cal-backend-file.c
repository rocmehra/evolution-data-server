/* Evolution calendar - iCalendar file backend
 *
 * Copyright (C) 2000-2003 Ximian, Inc.
 *
 * Authors: Federico Mena-Quintero <federico@ximian.com>
 *          Rodrigo Moya <rodrigo@ximian.com>
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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>
#include <unistd.h>
#include <bonobo/bonobo-exception.h>
#include <bonobo/bonobo-moniker-util.h>
#include <libgnome/gnome-i18n.h>
#include <libgnomevfs/gnome-vfs.h>
#include <libedataserver/e-xml-hash-utils.h>
#include <libecal/e-cal-recur.h>
#include <libecal/e-cal-util.h>
#include "e-cal-backend-file-events.h"
#include "e-cal-backend-util.h"
#include "e-cal-backend-sexp.h"



/* Placeholder for each component and its recurrences */
typedef struct {
	ECalComponent *full_object;
	GHashTable *recurrences;
} ECalBackendFileObject;

/* Private part of the ECalBackendFile structure */
struct _ECalBackendFilePrivate {
	/* URI where the calendar data is stored */
	char *uri;

	/* Filename in the dir */
	char *file_name;	
	gboolean read_only;

	/* Toplevel VCALENDAR component */
	icalcomponent *icalcomp;

	/* All the objects in the calendar, hashed by UID.  The
	 * hash key *is* the uid returned by cal_component_get_uid(); it is not
	 * copied, so don't free it when you remove an object from the hash
	 * table. Each item in the hash table is a ECalBackendFileObject.
	 */
	GHashTable *comp_uid_hash;

	GList *comp;
	
	/* The calendar's default timezone, used for resolving DATE and
	   floating DATE-TIME values. */
	icaltimezone *default_zone;

	/* The list of live queries */
	GList *queries;
};



static void e_cal_backend_file_dispose (GObject *object);
static void e_cal_backend_file_finalize (GObject *object);

static ECalBackendSyncClass *parent_class;



/* g_hash_table_foreach() callback to destroy recurrences in the hash table */
static void
free_recurrence (gpointer key, gpointer value, gpointer data)
{
	char *rid = key;
	ECalComponent *comp = value;

	g_free (rid);
	g_object_unref (comp);
}

/* g_hash_table_foreach() callback to destroy a ECalBackendFileObject */
static void
free_object (gpointer key, gpointer value, gpointer data)
{
	ECalBackendFileObject *obj_data = value;

	g_object_unref (obj_data->full_object);
	g_hash_table_foreach (obj_data->recurrences, (GHFunc) free_recurrence, NULL);
	g_hash_table_destroy (obj_data->recurrences);
}

/* Saves the calendar data */
static void
save (ECalBackendFile *cbfile)
{
	ECalBackendFilePrivate *priv;
	GnomeVFSURI *uri, *backup_uri;
	GnomeVFSHandle *handle = NULL;
	GnomeVFSResult result = GNOME_VFS_ERROR_BAD_FILE;
	GnomeVFSFileSize out;
	gchar *tmp, *backup_uristr;
	char *buf;
	
	priv = cbfile->priv;
	g_assert (priv->uri != NULL);
	g_assert (priv->icalcomp != NULL);

	uri = gnome_vfs_uri_new (priv->uri);
	if (!uri)
		goto error_malformed_uri;

	/* save calendar to backup file */
	tmp = gnome_vfs_uri_to_string (uri, GNOME_VFS_URI_HIDE_NONE);
	if (!tmp) {
		gnome_vfs_uri_unref (uri);
		goto error_malformed_uri;
	}
		
	backup_uristr = g_strconcat (tmp, "~", NULL);
	backup_uri = gnome_vfs_uri_new (backup_uristr);

	g_free (tmp);
	g_free (backup_uristr);

	if (!backup_uri) {
		gnome_vfs_uri_unref (uri);
		goto error_malformed_uri;
	}
	
	result = gnome_vfs_create_uri (&handle, backup_uri,
                                       GNOME_VFS_OPEN_WRITE,
                                       FALSE, 0666);
	if (result != GNOME_VFS_OK) {
		gnome_vfs_uri_unref (uri);
		gnome_vfs_uri_unref (backup_uri);
		goto error;
	}

	buf = icalcomponent_as_ical_string (priv->icalcomp);
	result = gnome_vfs_write (handle, buf, strlen (buf) * sizeof (char), &out);
	gnome_vfs_close (handle);
	if (result != GNOME_VFS_OK) {
		gnome_vfs_uri_unref (uri);
		gnome_vfs_uri_unref (backup_uri);
		goto error;
	}

	/* now copy the temporary file to the real file */
	result = gnome_vfs_move_uri (backup_uri, uri, TRUE);

	gnome_vfs_uri_unref (uri);
	gnome_vfs_uri_unref (backup_uri);
	if (result != GNOME_VFS_OK)
		goto error;

	return;

 error_malformed_uri:
	e_cal_backend_notify_error (E_CAL_BACKEND (cbfile),
				  _("Can't save calendar data: Malformed URI."));
	return;

 error:
	e_cal_backend_notify_error (E_CAL_BACKEND (cbfile), gnome_vfs_result_to_string (result));
	return;
}

/* Dispose handler for the file backend */
static void
e_cal_backend_file_dispose (GObject *object)
{
	ECalBackendFile *cbfile;
	ECalBackendFilePrivate *priv;

	cbfile = E_CAL_BACKEND_FILE (object);
	priv = cbfile->priv;

	/* Save if necessary */

	if (priv->comp_uid_hash) {
		g_hash_table_foreach (priv->comp_uid_hash, (GHFunc) free_object, NULL);
		g_hash_table_destroy (priv->comp_uid_hash);
		priv->comp_uid_hash = NULL;
	}

	g_list_free (priv->comp);
	priv->comp = NULL;

	if (priv->icalcomp) {
		icalcomponent_free (priv->icalcomp);
		priv->icalcomp = NULL;
	}

	if (G_OBJECT_CLASS (parent_class)->dispose)
		(* G_OBJECT_CLASS (parent_class)->dispose) (object);
}

/* Finalize handler for the file backend */
static void
e_cal_backend_file_finalize (GObject *object)
{
	ECalBackendFile *cbfile;
	ECalBackendFilePrivate *priv;

	g_return_if_fail (object != NULL);
	g_return_if_fail (E_IS_CAL_BACKEND_FILE (object));

	cbfile = E_CAL_BACKEND_FILE (object);
	priv = cbfile->priv;

	/* Clean up */

	if (priv->uri) {
	        g_free (priv->uri);
		priv->uri = NULL;
	}

	g_free (priv);
	cbfile->priv = NULL;

	if (G_OBJECT_CLASS (parent_class)->finalize)
		(* G_OBJECT_CLASS (parent_class)->finalize) (object);
}



/* Looks up a component by its UID on the backend's component hash table */
static ECalComponent *
lookup_component (ECalBackendFile *cbfile, const char *uid)
{
	ECalBackendFilePrivate *priv;
	ECalBackendFileObject *obj_data;

	priv = cbfile->priv;

	obj_data = g_hash_table_lookup (priv->comp_uid_hash, uid);
	return obj_data ? obj_data->full_object : NULL;
}



/* Calendar backend methods */

/* Is_read_only handler for the file backend */
static ECalBackendSyncStatus
e_cal_backend_file_is_read_only (ECalBackendSync *backend, EDataCal *cal, gboolean *read_only)
{
	ECalBackendFile *cbfile = (ECalBackendFile *) backend;

	*read_only = cbfile->priv->read_only;
	
	return GNOME_Evolution_Calendar_Success;
}

/* Get_email_address handler for the file backend */
static ECalBackendSyncStatus
e_cal_backend_file_get_cal_address (ECalBackendSync *backend, EDataCal *cal, char **address)
{
	/* A file backend has no particular email address associated
	 * with it (although that would be a useful feature some day).
	 */
	*address = NULL;

	return GNOME_Evolution_Calendar_Success;
}

static ECalBackendSyncStatus
e_cal_backend_file_get_ldap_attribute (ECalBackendSync *backend, EDataCal *cal, char **attribute)
{
	*attribute = NULL;
	
	return GNOME_Evolution_Calendar_Success;
}

static ECalBackendSyncStatus
e_cal_backend_file_get_alarm_email_address (ECalBackendSync *backend, EDataCal *cal, char **address)
{
 	/* A file backend has no particular email address associated
 	 * with it (although that would be a useful feature some day).
 	 */
	*address = NULL;
	
	return GNOME_Evolution_Calendar_Success;
}

static ECalBackendSyncStatus
e_cal_backend_file_get_static_capabilities (ECalBackendSync *backend, EDataCal *cal, char **capabilities)
{
	*capabilities = CAL_STATIC_CAPABILITY_NO_EMAIL_ALARMS;
	
	return GNOME_Evolution_Calendar_Success;
}

/* function to resolve timezones */
static icaltimezone *
resolve_tzid (const char *tzid, gpointer user_data)
{
	icalcomponent *vcalendar_comp = user_data;

        if (!tzid || !tzid[0])
                return NULL;
        else if (!strcmp (tzid, "UTC"))
                return icaltimezone_get_utc_timezone ();

	return icalcomponent_get_timezone (vcalendar_comp, tzid);
}

/* Checks if the specified component has a duplicated UID and if so changes it */
static void
check_dup_uid (ECalBackendFile *cbfile, ECalComponent *comp)
{
	ECalBackendFilePrivate *priv;
	ECalBackendFileObject *obj_data;
	const char *uid;
	char *new_uid;

	priv = cbfile->priv;

	e_cal_component_get_uid (comp, &uid);

	obj_data = g_hash_table_lookup (priv->comp_uid_hash, uid);
	if (!obj_data)
		return; /* Everything is fine */

	g_message ("check_dup_uid(): Got object with duplicated UID `%s', changing it...", uid);

	new_uid = e_cal_component_gen_uid ();
	e_cal_component_set_uid (comp, new_uid);
	g_free (new_uid);

	/* FIXME: I think we need to reset the SEQUENCE property and reset the
	 * CREATED/DTSTAMP/LAST-MODIFIED.
	 */

	save (cbfile);
}

static const char *
get_rid_string (ECalComponent *comp)
{
        ECalComponentRange range;
        struct icaltimetype tt;
                                                                                   
        e_cal_component_get_recurid (comp, &range);
        if (!range.datetime.value)
                return "0";
        tt = *range.datetime.value;
        e_cal_component_free_range (&range);
                                                                                   
        return icaltime_is_valid_time (tt) && !icaltime_is_null_time (tt) ?
                icaltime_as_ical_string (tt) : "0";
}

static struct icaltimetype
get_rid_icaltime (ECalComponent *comp)
{
	ECalComponentRange range;
        struct icaltimetype tt;
                                                                                   
        e_cal_component_get_recurid (comp, &range);
        if (!range.datetime.value)
                return icaltime_null_time ();
        tt = *range.datetime.value;
        e_cal_component_free_range (&range);
                                                                                   
        return tt;
}

/* Tries to add an icalcomponent to the file backend.  We only store the objects
 * of the types we support; all others just remain in the toplevel component so
 * that we don't lose them.
 */
static void
add_component (ECalBackendFile *cbfile, ECalComponent *comp, gboolean add_to_toplevel)
{
	ECalBackendFilePrivate *priv;
	ECalBackendFileObject *obj_data;
	const char *uid;
	GSList *categories;

	priv = cbfile->priv;

	if (e_cal_component_is_instance (comp)) { /* FIXME: more checks needed, to detect detached instances */
		const char *rid;

		e_cal_component_get_uid (comp, &uid);

		obj_data = g_hash_table_lookup (priv->comp_uid_hash, uid);
		if (!obj_data) {
			g_warning (G_STRLOC ": Got an instance of a non-existing component");
			return;
		}

		rid = get_rid_string (comp);
		if (g_hash_table_lookup (obj_data->recurrences, rid)) {
			g_warning (G_STRLOC ": Tried to adding an already existing recurrence");
			return;
		}

		g_hash_table_insert (obj_data->recurrences, g_strdup (rid), comp);
	} else {
		/* Ensure that the UID is unique; some broken implementations spit
		 * components with duplicated UIDs.
		 */
		check_dup_uid (cbfile, comp);
		e_cal_component_get_uid (comp, &uid);

		obj_data = g_new0 (ECalBackendFileObject, 1);
		obj_data->full_object = comp;
		obj_data->recurrences = g_hash_table_new (g_str_hash, g_str_equal);

		g_hash_table_insert (priv->comp_uid_hash, (gpointer) uid, obj_data);
	}

	priv->comp = g_list_prepend (priv->comp, comp);

	/* Put the object in the toplevel component if required */

	if (add_to_toplevel) {
		icalcomponent *icalcomp;

		icalcomp = e_cal_component_get_icalcomponent (comp);
		g_assert (icalcomp != NULL);

		icalcomponent_add_component (priv->icalcomp, icalcomp);
	}

	/* Update the set of categories */
	e_cal_component_get_categories_list (comp, &categories);
	e_cal_backend_ref_categories (E_CAL_BACKEND (cbfile), categories);
	e_cal_component_free_categories_list (categories);
}

/* g_hash_table_foreach() callback to remove recurrences from the calendar */
static void
remove_recurrence_cb (gpointer key, gpointer value, gpointer data)
{
	GList *l;
	GSList *categories;
	icalcomponent *icalcomp;
	ECalBackendFilePrivate *priv;
	ECalComponent *comp = value;
	ECalBackendFile *cbfile = data;

	priv = cbfile->priv;

	/* remove the recurrence from the top-level calendar */
	icalcomp = e_cal_component_get_icalcomponent (comp);
	g_assert (icalcomp != NULL);

	icalcomponent_remove_component (priv->icalcomp, icalcomp);

	/* remove it from our mapping */
	l = g_list_find (priv->comp, comp);
	priv->comp = g_list_delete_link (priv->comp, l);

	/* update the set of categories */
	e_cal_component_get_categories_list (comp, &categories);
	e_cal_backend_unref_categories (E_CAL_BACKEND (cbfile), categories);
	e_cal_component_free_categories_list (categories);
}

/* Removes a component from the backend's hash and lists.  Does not perform
 * notification on the clients.  Also removes the component from the toplevel
 * icalcomponent.
 */
static void
remove_component (ECalBackendFile *cbfile, ECalComponent *comp)
{
	ECalBackendFilePrivate *priv;
	icalcomponent *icalcomp;
	const char *uid;
	GList *l;
	GSList *categories;
	ECalBackendFileObject *obj_data;

	priv = cbfile->priv;

	/* Remove the icalcomp from the toplevel */

	icalcomp = e_cal_component_get_icalcomponent (comp);
	g_assert (icalcomp != NULL);

	icalcomponent_remove_component (priv->icalcomp, icalcomp);

	/* Remove it from our mapping */

	e_cal_component_get_uid (comp, &uid);
	obj_data = g_hash_table_lookup (priv->comp_uid_hash, uid);
	if (!obj_data)
		return;

	g_hash_table_remove (priv->comp_uid_hash, uid);

	l = g_list_find (priv->comp, comp);
	g_assert (l != NULL);
	priv->comp = g_list_delete_link (priv->comp, l);

	/* remove the recurrences also */
	g_hash_table_foreach (obj_data->recurrences, (GHFunc) remove_recurrence_cb, cbfile);

	/* Update the set of categories */
	e_cal_component_get_categories_list (comp, &categories);
	e_cal_backend_unref_categories (E_CAL_BACKEND (cbfile), categories);
	e_cal_component_free_categories_list (categories);

	free_object ((gpointer) uid, (gpointer) obj_data, NULL);
}

/* Scans the toplevel VCALENDAR component and stores the objects it finds */
static void
scan_vcalendar (ECalBackendFile *cbfile)
{
	ECalBackendFilePrivate *priv;
	icalcompiter iter;

	priv = cbfile->priv;
	g_assert (priv->icalcomp != NULL);
	g_assert (priv->comp_uid_hash != NULL);

	for (iter = icalcomponent_begin_component (priv->icalcomp, ICAL_ANY_COMPONENT);
	     icalcompiter_deref (&iter) != NULL;
	     icalcompiter_next (&iter)) {
		icalcomponent *icalcomp;
		icalcomponent_kind kind;
		ECalComponent *comp;

		icalcomp = icalcompiter_deref (&iter);
		
		kind = icalcomponent_isa (icalcomp);

		if (!(kind == ICAL_VEVENT_COMPONENT
		      || kind == ICAL_VTODO_COMPONENT
		      || kind == ICAL_VJOURNAL_COMPONENT))
			continue;

		comp = e_cal_component_new ();

		if (!e_cal_component_set_icalcomponent (comp, icalcomp))
			continue;

		add_component (cbfile, comp, FALSE);
	}
}

/* Parses an open iCalendar file and loads it into the backend */
static ECalBackendSyncStatus
open_cal (ECalBackendFile *cbfile, const char *uristr)
{
	ECalBackendFilePrivate *priv;
	icalcomponent *icalcomp;

	priv = cbfile->priv;

	icalcomp = e_cal_util_parse_ics_file (uristr);
	if (!icalcomp)
		return GNOME_Evolution_Calendar_OtherError;

	/* FIXME: should we try to demangle XROOT components and
	 * individual components as well?
	 */

	if (icalcomponent_isa (icalcomp) != ICAL_VCALENDAR_COMPONENT) {
		icalcomponent_free (icalcomp);

		return GNOME_Evolution_Calendar_OtherError;
	}

	priv->icalcomp = icalcomp;

	priv->comp_uid_hash = g_hash_table_new (g_str_hash, g_str_equal);
	scan_vcalendar (cbfile);

	priv->uri = g_strdup (uristr);

	return GNOME_Evolution_Calendar_Success;
}

static ECalBackendSyncStatus
create_cal (ECalBackendFile *cbfile, const char *uristr)
{
	ECalBackendFilePrivate *priv;

	priv = cbfile->priv;

	/* Create the new calendar information */
	priv->icalcomp = e_cal_util_new_top_level ();

	/* Create our internal data */
	priv->comp_uid_hash = g_hash_table_new (g_str_hash, g_str_equal);

	priv->uri = g_strdup (uristr);

	save (cbfile);

	return GNOME_Evolution_Calendar_Success;
}

static char *
get_uri_string (ECalBackend *backend)
{
	ECalBackendFile *cbfile;
	ECalBackendFilePrivate *priv;
	const char *master_uri;
	char *full_uri, *str_uri;
	GnomeVFSURI *uri;

	cbfile = E_CAL_BACKEND_FILE (backend);
	priv = cbfile->priv;
	
	master_uri = e_cal_backend_get_uri (backend);
	g_message (G_STRLOC ": Trying to open %s", master_uri);
	
	/* FIXME Check the error conditions a little more elegantly here */
	if (g_strrstr ("tasks.ics", master_uri) || g_strrstr ("calendar.ics", master_uri)) {
		g_warning (G_STRLOC ": Existing file name %s", master_uri);

		return NULL;
	}
	
	full_uri = g_strdup_printf ("%s%s%s", master_uri, G_DIR_SEPARATOR_S, priv->file_name);
	uri = gnome_vfs_uri_new (full_uri);
	g_free (full_uri);
	
	if (!uri)
		return NULL;

	str_uri = gnome_vfs_uri_to_string (uri,
					   (GNOME_VFS_URI_HIDE_USER_NAME
					    | GNOME_VFS_URI_HIDE_PASSWORD
					    | GNOME_VFS_URI_HIDE_HOST_NAME
					    | GNOME_VFS_URI_HIDE_HOST_PORT
					    | GNOME_VFS_URI_HIDE_TOPLEVEL_METHOD));
	gnome_vfs_uri_unref (uri);

	if (!str_uri || !strlen (str_uri)) {
		g_free (str_uri);

		return NULL;
	}	

	return str_uri;
}

/* Open handler for the file backend */
static ECalBackendSyncStatus
e_cal_backend_file_open (ECalBackendSync *backend, EDataCal *cal, gboolean only_if_exists)
{
	ECalBackendFile *cbfile;
	ECalBackendFilePrivate *priv;
	char *str_uri;
	ECalBackendSyncStatus status;
	
	cbfile = E_CAL_BACKEND_FILE (backend);
	priv = cbfile->priv;

	/* Claim a succesful open if we are already open */
	if (priv->uri && priv->comp_uid_hash)
		return GNOME_Evolution_Calendar_Success;
	
	str_uri = get_uri_string (E_CAL_BACKEND (backend));
	if (!str_uri)
		return GNOME_Evolution_Calendar_OtherError;
	
	if (access (str_uri, R_OK) == 0) {
		status = open_cal (cbfile, str_uri);
		if (access (str_uri, W_OK) != 0)
			priv->read_only = TRUE;
	} else {
		if (only_if_exists)
			status = GNOME_Evolution_Calendar_NoSuchCal;
		else
			status = create_cal (cbfile, str_uri);
	}

	g_free (str_uri);

	return status;
}

static ECalBackendSyncStatus
e_cal_backend_file_remove (ECalBackendSync *backend, EDataCal *cal)
{
	ECalBackendFile *cbfile;
	ECalBackendFilePrivate *priv;
	char *str_uri;
	
	cbfile = E_CAL_BACKEND_FILE (backend);
	priv = cbfile->priv;

	str_uri = get_uri_string (E_CAL_BACKEND (backend));
	if (!str_uri)
		return GNOME_Evolution_Calendar_OtherError;

	if (access (str_uri, W_OK) != 0) {
		g_free (str_uri);

		return GNOME_Evolution_Calendar_PermissionDenied;
	}

	/* FIXME Remove backup file and whole directory too? */
	if (unlink (str_uri) != 0) {
		g_free (str_uri);

		return GNOME_Evolution_Calendar_OtherError;
	}
	
	g_free (str_uri);
	
	return GNOME_Evolution_Calendar_Success;
}

/* is_loaded handler for the file backend */
static gboolean
e_cal_backend_file_is_loaded (ECalBackend *backend)
{
	ECalBackendFile *cbfile;
	ECalBackendFilePrivate *priv;

	cbfile = E_CAL_BACKEND_FILE (backend);
	priv = cbfile->priv;

	return (priv->icalcomp != NULL);
}

/* is_remote handler for the file backend */
static CalMode
e_cal_backend_file_get_mode (ECalBackend *backend)
{
	ECalBackendFile *cbfile;
	ECalBackendFilePrivate *priv;

	cbfile = E_CAL_BACKEND_FILE (backend);
	priv = cbfile->priv;

	return CAL_MODE_LOCAL;	
}

/* Set_mode handler for the file backend */
static void
e_cal_backend_file_set_mode (ECalBackend *backend, CalMode mode)
{
	e_cal_backend_notify_mode (backend,
				 GNOME_Evolution_Calendar_CalListener_MODE_NOT_SUPPORTED,
				 GNOME_Evolution_Calendar_MODE_LOCAL);
	
}

static ECalBackendSyncStatus
e_cal_backend_file_get_default_object (ECalBackendSync *backend, EDataCal *cal, char **object)
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
 	case ICAL_VJOURNAL_COMPONENT:
 		e_cal_component_set_new_vtype (comp, E_CAL_COMPONENT_JOURNAL);
 		break;
 	default:
 		g_object_unref (comp);
		return GNOME_Evolution_Calendar_ObjectNotFound;
 	}
 	
 	*object = e_cal_component_get_as_string (comp);
 	g_object_unref (comp);
 
	return GNOME_Evolution_Calendar_Success;
}

/* Get_object_component handler for the file backend */
static ECalBackendSyncStatus
e_cal_backend_file_get_object (ECalBackendSync *backend, EDataCal *cal, const char *uid, const char *rid, char **object)
{
	ECalBackendFile *cbfile;
	ECalBackendFilePrivate *priv;
	ECalBackendFileObject *obj_data;
	ECalComponent *comp = NULL;
	gboolean free_comp = FALSE;

	cbfile = E_CAL_BACKEND_FILE (backend);
	priv = cbfile->priv;

	g_return_val_if_fail (priv->icalcomp != NULL, GNOME_Evolution_Calendar_InvalidObject);
	g_return_val_if_fail (uid != NULL, GNOME_Evolution_Calendar_ObjectNotFound);
	g_assert (priv->comp_uid_hash != NULL);

	obj_data = g_hash_table_lookup (priv->comp_uid_hash, uid);
	if (!obj_data)
		return GNOME_Evolution_Calendar_ObjectNotFound;

	if (rid && *rid) {
		comp = g_hash_table_lookup (obj_data->recurrences, rid);
		if (!comp) {
			icalcomponent *icalcomp;
			struct icaltimetype itt;

			itt = icaltime_from_string (rid);
			icalcomp = e_cal_util_construct_instance (
				e_cal_component_get_icalcomponent (obj_data->full_object),
				itt);
			if (!icalcomp)
				return GNOME_Evolution_Calendar_ObjectNotFound;

			comp = e_cal_component_new ();
			free_comp = TRUE;
			e_cal_component_set_icalcomponent (comp, icalcomp);
		}
	} else
		comp = obj_data->full_object;
	
	if (!comp)
		return GNOME_Evolution_Calendar_ObjectNotFound;

	*object = e_cal_component_get_as_string (comp);

	if (free_comp)
		g_object_unref (comp);

	return GNOME_Evolution_Calendar_Success;
}

/* Get_timezone_object handler for the file backend */
static ECalBackendSyncStatus
e_cal_backend_file_get_timezone (ECalBackendSync *backend, EDataCal *cal, const char *tzid, char **object)
{
	ECalBackendFile *cbfile;
	ECalBackendFilePrivate *priv;
	icaltimezone *zone;
	icalcomponent *icalcomp;

	cbfile = E_CAL_BACKEND_FILE (backend);
	priv = cbfile->priv;

	g_return_val_if_fail (priv->icalcomp != NULL, GNOME_Evolution_Calendar_NoSuchCal);
	g_return_val_if_fail (tzid != NULL, GNOME_Evolution_Calendar_ObjectNotFound);

	if (!strcmp (tzid, "UTC")) {
		zone = icaltimezone_get_utc_timezone ();
	} else {
		zone = icalcomponent_get_timezone (priv->icalcomp, tzid);
		if (!zone) {
			zone = icaltimezone_get_builtin_timezone_from_tzid (tzid);
			if (!zone)
				return GNOME_Evolution_Calendar_ObjectNotFound;
		}
	}
	
	icalcomp = icaltimezone_get_component (zone);
	if (!icalcomp)
		return GNOME_Evolution_Calendar_InvalidObject;

	*object = g_strdup (icalcomponent_as_ical_string (icalcomp));

	return GNOME_Evolution_Calendar_Success;
}

/* Add_timezone handler for the file backend */
static ECalBackendSyncStatus
e_cal_backend_file_add_timezone (ECalBackendSync *backend, EDataCal *cal, const char *tzobj)
{
	icalcomponent *tz_comp;
	ECalBackendFile *cbfile;
	ECalBackendFilePrivate *priv;

	cbfile = (ECalBackendFile *) backend;

	g_return_val_if_fail (E_IS_CAL_BACKEND_FILE (cbfile), GNOME_Evolution_Calendar_OtherError);
	g_return_val_if_fail (tzobj != NULL, GNOME_Evolution_Calendar_OtherError);

	priv = cbfile->priv;

	tz_comp = icalparser_parse_string (tzobj);
	if (!tz_comp)
		return GNOME_Evolution_Calendar_InvalidObject;

	if (icalcomponent_isa (tz_comp) == ICAL_VTIMEZONE_COMPONENT) {
		icaltimezone *zone;

		zone = icaltimezone_new ();
		icaltimezone_set_component (zone, tz_comp);
		if (!icalcomponent_get_timezone (priv->icalcomp,
						 icaltimezone_get_tzid (zone))) {
			icalcomponent_add_component (priv->icalcomp, tz_comp);
			save (cbfile);
		}

		icaltimezone_free (zone, 1);
	}

	return GNOME_Evolution_Calendar_Success;
}


static ECalBackendSyncStatus
e_cal_backend_file_set_default_timezone (ECalBackendSync *backend, EDataCal *cal, const char *tzid)
{
	ECalBackendFile *cbfile;
	ECalBackendFilePrivate *priv;
	icaltimezone *zone;

	cbfile = E_CAL_BACKEND_FILE (backend);
	priv = cbfile->priv;

	g_return_val_if_fail (priv->icalcomp != NULL, GNOME_Evolution_Calendar_NoSuchCal);

	/* Look up the VTIMEZONE in our icalcomponent. */
	zone = icalcomponent_get_timezone (priv->icalcomp, tzid);
	if (!zone)
		return GNOME_Evolution_Calendar_ObjectNotFound;

	/* Set the default timezone to it. */
	priv->default_zone = zone;

	return GNOME_Evolution_Calendar_Success;
}

typedef struct {
	GList *obj_list;
	gboolean search_needed;
	const char *query;
	ECalBackendSExp *obj_sexp;
	ECalBackend *backend;
	icaltimezone *default_zone;
} MatchObjectData;

static void
match_recurrence_sexp (gpointer key, gpointer value, gpointer data)
{
	ECalComponent *comp = value;
	MatchObjectData *match_data = data;

	if ((!match_data->search_needed) ||
	    (e_cal_backend_sexp_match_comp (match_data->obj_sexp, comp, match_data->backend))) {
		match_data->obj_list = g_list_append (match_data->obj_list,
						      e_cal_component_get_as_string (comp));
	}
}

static void
match_object_sexp (gpointer key, gpointer value, gpointer data)
{
	ECalBackendFileObject *obj_data = value;
	MatchObjectData *match_data = data;

	if ((!match_data->search_needed) ||
	    (e_cal_backend_sexp_match_comp (match_data->obj_sexp, obj_data->full_object, match_data->backend))) {
		match_data->obj_list = g_list_append (match_data->obj_list,
						      e_cal_component_get_as_string (obj_data->full_object));

		/* match also recurrences */
		g_hash_table_foreach (obj_data->recurrences,
				      (GHFunc) match_recurrence_sexp,
				      match_data);
	}
}

/* Get_objects_in_range handler for the file backend */
static ECalBackendSyncStatus
e_cal_backend_file_get_object_list (ECalBackendSync *backend, EDataCal *cal, const char *sexp, GList **objects)
{
	ECalBackendFile *cbfile;
	ECalBackendFilePrivate *priv;
	MatchObjectData match_data;

	cbfile = E_CAL_BACKEND_FILE (backend);
	priv = cbfile->priv;

	g_message (G_STRLOC ": Getting object list (%s)", sexp);

	match_data.search_needed = TRUE;
	match_data.query = sexp;
	match_data.obj_list = NULL;
	match_data.backend = E_CAL_BACKEND (backend);
	match_data.default_zone = priv->default_zone;

	if (!strcmp (sexp, "#t"))
		match_data.search_needed = FALSE;

	match_data.obj_sexp = e_cal_backend_sexp_new (sexp);
	if (!match_data.obj_sexp)
		return GNOME_Evolution_Calendar_InvalidQuery;

	g_hash_table_foreach (priv->comp_uid_hash, (GHFunc) match_object_sexp, &match_data);

	*objects = match_data.obj_list;
	
	return GNOME_Evolution_Calendar_Success;	
}

/* get_query handler for the file backend */
static void
e_cal_backend_file_start_query (ECalBackend *backend, EDataCalView *query)
{
	ECalBackendFile *cbfile;
	ECalBackendFilePrivate *priv;
	MatchObjectData match_data;

	cbfile = E_CAL_BACKEND_FILE (backend);
	priv = cbfile->priv;

	g_message (G_STRLOC ": Starting query (%s)", e_data_cal_view_get_text (query));

	/* try to match all currently existing objects */
	match_data.search_needed = TRUE;
	match_data.query = e_data_cal_view_get_text (query);
	match_data.obj_list = NULL;
	match_data.backend = backend;
	match_data.default_zone = priv->default_zone;

	if (!strcmp (match_data.query, "#t"))
		match_data.search_needed = FALSE;

	match_data.obj_sexp = e_data_cal_view_get_object_sexp (query);
	if (!match_data.obj_sexp) {
		e_data_cal_view_notify_done (query, GNOME_Evolution_Calendar_InvalidQuery);
		return;
	}

	g_hash_table_foreach (priv->comp_uid_hash, (GHFunc) match_object_sexp, &match_data);

	/* notify listeners of all objects */
	if (match_data.obj_list) {
		e_data_cal_view_notify_objects_added (query, (const GList *) match_data.obj_list);

		/* free memory */
		g_list_foreach (match_data.obj_list, (GFunc) g_free, NULL);
		g_list_free (match_data.obj_list);
	}

	e_data_cal_view_notify_done (query, GNOME_Evolution_Calendar_Success);
}

static gboolean
free_busy_instance (ECalComponent *comp,
		    time_t        instance_start,
		    time_t        instance_end,
		    gpointer      data)
{
	icalcomponent *vfb = data;
	icalproperty *prop;
	icalparameter *param;
	struct icalperiodtype ipt;
	icaltimezone *utc_zone;

	utc_zone = icaltimezone_get_utc_timezone ();

	ipt.start = icaltime_from_timet_with_zone (instance_start, FALSE, utc_zone);
	ipt.end = icaltime_from_timet_with_zone (instance_end, FALSE, utc_zone);
	ipt.duration = icaldurationtype_null_duration ();
	
        /* add busy information to the vfb component */
	prop = icalproperty_new (ICAL_FREEBUSY_PROPERTY);
	icalproperty_set_freebusy (prop, ipt);
	
	param = icalparameter_new_fbtype (ICAL_FBTYPE_BUSY);
	icalproperty_add_parameter (prop, param);
	
	icalcomponent_add_property (vfb, prop);

	return TRUE;
}

static icalcomponent *
create_user_free_busy (ECalBackendFile *cbfile, const char *address, const char *cn,
		       time_t start, time_t end)
{	
	ECalBackendFilePrivate *priv;
	GList *l;
	icalcomponent *vfb;
	icaltimezone *utc_zone;
	ECalBackendSExp *obj_sexp;
	char *query;
	
	priv = cbfile->priv;

	/* create the (unique) VFREEBUSY object that we'll return */
	vfb = icalcomponent_new_vfreebusy ();
	if (address != NULL) {
		icalproperty *prop;
		icalparameter *param;
		
		prop = icalproperty_new_organizer (address);
		if (prop != NULL && cn != NULL) {
			param = icalparameter_new_cn (cn);
			icalproperty_add_parameter (prop, param);			
		}
		if (prop != NULL)
			icalcomponent_add_property (vfb, prop);		
	}
	utc_zone = icaltimezone_get_utc_timezone ();
	icalcomponent_set_dtstart (vfb, icaltime_from_timet_with_zone (start, FALSE, utc_zone));
	icalcomponent_set_dtend (vfb, icaltime_from_timet_with_zone (end, FALSE, utc_zone));

	/* add all objects in the given interval */
	query = g_strdup_printf ("occur-in-time-range? %lu %lu", start, end);
	obj_sexp = e_cal_backend_sexp_new (query);
	g_free (query);

	if (!obj_sexp)
		return vfb;

	for (l = priv->comp; l; l = l->next) {
		ECalComponent *comp = l->data;
		icalcomponent *icalcomp, *vcalendar_comp;
		icalproperty *prop;
		
		icalcomp = e_cal_component_get_icalcomponent (comp);
		if (!icalcomp)
			continue;

		/* If the event is TRANSPARENT, skip it. */
		prop = icalcomponent_get_first_property (icalcomp,
							 ICAL_TRANSP_PROPERTY);
		if (prop) {
			icalproperty_transp transp_val = icalproperty_get_transp (prop);
			if (transp_val == ICAL_TRANSP_TRANSPARENT ||
			    transp_val == ICAL_TRANSP_TRANSPARENTNOCONFLICT)
				continue;
		}
	
		if (!e_cal_backend_sexp_match_comp (obj_sexp, l->data, E_CAL_BACKEND (cbfile)))
			continue;
		
		vcalendar_comp = icalcomponent_get_parent (icalcomp);
		e_cal_recur_generate_instances (comp, start, end,
						free_busy_instance,
						vfb,
						resolve_tzid,
						vcalendar_comp,
						priv->default_zone);
	}

	return vfb;	
}

/* Get_free_busy handler for the file backend */
static ECalBackendSyncStatus
e_cal_backend_file_get_free_busy (ECalBackendSync *backend, EDataCal *cal, GList *users,
				time_t start, time_t end, GList **freebusy)
{
	ECalBackendFile *cbfile;
	ECalBackendFilePrivate *priv;
	gchar *address, *name;	
	icalcomponent *vfb;
	char *calobj;
	GList *l;

	cbfile = E_CAL_BACKEND_FILE (backend);
	priv = cbfile->priv;

	g_return_val_if_fail (priv->icalcomp != NULL, GNOME_Evolution_Calendar_NoSuchCal);
	g_return_val_if_fail (start != -1 && end != -1, GNOME_Evolution_Calendar_InvalidRange);
	g_return_val_if_fail (start <= end, GNOME_Evolution_Calendar_InvalidRange);

	*freebusy = NULL;
	
	if (users == NULL) {
		if (e_cal_backend_mail_account_get_default (&address, &name)) {
			vfb = create_user_free_busy (cbfile, address, name, start, end);
			calobj = icalcomponent_as_ical_string (vfb);
			*freebusy = g_list_append (*freebusy, g_strdup (calobj));
			icalcomponent_free (vfb);
			g_free (address);
			g_free (name);
		}		
	} else {
		for (l = users; l != NULL; l = l->next ) {
			address = l->data;			
			if (e_cal_backend_mail_account_is_valid (address, &name)) {
				vfb = create_user_free_busy (cbfile, address, name, start, end);
				calobj = icalcomponent_as_ical_string (vfb);
				*freebusy = g_list_append (*freebusy, g_strdup (calobj));
				icalcomponent_free (vfb);
				g_free (name);
			}
		}		
	}

	return GNOME_Evolution_Calendar_Success;
}

typedef struct 
{
	ECalBackendFile *backend;
	icalcomponent_kind kind;
	GList *deletes;
	EXmlHash *ehash;
} ECalBackendFileComputeChangesData;

static void
e_cal_backend_file_compute_changes_foreach_key (const char *key, gpointer data)
{
	ECalBackendFileComputeChangesData *be_data = data;
	
	if (!lookup_component (be_data->backend, key)) {
		ECalComponent *comp;

		comp = e_cal_component_new ();
		if (be_data->kind == ICAL_VTODO_COMPONENT)
			e_cal_component_set_new_vtype (comp, E_CAL_COMPONENT_TODO);
		else
			e_cal_component_set_new_vtype (comp, E_CAL_COMPONENT_EVENT);

		e_cal_component_set_uid (comp, key);
		be_data->deletes = g_list_prepend (be_data->deletes, e_cal_component_get_as_string (comp));

		e_xmlhash_remove (be_data->ehash, key);
 	}
}

static ECalBackendSyncStatus
e_cal_backend_file_compute_changes (ECalBackendFile *cbfile, const char *change_id,
				  GList **adds, GList **modifies, GList **deletes)
{
	ECalBackendFilePrivate *priv;
	char    *filename;
	EXmlHash *ehash;
	ECalBackendFileComputeChangesData be_data;
	GList *i;

	priv = cbfile->priv;

	/* FIXME Will this always work? */
	filename = g_strdup_printf ("%s/%s.db", priv->uri, change_id);
	ehash = e_xmlhash_new (filename);
	g_free (filename);
	
	/* Calculate adds and modifies */
	for (i = priv->comp; i != NULL; i = i->next) {
		const char *uid;
		char *calobj;

		e_cal_component_get_uid (i->data, &uid);
		calobj = e_cal_component_get_as_string (i->data);

		g_assert (calobj != NULL);

		/* check what type of change has occurred, if any */
		switch (e_xmlhash_compare (ehash, uid, calobj)) {
		case E_XMLHASH_STATUS_SAME:
			break;
		case E_XMLHASH_STATUS_NOT_FOUND:
			*adds = g_list_prepend (*adds, g_strdup (calobj));
			e_xmlhash_add (ehash, uid, calobj);
			break;
		case E_XMLHASH_STATUS_DIFFERENT:
			*modifies = g_list_prepend (*modifies, g_strdup (calobj));
			e_xmlhash_add (ehash, uid, calobj);
			break;
		}

		g_free (calobj);
	}

	/* Calculate deletions */
	be_data.backend = cbfile;
	be_data.kind = e_cal_backend_get_kind (E_CAL_BACKEND (cbfile));
	be_data.deletes = NULL;
	be_data.ehash = ehash;
   	e_xmlhash_foreach_key (ehash, (EXmlHashFunc)e_cal_backend_file_compute_changes_foreach_key, &be_data);

	*deletes = be_data.deletes;

	e_xmlhash_write (ehash);
  	e_xmlhash_destroy (ehash);
	
	return GNOME_Evolution_Calendar_Success;
}

/* Get_changes handler for the file backend */
static ECalBackendSyncStatus
e_cal_backend_file_get_changes (ECalBackendSync *backend, EDataCal *cal, const char *change_id,
			      GList **adds, GList **modifies, GList **deletes)
{
	ECalBackendFile *cbfile;
	ECalBackendFilePrivate *priv;

	cbfile = E_CAL_BACKEND_FILE (backend);
	priv = cbfile->priv;

	g_return_val_if_fail (priv->icalcomp != NULL, GNOME_Evolution_Calendar_NoSuchCal);
	g_return_val_if_fail (change_id != NULL, GNOME_Evolution_Calendar_ObjectNotFound);

	return e_cal_backend_file_compute_changes (cbfile, change_id, adds, modifies, deletes);
}

/* Discard_alarm handler for the file backend */
static ECalBackendSyncStatus
e_cal_backend_file_discard_alarm (ECalBackendSync *backend, EDataCal *cal, const char *uid, const char *auid)
{
	/* we just do nothing with the alarm */
	return GNOME_Evolution_Calendar_Success;
}

static ECalBackendSyncStatus
e_cal_backend_file_create_object (ECalBackendSync *backend, EDataCal *cal, const char *calobj, char **uid)
{
	ECalBackendFile *cbfile;
	ECalBackendFilePrivate *priv;
	icalcomponent *icalcomp;
	icalcomponent_kind kind;
	ECalComponent *comp;
	const char *comp_uid;
	struct icaltimetype current;
	
	cbfile = E_CAL_BACKEND_FILE (backend);
	priv = cbfile->priv;

	g_return_val_if_fail (priv->icalcomp != NULL, GNOME_Evolution_Calendar_NoSuchCal);
	g_return_val_if_fail (calobj != NULL, GNOME_Evolution_Calendar_ObjectNotFound);

	icalcomp = icalparser_parse_string ((char *) calobj);
	if (!icalcomp)
		return GNOME_Evolution_Calendar_InvalidObject;

	/* FIXME Check kind with the parent */
	kind = icalcomponent_isa (icalcomp);
	if (kind != ICAL_VEVENT_COMPONENT && kind != ICAL_VTODO_COMPONENT) {
		icalcomponent_free (icalcomp);
		return GNOME_Evolution_Calendar_InvalidObject;
	}

	/* Get the UID */
	comp_uid = icalcomponent_get_uid (icalcomp);
	
	/* check the object is not in our cache */
	if (lookup_component (cbfile, comp_uid)) {
		icalcomponent_free (icalcomp);
		return GNOME_Evolution_Calendar_CardIdAlreadyExists;
	}

	/* Create the cal component */
	comp = e_cal_component_new ();
	e_cal_component_set_icalcomponent (comp, icalcomp);

	/* Set the created and last modified times on the component */
	current = icaltime_from_timet (time (NULL), 0);
	e_cal_component_set_created (comp, &current);
	e_cal_component_set_last_modified (comp, &current);

	/* Add the object */
	add_component (cbfile, comp, TRUE);

	/* Save the file */
	save (cbfile);

	/* Return the UID */
	if (uid)
		*uid = g_strdup (comp_uid);

	return GNOME_Evolution_Calendar_Success;
}

static ECalBackendSyncStatus
e_cal_backend_file_modify_object (ECalBackendSync *backend, EDataCal *cal, const char *calobj, 
				CalObjModType mod, char **old_object)
{
	ECalBackendFile *cbfile;
	ECalBackendFilePrivate *priv;
	icalcomponent *icalcomp;
	icalcomponent_kind kind;
	const char *comp_uid, *rid;
	char *real_rid;
	ECalComponent *comp, *recurrence;
	ECalBackendFileObject *obj_data;
	struct icaltimetype current;

	cbfile = E_CAL_BACKEND_FILE (backend);
	priv = cbfile->priv;
		
	g_return_val_if_fail (priv->icalcomp != NULL, GNOME_Evolution_Calendar_NoSuchCal);
	g_return_val_if_fail (calobj != NULL, GNOME_Evolution_Calendar_ObjectNotFound);

	icalcomp = icalparser_parse_string ((char *) calobj);
	if (!icalcomp)
		return GNOME_Evolution_Calendar_InvalidObject;

	/* check kind with the parent */
	kind = icalcomponent_isa (icalcomp);
	if (kind != ICAL_VEVENT_COMPONENT && kind != ICAL_VTODO_COMPONENT) {
		icalcomponent_free (icalcomp);
		return GNOME_Evolution_Calendar_InvalidObject;
	}

	/* Get the uid */
	comp_uid = icalcomponent_get_uid (icalcomp);

	/* Get the object from our cache */
	if (!(obj_data = g_hash_table_lookup (priv->comp_uid_hash, comp_uid))) {
		icalcomponent_free (icalcomp);
		return GNOME_Evolution_Calendar_ObjectNotFound;
	}

	/* Create the cal component */
	comp = e_cal_component_new ();
	e_cal_component_set_icalcomponent (comp, icalcomp);
	
	/* Set the last modified time on the component */
	current = icaltime_from_timet (time (NULL), 0);
	e_cal_component_set_last_modified (comp, &current);

	/* handle mod_type */
	switch (mod) {
	case CALOBJ_MOD_THIS :
		rid = get_rid_string (comp);
		if (!rid || !*rid) {
			g_object_unref (comp);
			return GNOME_Evolution_Calendar_ObjectNotFound;
		}

		if (g_hash_table_lookup_extended (obj_data->recurrences, rid,
						  &real_rid, &recurrence)) {
			/* remove the component from our data */
			icalcomponent_remove_component (priv->icalcomp,
							e_cal_component_get_icalcomponent (recurrence));
			priv->comp = g_list_remove (priv->comp, recurrence);
			g_hash_table_remove (obj_data->recurrences, rid);

			/* free memory */
			g_free (real_rid);
			g_object_unref (recurrence);
		} else {
			char *old, *new;

			old = e_cal_component_get_as_string (obj_data->full_object);

			e_cal_util_remove_instances (e_cal_component_get_icalcomponent (obj_data->full_object),
						     get_rid_icaltime (comp),
						     mod);

			new = e_cal_component_get_as_string (obj_data->full_object);

			e_cal_backend_notify_object_modified (E_CAL_BACKEND (backend), old, new);

			g_free (old);
			g_free (new);
		}

		/* add the detached instance */
		g_hash_table_insert (obj_data->recurrences, g_strdup (get_rid_string (comp)), comp);
		break;
	case CALOBJ_MOD_THISANDPRIOR :
		break;
	case CALOBJ_MOD_THISANDFUTURE :
		break;
	case CALOBJ_MOD_ALL :
		/* in this case, we blow away all recurrences, and start over
		   with a clean component */
		/* Remove the old version */
		remove_component (cbfile, obj_data->full_object);

		/* Add the new object */
		add_component (cbfile, comp, TRUE);
		break;
	}

	save (cbfile);

	if (old_object)
		*old_object = e_cal_component_get_as_string (comp);

	return GNOME_Evolution_Calendar_Success;
}

static void
remove_instance (ECalBackendFile *cbfile, ECalBackendFileObject *obj_data, const char *rid)
{
	char *hash_rid;
	ECalComponent *comp;
	GSList *categories;

	if (!rid || !*rid)
		return;

	if (g_hash_table_lookup_extended (obj_data->recurrences, rid, &hash_rid, &comp)) {
		/* remove the component from our data */
		icalcomponent_remove_component (cbfile->priv->icalcomp,
						e_cal_component_get_icalcomponent (comp));
		cbfile->priv->comp = g_list_remove (cbfile->priv->comp, comp);
		g_hash_table_remove (obj_data->recurrences, rid);

		/* update the set of categories */
		e_cal_component_get_categories_list (comp, &categories);
		e_cal_backend_unref_categories (E_CAL_BACKEND (cbfile), categories);
		e_cal_component_free_categories_list (categories);

		/* free memory */
		g_free (hash_rid);
		g_object_unref (comp);

		return;
	}

	/* remove the component from our data, temporarily */
	icalcomponent_remove_component (cbfile->priv->icalcomp,
					e_cal_component_get_icalcomponent (obj_data->full_object));
	cbfile->priv->comp = g_list_remove (cbfile->priv->comp, obj_data->full_object);

	e_cal_util_remove_instances (e_cal_component_get_icalcomponent (obj_data->full_object),
				     icaltime_from_string (rid), CALOBJ_MOD_THIS);

	/* add the modified object to the beginning of the list, 
	   so that it's always before any detached instance we
	   might have */
	cbfile->priv->comp = g_list_prepend (cbfile->priv->comp, obj_data->full_object);
}

typedef struct {
	ECalBackendFile *cbfile;
	ECalBackendFileObject *obj_data;
	const char *rid;
	CalObjModType mod;
} RemoveRecurrenceData;

static gboolean
remove_object_instance_cb (gpointer key, gpointer value, gpointer user_data)
{
	time_t fromtt, instancett;
	GSList *categories;
	char *rid = key;
	ECalComponent *instance = value;
	RemoveRecurrenceData *rrdata = user_data;

	fromtt = icaltime_as_timet (icaltime_from_string (rrdata->rid));
	instancett = icaltime_as_timet (get_rid_icaltime (instance));

	if (fromtt > 0 && instancett > 0) {
		if ((rrdata->mod == CALOBJ_MOD_THISANDPRIOR && instancett <= fromtt) ||
		    (rrdata->mod == CALOBJ_MOD_THISANDFUTURE && instancett >= fromtt)) {
			/* remove the component from our data */
			icalcomponent_remove_component (rrdata->cbfile->priv->icalcomp,
							e_cal_component_get_icalcomponent (instance));
			rrdata->cbfile->priv->comp = g_list_remove (rrdata->cbfile->priv->comp, instance);

			/* update the set of categories */
			e_cal_component_get_categories_list (instance, &categories);
			e_cal_backend_unref_categories (E_CAL_BACKEND (rrdata->cbfile), categories);
			e_cal_component_free_categories_list (categories);

			/* free memory */
			g_free (rid);
			g_object_unref (instance);

			return TRUE;
		}
	}

	return FALSE;
}

/* Remove_object handler for the file backend */
static ECalBackendSyncStatus
e_cal_backend_file_remove_object (ECalBackendSync *backend, EDataCal *cal,
				const char *uid, const char *rid,
				CalObjModType mod, char **object)
{
	ECalBackendFile *cbfile;
	ECalBackendFilePrivate *priv;
	ECalBackendFileObject *obj_data;
	ECalComponent *comp;
	GSList *categories;
	RemoveRecurrenceData rrdata;

	cbfile = E_CAL_BACKEND_FILE (backend);
	priv = cbfile->priv;

	g_return_val_if_fail (priv->icalcomp != NULL, GNOME_Evolution_Calendar_NoSuchCal);
	g_return_val_if_fail (uid != NULL, GNOME_Evolution_Calendar_ObjectNotFound);

	obj_data = g_hash_table_lookup (priv->comp_uid_hash, uid);
	if (!obj_data)
		return GNOME_Evolution_Calendar_ObjectNotFound;

	comp = obj_data->full_object;

	switch (mod) {
	case CALOBJ_MOD_ALL :
		*object = e_cal_component_get_as_string (comp);
		remove_component (cbfile, comp);
		break;
	case CALOBJ_MOD_THIS :
		if (!rid || !*rid)
			return GNOME_Evolution_Calendar_ObjectNotFound;

		remove_instance (cbfile, obj_data, rid);
		break;
	case CALOBJ_MOD_THISANDPRIOR :
	case CALOBJ_MOD_THISANDFUTURE :
		if (!rid || !*rid)
			return GNOME_Evolution_Calendar_ObjectNotFound;

		/* remove the component from our data, temporarily */
		icalcomponent_remove_component (priv->icalcomp,
						e_cal_component_get_icalcomponent (comp));
		priv->comp = g_list_remove (priv->comp, comp);

		e_cal_util_remove_instances (e_cal_component_get_icalcomponent (comp),
					   icaltime_from_string (rid), mod);

		/* now remove all detached instances */
		rrdata.cbfile = cbfile;
		rrdata.obj_data = obj_data;
		rrdata.rid = rid;
		rrdata.mod = mod;
		g_hash_table_foreach_remove (obj_data->recurrences, (GHRFunc) remove_object_instance_cb, &rrdata);

		/* add the modified object to the beginning of the list, 
		   so that it's always before any detached instance we
		   might have */
		priv->comp = g_list_prepend (priv->comp, comp);
		break;
	}

	save (cbfile);

	return GNOME_Evolution_Calendar_Success;
}

static gboolean
cancel_received_object (ECalBackendFile *cbfile, icalcomponent *icalcomp)
{
	ECalComponent *old_comp;

	/* Find the old version of the component. */
	old_comp = lookup_component (cbfile, icalcomponent_get_uid (icalcomp));
	if (!old_comp)
		return FALSE;

	/* And remove it */
	remove_component (cbfile, old_comp);

	return TRUE;
}

typedef struct {
	GHashTable *zones;
	
	gboolean found;
} ECalBackendFileTzidData;

static void
check_tzids (icalparameter *param, void *data)
{
	ECalBackendFileTzidData *tzdata = data;
	const char *tzid;
	
	tzid = icalparameter_get_tzid (param);
	if (!tzid || g_hash_table_lookup (tzdata->zones, tzid))
		tzdata->found = FALSE;
}

/* Update_objects handler for the file backend. */
static ECalBackendSyncStatus
e_cal_backend_file_receive_objects (ECalBackendSync *backend, EDataCal *cal, const char *calobj)
{
	ECalBackendFile *cbfile;
	ECalBackendFilePrivate *priv;
	icalcomponent *toplevel_comp, *icalcomp = NULL;
	icalcomponent_kind kind;
	icalproperty_method method;
	icalcomponent *subcomp;
	GList *comps, *l;
	ECalBackendFileTzidData tzdata;
	ECalBackendSyncStatus status = GNOME_Evolution_Calendar_Success;

	cbfile = E_CAL_BACKEND_FILE (backend);
	priv = cbfile->priv;

	g_return_val_if_fail (priv->icalcomp != NULL, GNOME_Evolution_Calendar_InvalidObject);
	g_return_val_if_fail (calobj != NULL, GNOME_Evolution_Calendar_InvalidObject);

	/* Pull the component from the string and ensure that it is sane */
	toplevel_comp = icalparser_parse_string ((char *) calobj);
	if (!toplevel_comp)
		return GNOME_Evolution_Calendar_InvalidObject;

	kind = icalcomponent_isa (toplevel_comp);
	if (kind != ICAL_VCALENDAR_COMPONENT) {
		/* If its not a VCALENDAR, make it one to simplify below */
		icalcomp = toplevel_comp;
		toplevel_comp = e_cal_util_new_top_level ();
		icalcomponent_add_component (toplevel_comp, icalcomp);	
	}

	method = icalcomponent_get_method (toplevel_comp);

	/* Build a list of timezones so we can make sure all the objects have valid info */
	tzdata.zones = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

	subcomp = icalcomponent_get_first_component (toplevel_comp, ICAL_VTIMEZONE_COMPONENT);
	while (subcomp) {
		icaltimezone *zone;
		
		zone = icaltimezone_new ();
		if (icaltimezone_set_component (zone, subcomp))
			g_hash_table_insert (tzdata.zones, g_strdup (icaltimezone_get_tzid (zone)), NULL);
		
		subcomp = icalcomponent_get_next_component (toplevel_comp, ICAL_VTIMEZONE_COMPONENT);
	}	

	/* First we make sure all the components are usuable */
	comps = NULL;
	subcomp = icalcomponent_get_first_component (toplevel_comp, ICAL_ANY_COMPONENT);
	while (subcomp) {
		/* We ignore anything except VEVENT, VTODO and VJOURNAL
		   components. */
		icalcomponent_kind child_kind = icalcomponent_isa (subcomp);

		switch (child_kind) {
		case ICAL_VEVENT_COMPONENT:
		case ICAL_VTODO_COMPONENT:
		case ICAL_VJOURNAL_COMPONENT:
			tzdata.found = TRUE;
			icalcomponent_foreach_tzid (subcomp, check_tzids, &tzdata);
			
			if (!tzdata.found) {
				status = GNOME_Evolution_Calendar_InvalidObject;
				goto error;
			}

			if (!icalcomponent_get_uid (subcomp)) {
				status = GNOME_Evolution_Calendar_InvalidObject;
				goto error;
			}
		
			comps = g_list_prepend (comps, subcomp);
			break;
		default:
			/* Ignore it */
			break;
		}

		subcomp = icalcomponent_get_next_component (toplevel_comp, ICAL_ANY_COMPONENT);
	}

	/* Now we manipulate the components we care about */
	for (l = comps; l; l = l->next) {
		subcomp = l->data;
		
		switch (method) {
		case ICAL_METHOD_PUBLISH:
		case ICAL_METHOD_REQUEST:
			/* FIXME Need to see the new create/modify stuff before we set this up */
			break;			
		case ICAL_METHOD_REPLY:
			/* FIXME Update the status of the user, if we are the organizer */
			break;
		case ICAL_METHOD_ADD:
			/* FIXME This should be doable once all the recurid stuff is done */
			break;
		case ICAL_METHOD_COUNTER:
			status = GNOME_Evolution_Calendar_UnsupportedMethod;
			goto error;
			break;			
		case ICAL_METHOD_DECLINECOUNTER:			
			status = GNOME_Evolution_Calendar_UnsupportedMethod;
			goto error;
			break;
		case ICAL_METHOD_CANCEL:
			/* FIXME Do we need to remove the subcomp so it isn't merged? */
			if (cancel_received_object (cbfile, subcomp)) {
				const char *calobj = icalcomponent_as_ical_string (subcomp);
				e_cal_backend_notify_object_removed (E_CAL_BACKEND (backend), icalcomponent_get_uid (subcomp), calobj);
			}
			break;
		default:
			status = GNOME_Evolution_Calendar_UnsupportedMethod;
			goto error;
		}
	}
	g_list_free (comps);
	
	/* Merge the iCalendar components with our existing VCALENDAR,
	   resolving any conflicting TZIDs. */
	icalcomponent_merge_component (priv->icalcomp, toplevel_comp);

	save (cbfile);

 error:
	g_hash_table_destroy (tzdata.zones);
	
	return status;
}

static ECalBackendSyncStatus
e_cal_backend_file_send_objects (ECalBackendSync *backend, EDataCal *cal, const char *calobj)
{
	/* FIXME Put in a util routine to send stuff via email */
	
	return GNOME_Evolution_Calendar_Success;
}

static icaltimezone *
e_cal_backend_file_internal_get_default_timezone (ECalBackend *backend)
{
	ECalBackendFile *cbfile;
	ECalBackendFilePrivate *priv;

	cbfile = E_CAL_BACKEND_FILE (backend);
	priv = cbfile->priv;

	g_return_val_if_fail (priv->icalcomp != NULL, NULL);

	return priv->default_zone;
}

static icaltimezone *
e_cal_backend_file_internal_get_timezone (ECalBackend *backend, const char *tzid)
{
	ECalBackendFile *cbfile;
	ECalBackendFilePrivate *priv;
	icaltimezone *zone;

	cbfile = E_CAL_BACKEND_FILE (backend);
	priv = cbfile->priv;

	g_return_val_if_fail (priv->icalcomp != NULL, NULL);

	if (!strcmp (tzid, "UTC"))
	        zone = icaltimezone_get_utc_timezone ();
	else {
		zone = icalcomponent_get_timezone (priv->icalcomp, tzid);
		if (!zone)
			zone = icaltimezone_get_builtin_timezone_from_tzid (tzid);
	}

	return zone;
}

/* Object initialization function for the file backend */
static void
e_cal_backend_file_init (ECalBackendFile *cbfile, ECalBackendFileClass *class)
{
	ECalBackendFilePrivate *priv;

	priv = g_new0 (ECalBackendFilePrivate, 1);
	cbfile->priv = priv;

	priv->uri = NULL;
	priv->file_name = g_strdup ("calendar.ics");
	priv->read_only = FALSE;
	priv->icalcomp = NULL;
	priv->comp_uid_hash = NULL;
	priv->comp = NULL;

	/* The timezone defaults to UTC. */
	priv->default_zone = icaltimezone_get_utc_timezone ();
}

/* Class initialization function for the file backend */
static void
e_cal_backend_file_class_init (ECalBackendFileClass *class)
{
	GObjectClass *object_class;
	ECalBackendClass *backend_class;
	ECalBackendSyncClass *sync_class;

	object_class = (GObjectClass *) class;
	backend_class = (ECalBackendClass *) class;
	sync_class = (ECalBackendSyncClass *) class;

	parent_class = (ECalBackendSyncClass *) g_type_class_peek_parent (class);

	object_class->dispose = e_cal_backend_file_dispose;
	object_class->finalize = e_cal_backend_file_finalize;

	sync_class->is_read_only_sync = e_cal_backend_file_is_read_only;
	sync_class->get_cal_address_sync = e_cal_backend_file_get_cal_address;
 	sync_class->get_alarm_email_address_sync = e_cal_backend_file_get_alarm_email_address;
 	sync_class->get_ldap_attribute_sync = e_cal_backend_file_get_ldap_attribute;
 	sync_class->get_static_capabilities_sync = e_cal_backend_file_get_static_capabilities;
	sync_class->open_sync = e_cal_backend_file_open;
	sync_class->remove_sync = e_cal_backend_file_remove;
	sync_class->create_object_sync = e_cal_backend_file_create_object;
	sync_class->modify_object_sync = e_cal_backend_file_modify_object;
	sync_class->remove_object_sync = e_cal_backend_file_remove_object;
	sync_class->discard_alarm_sync = e_cal_backend_file_discard_alarm;
	sync_class->receive_objects_sync = e_cal_backend_file_receive_objects;
	sync_class->send_objects_sync = e_cal_backend_file_send_objects;
 	sync_class->get_default_object_sync = e_cal_backend_file_get_default_object;
	sync_class->get_object_sync = e_cal_backend_file_get_object;
	sync_class->get_object_list_sync = e_cal_backend_file_get_object_list;
	sync_class->get_timezone_sync = e_cal_backend_file_get_timezone;
	sync_class->add_timezone_sync = e_cal_backend_file_add_timezone;
	sync_class->set_default_timezone_sync = e_cal_backend_file_set_default_timezone;
	sync_class->get_freebusy_sync = e_cal_backend_file_get_free_busy;
	sync_class->get_changes_sync = e_cal_backend_file_get_changes;

	backend_class->is_loaded = e_cal_backend_file_is_loaded;
	backend_class->start_query = e_cal_backend_file_start_query;
	backend_class->get_mode = e_cal_backend_file_get_mode;
	backend_class->set_mode = e_cal_backend_file_set_mode;

	backend_class->internal_get_default_timezone = e_cal_backend_file_internal_get_default_timezone;
	backend_class->internal_get_timezone = e_cal_backend_file_internal_get_timezone;
}


/**
 * e_cal_backend_file_get_type:
 * @void: 
 * 
 * Registers the #ECalBackendFile class if necessary, and returns the type ID
 * associated to it.
 * 
 * Return value: The type ID of the #ECalBackendFile class.
 **/
GType
e_cal_backend_file_get_type (void)
{
	static GType e_cal_backend_file_type = 0;

	if (!e_cal_backend_file_type) {
		static GTypeInfo info = {
                        sizeof (ECalBackendFileClass),
                        (GBaseInitFunc) NULL,
                        (GBaseFinalizeFunc) NULL,
                        (GClassInitFunc) e_cal_backend_file_class_init,
                        NULL, NULL,
                        sizeof (ECalBackendFile),
                        0,
                        (GInstanceInitFunc) e_cal_backend_file_init
                };
		e_cal_backend_file_type = g_type_register_static (E_TYPE_CAL_BACKEND_SYNC,
								"ECalBackendFile", &info, 0);
	}

	return e_cal_backend_file_type;
}

void
e_cal_backend_file_set_file_name (ECalBackendFile *cbfile, const char *file_name)
{
	ECalBackendFilePrivate *priv;
	
	g_return_if_fail (cbfile != NULL);
	g_return_if_fail (E_IS_CAL_BACKEND_FILE (cbfile));
	g_return_if_fail (file_name != NULL);

	priv = cbfile->priv;
	
	if (priv->file_name)
		g_free (priv->file_name);
	
	priv->file_name = g_strdup (file_name);
}

const char *
e_cal_backend_file_get_file_name (ECalBackendFile *cbfile)
{
	ECalBackendFilePrivate *priv;

	g_return_val_if_fail (cbfile != NULL, NULL);
	g_return_val_if_fail (E_IS_CAL_BACKEND_FILE (cbfile), NULL);

	priv = cbfile->priv;	

	return priv->file_name;
}
