/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* 
 * Copyright (C) 2005 Novell, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <libxml/parser.h>
#include <glib/ghash.h>
#include <glib/gi18n.h>
#include <gconf/gconf-client.h>
#include "e-categories.h"

typedef struct {
	char *category;
	char *icon_file;
	char *color;
} CategoryInfo;

static gboolean initialized = FALSE;
static GHashTable *categories_table = NULL;
static GConfClient *conf_client = NULL;
static gboolean conf_is_dirty = FALSE;
static guint idle_id = 0;

static void
free_category_info (CategoryInfo *cat_info)
{
	if (cat_info->category)
		g_free (cat_info->category);
	if (cat_info->icon_file)
		g_free (cat_info->icon_file);
	if (cat_info->color)
		g_free (cat_info->color);

	g_free (cat_info);
}

static void
hash_to_xml_string (gpointer key, gpointer value, gpointer user_data)
{
	char *s;
	CategoryInfo *cat_info = value;
	GString **str = user_data;

	s = g_strdup_printf ("<category a=\"%s\" color=\"%s\" icon=\"%s\"/>",
			     cat_info->category, cat_info->color, cat_info->icon_file);

	*str = g_string_append (*str, s);

	g_free (s);
}

static void
idle_saver_cb (gpointer user_data)
{
	if (conf_is_dirty) {
		GString *str = g_string_new ("<categories>");

		g_hash_table_foreach (categories_table, (GHFunc) hash_to_xml_string, &str);
		str = g_string_append (str, "</categories>");
		gconf_client_set_string (conf_client, "/apps/evolution/general/category_master_list", str->str, NULL);

		g_string_free (str, TRUE);

		conf_is_dirty = FALSE;
	}
}

static void
cleanup_at_exit (void)
{
	if (conf_is_dirty)
		idle_saver_cb (NULL);

	g_source_remove (idle_id);
	idle_id = 0;

	if (categories_table) {
		g_hash_table_destroy (categories_table);
		categories_table = NULL;
	}

	if (conf_client) {
		g_object_unref (conf_client);
		conf_client = NULL;
	}

	initialized = FALSE;
}

static void
initialize_categories_config (void)
{
	char *str;

	if (initialized)
		return;

	/* create all the internal data we need */
	categories_table = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, free_category_info);

	conf_client = gconf_client_get_default ();

	g_atexit (cleanup_at_exit);

	/* load the categories config from the config database */
	str = gconf_client_get_string (conf_client, "/apps/evolution/general/category_master_list", NULL);
	if (str) {
		xmlDoc *doc;
		xmlNode *node, *children;

		doc = xmlParseMemory (str, strlen (str));
		if (doc) {
			node = xmlDocGetRootElement (doc);

			for (children = node->xmlChildrenNode; children != NULL; children = children->next) {
				e_categories_add (xmlGetProp (children, "a"),
						  xmlGetProp (children, "color"),
						  xmlGetProp (children, "icon"));
			}

			xmlFreeDoc (doc);
		}

		conf_is_dirty = FALSE;
	} else {
		e_categories_add (N_("Birthday"), NULL, E_DATA_SERVER_IMAGESDIR "/category_birthday_16.png");
		e_categories_add (N_("Business"), NULL, E_DATA_SERVER_IMAGESDIR "/category_business_16.png");
		e_categories_add (N_("Competition"), NULL, NULL);
		e_categories_add (N_("Favorites"), NULL, E_DATA_SERVER_IMAGESDIR "/category_favorites_16.png");
		e_categories_add (N_("Gifts"), NULL, E_DATA_SERVER_IMAGESDIR "/category_gifts_16.png");
		e_categories_add (N_("Goals/Objectives"), NULL, E_DATA_SERVER_IMAGESDIR "/category_goals_16.png");
		e_categories_add (N_("Holiday"), NULL, E_DATA_SERVER_IMAGESDIR "/category_holiday_16.png");
		e_categories_add (N_("Holiday Cards"), NULL, E_DATA_SERVER_IMAGESDIR "/category_holiday-cards_16.png");
		e_categories_add (N_("Hot Contacts"), NULL, E_DATA_SERVER_IMAGESDIR "/category_hot-contacts_16.png");
		e_categories_add (N_("Ideas"), NULL, E_DATA_SERVER_IMAGESDIR "/category_ideas_16.png");
		e_categories_add (N_("International"), NULL, E_DATA_SERVER_IMAGESDIR "/category_international_16.png");
		e_categories_add (N_("Key Customer"), NULL, E_DATA_SERVER_IMAGESDIR "/category_key-customer_16.png");
		e_categories_add (N_("Miscellaneous"), NULL, E_DATA_SERVER_IMAGESDIR "/category_miscellaneous_16.png");
		e_categories_add (N_("Personal"), NULL, E_DATA_SERVER_IMAGESDIR "/category_personal_16.png");
		e_categories_add (N_("Phone Calls"), NULL, E_DATA_SERVER_IMAGESDIR "/category_phonecalls_16.png");
		e_categories_add (N_("Status"), NULL, E_DATA_SERVER_IMAGESDIR "/category_status_16.png");
		e_categories_add (N_("Strategies"), NULL, E_DATA_SERVER_IMAGESDIR "/category_strategies_16.png");
		e_categories_add (N_("Suppliers"), NULL, E_DATA_SERVER_IMAGESDIR "/category_suppliers_16.png");
		e_categories_add (N_("Time & Expenses"), NULL, E_DATA_SERVER_IMAGESDIR "/category_time-and-expenses_16.png");
		e_categories_add (N_("VIP"), NULL, NULL);
		e_categories_add (N_("Waiting"), NULL, NULL);
	}

	/* install idle callback to save the file */
	idle_id = g_idle_add ((GSourceFunc) idle_saver_cb, NULL);

	g_free (str);
	initialized = TRUE;
}

