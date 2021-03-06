
#include <config.h>
#include <gconf/gconf-client.h>
#include <glib/gmain.h>
#include "libedataserver/e-source-list.h"

static GConfClient *conf_client;
static GMainLoop *main_loop;
static char *arg_hostname, *arg_username, *arg_password;

static void
add_account (const char *conf_key, const char *hostname, const char *username, const char *password)
{
	ESourceList *source_list;
	ESourceGroup *group;
	ESource *source;
	char *group_name;

	source_list = e_source_list_new_for_gconf (conf_client, conf_key);

	group_name = g_strdup_printf ("%s@%s:7181/soap", username, hostname);
	group = e_source_group_new ("Groupwise", "groupwise://");
	e_source_list_add_group (source_list, group, -1);

	if (password && *password) {
		g_free (group_name);
		group_name = g_strdup_printf ("%s:%s@%s/soap/", username, password, hostname);
	}
	source = e_source_new ("Frequent Contacts", group_name);
	e_source_set_property (source, "auth", "ldap/simple-binddn");
	e_source_set_property(source, "binddn", "user1");
	e_source_group_add_source (group, source, -1);
/*
	source = e_source_new ("Test User1", group_name);
	e_source_set_property (source, "auth", "ldap/simple-binddn");
	e_source_set_property(source, "binddn", "user1");
	e_source_group_add_source (group, source, -1);
	source = e_source_new ("mybook1", group_name);
	e_source_set_property (source, "auth", "ldap/simple-binddn");
	e_source_set_property(source, "binddn", "user1");
	e_source_group_add_source (group, source, -1);*/
	e_source_list_sync (source_list, NULL);

	g_free (group_name);
	g_object_unref (source);
	g_object_unref (group);
	g_object_unref (source_list);
}

static gboolean
idle_cb (gpointer data)
{
	add_account ("/apps/evolution/addressbook/sources", arg_hostname, arg_username, arg_password);
//	add_account ("/apps/evolution/tasks/sources", arg_hostname, arg_username, arg_password);

	g_main_loop_quit (main_loop);

	return FALSE;
}

int
main (int argc, char *argv[])
{
	g_type_init ();

	if (argc != 3 && argc != 4) {
		g_print ("Usage: %s hostname username [password]\n", argv[0]);
		return -1;
	}

	arg_hostname = argv[1];
	arg_username = argv[2];
	if (argc == 4)
		arg_password = argv[3];
	else
		arg_password = NULL;

	conf_client = gconf_client_get_default ();

	main_loop = g_main_loop_new (NULL, TRUE);
	g_idle_add ((GSourceFunc) idle_cb, NULL);
	g_main_loop_run (main_loop);

	/* terminate */
	g_object_unref (conf_client);
	g_main_loop_unref (main_loop);

	return 0;
}