static void
add_hash_to_list (gpointer key, gpointer value, gpointer user_data)
{
	GList **list = user_data;

	*list = g_list_append (*list, key);
}

/**
 * e_categories_get_list:
 *
 * Returns a list of all the category names currently configured.
 *
 * Return value: a GList containing the names of the categories. The list
 * should be freed using g_list_free, but the names of the categories should
 * not be touched at all, they are internal strings.
 */
GList *
e_categories_get_list (void)
{
	GList *list = NULL;

	if (!initialized)
		initialize_categories_config ();

	g_hash_table_foreach (categories_table, (GHFunc) add_hash_to_list, &list);

	return list;
}

/**
 * e_categories_add:
 * @category: name of category to add.
 * @color: associated color.
 * @icon_file: full path of the icon associated to the category.
 *
 * Adds a new category, with its corresponding color and icon, to the
 * configuration database.
 */
void
e_categories_add (const char *category, const char *color, const char *icon_file)
{
	CategoryInfo *cat_info;

	if (!initialized)
		initialize_categories_config ();

	/* remove the category if already in the hash table */
	if (g_hash_table_lookup (categories_table, category))
		g_hash_table_remove (categories_table, category);

	/* add the new category */
	cat_info = g_new0 (CategoryInfo, 1);
	cat_info->category = g_strdup (category);
	cat_info->color = g_strdup (color);
	cat_info->icon_file = g_strdup (icon_file);

	g_hash_table_insert (categories_table, g_strdup (category), cat_info);

	conf_is_dirty = TRUE;
}

/**
 * e_categories_remove:
 * @category: category to be removed.
 *
 * Removes the given category from the configuration.
 */
void
e_categories_remove (const char *category)
{
	g_return_if_fail (category != NULL);

	if (!initialized)
		initialize_categories_config ();

	if (g_hash_table_lookup (categories_table, category)) {
		g_hash_table_remove (categories_table, category);

		conf_is_dirty = TRUE;
	}
}

/**
 * e_categories_get_color_for:
 * @category: category to retrieve the color for.
 *
 * Gets the color associated with the given category.
 *
 * Return value: a string representation of the color.
 */
const char *
e_categories_get_color_for (const char *category)
{
	CategoryInfo *cat_info;

	if (!initialized)
		initialize_categories_config ();

	cat_info = g_hash_table_lookup (categories_table, category);
	if (!cat_info)
		return NULL;

	return (const char *) cat_info->color;
}

/**
 * e_categories_set_color_for:
 * @category: category to set the color for.
 * @color: X color.
 *
 * Sets the color associated with the given category.
 */
void
e_categories_set_color_for (const char *category, const char *color)
{
	CategoryInfo *cat_info;

	if (!initialized)
		initialize_categories_config ();

	cat_info = g_hash_table_lookup (categories_table, category);
	if (!cat_info)
		return;

	if (cat_info->color)
		g_free (cat_info->color);
	cat_info->color = g_strdup (color);

	conf_is_dirty = TRUE;
}

/**
 * e_categories_get_icon_file_for:
 * @category: category to retrieve the icon file for.
 *
 * Gets the icon file associated with the given category.
 *
 * Return value: a string representation of the color.
 */
const char *
e_categories_get_icon_file_for (const char *category)
{
	CategoryInfo *cat_info;

	if (!initialized)
		initialize_categories_config ();

	cat_info = g_hash_table_lookup (categories_table, category);
	if (!cat_info)
		return NULL;

	return (const char *) cat_info->icon_file;
}

/**
 * e_categories_set_icon_file_for:
 * @category: category to set the icon file for.
 * @color: X color.
 *
 * Sets the icon file associated with the given category.
 */
void
e_categories_set_icon_file_for (const char *category, const char *icon_file)
{
	CategoryInfo *cat_info;

	if (!initialized)
		initialize_categories_config ();

	cat_info = g_hash_table_lookup (categories_table, category);
	if (!cat_info)
		return;

	if (cat_info->icon_file)
		g_free (cat_info->icon_file);
	cat_info->icon_file = g_strdup (icon_file);

	conf_is_dirty = TRUE;
}
