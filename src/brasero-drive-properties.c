/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/*
 * brasero
 * Copyright (C) Philippe Rouquier 2007-2008 <bonfire-app@wanadoo.fr>
 * 
 *  Brasero is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 * 
 * brasero is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with brasero.  If not, write to:
 * 	The Free Software Foundation, Inc.,
 * 	51 Franklin Street, Fifth Floor
 * 	Boston, MA  02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <string.h>

#include <glib.h>
#include <glib-object.h>
#include <glib/gi18n-lib.h>

#include <gio/gio.h>

#include <gtk/gtk.h>

#include "burn-basics.h"
#include "brasero-medium.h"
#include "burn-debug.h"
#include "brasero-drive.h"
#include "brasero-utils.h"
#include "brasero-session-cfg.h"
#include "brasero-drive-properties.h"

typedef struct _BraseroDrivePropertiesPrivate BraseroDrivePropertiesPrivate;
struct _BraseroDrivePropertiesPrivate
{
	GtkWidget *speed;
	GtkWidget *dummy;
	GtkWidget *multi;
	GtkWidget *burnproof;
	GtkWidget *notmp;
	GtkWidget *eject;

	GtkWidget *tmpdir;
	GtkWidget *tmpdir_size;
};

#define BRASERO_DRIVE_PROPERTIES_PRIVATE(o)  (G_TYPE_INSTANCE_GET_PRIVATE ((o), BRASERO_TYPE_DRIVE_PROPERTIES, BraseroDrivePropertiesPrivate))

enum {
	PROP_TEXT,
	PROP_RATE,
	PROP_NUM
};


G_DEFINE_TYPE (BraseroDriveProperties, brasero_drive_properties, GTK_TYPE_DIALOG);

BraseroBurnFlag
brasero_drive_properties_get_flags (BraseroDriveProperties *self)
{
	BraseroBurnFlag flags = BRASERO_BURN_FLAG_NONE;
	BraseroDrivePropertiesPrivate *priv;

	priv = BRASERO_DRIVE_PROPERTIES_PRIVATE (self);

	/* retrieve the flags */
	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (priv->notmp)))
		flags |= BRASERO_BURN_FLAG_NO_TMP_FILES;

	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (priv->eject)))
		flags |= BRASERO_BURN_FLAG_EJECT;

	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (priv->dummy)))
		flags |= BRASERO_BURN_FLAG_DUMMY;

	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (priv->burnproof)))
		flags |= BRASERO_BURN_FLAG_BURNPROOF;

	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (priv->multi)))
		flags |= BRASERO_BURN_FLAG_MULTI;

	return flags;
}

gint64
brasero_drive_properties_get_rate (BraseroDriveProperties *self)
{
	BraseroDrivePropertiesPrivate *priv;
	GtkTreeModel *model;
	GtkTreeIter iter;
	gint64 rate;

	priv = BRASERO_DRIVE_PROPERTIES_PRIVATE (self);

	model = gtk_combo_box_get_model (GTK_COMBO_BOX (priv->speed));
	if (!gtk_combo_box_get_active_iter (GTK_COMBO_BOX (priv->speed), &iter))
		gtk_tree_model_get_iter_first (model, &iter);

	gtk_tree_model_get (model, &iter,
			    PROP_RATE, &rate,
			    -1);

	return rate;
}

gchar *
brasero_drive_properties_get_tmpdir (BraseroDriveProperties *self)
{
	BraseroDrivePropertiesPrivate *priv;

	priv = BRASERO_DRIVE_PROPERTIES_PRIVATE (self);
	return gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (priv->tmpdir));
}

static void
brasero_drive_properties_set_tmpdir_info (BraseroDriveProperties *self,
					  const gchar *path)
{
	GFile *file;
	gchar *string;
	GFileInfo *info;
	guint64 vol_size = 0;
	BraseroDrivePropertiesPrivate *priv;

	priv = BRASERO_DRIVE_PROPERTIES_PRIVATE (self);

	/* get the volume free space */
	file = g_file_new_for_commandline_arg (path);
	if (!file) {
		BRASERO_BURN_LOG ("Impossible to retrieve size for %s", path);
		gtk_label_set_text (GTK_LABEL (priv->tmpdir_size), _("Unknown"));
		return;
	}

	info = g_file_query_filesystem_info (file,
					     G_FILE_ATTRIBUTE_FILESYSTEM_FREE,
					     NULL,
					     NULL);
	g_object_unref (file);

	if (!info) {
		BRASERO_BURN_LOG ("Impossible to retrieve size for %s", path);
		gtk_label_set_text (GTK_LABEL (priv->tmpdir_size), _("Unknown"));
		return;
	}

	vol_size = g_file_info_get_attribute_uint64 (info, G_FILE_ATTRIBUTE_FILESYSTEM_FREE);
	g_object_unref (info);

	string = brasero_utils_get_size_string (vol_size, TRUE, TRUE);
	gtk_label_set_text (GTK_LABEL (priv->tmpdir_size), string);
	g_free (string);
}

static void
brasero_drive_properties_tmpdir_changed (GtkFileChooser *chooser,
					 BraseroDriveProperties *self)
{
	gchar *path;
	BraseroDrivePropertiesPrivate *priv;

	priv = BRASERO_DRIVE_PROPERTIES_PRIVATE (self);

	path = gtk_file_chooser_get_filename (chooser);
	if (!path)
		return;

	brasero_drive_properties_set_tmpdir_info (self, path);
	g_free (path);
}

void
brasero_drive_properties_set_tmpdir (BraseroDriveProperties *self,
				     const gchar *path)
{
	BraseroDrivePropertiesPrivate *priv;

	priv = BRASERO_DRIVE_PROPERTIES_PRIVATE (self);

	if (!path)
		path = g_get_tmp_dir ();

	gtk_file_chooser_set_filename (GTK_FILE_CHOOSER (priv->tmpdir), path);
	brasero_drive_properties_set_tmpdir_info (self, path);
}

static gboolean
brasero_drive_properties_check_tmpdir (BraseroDriveProperties *self,
				       const gchar *path)
{
	GFile *file;
	GFileInfo *info;
	GError *error = NULL;
	const gchar *filesystem;
	BraseroDrivePropertiesPrivate *priv;

	priv = BRASERO_DRIVE_PROPERTIES_PRIVATE (self);

	file = g_file_new_for_commandline_arg (path);
	if (!file)
		return TRUE;

	info = g_file_query_info (file,
				  G_FILE_ATTRIBUTE_ACCESS_CAN_WRITE,
				  G_FILE_QUERY_INFO_NONE,
				  NULL,
				  &error);
	if (error) {
		gint answer;
		gchar *string;
		GtkWidget *dialog;
		GtkWidget *toplevel;

		if (error)
			return TRUE;

		/* Tell the user what went wrong */
		toplevel = gtk_widget_get_toplevel (GTK_WIDGET (self));
		dialog = gtk_message_dialog_new (GTK_WINDOW (toplevel),
						 GTK_DIALOG_DESTROY_WITH_PARENT |
						 GTK_DIALOG_MODAL,
						 GTK_MESSAGE_WARNING,
						 GTK_BUTTONS_NONE,
						 _("Do you really want to choose this location?"));

		string = g_strdup_printf ("%s.", error->message);
		gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog), "%s", string);
		g_error_free (error);
		g_free (string);

		gtk_dialog_add_buttons (GTK_DIALOG (dialog),
					_("_Keep Current Location"), GTK_RESPONSE_CANCEL,
					_("_Change Location"), GTK_RESPONSE_OK,
					NULL);

		gtk_widget_show_all (dialog);
		answer = gtk_dialog_run (GTK_DIALOG (dialog));
		gtk_widget_destroy (dialog);

		g_object_unref (info);
		g_object_unref (file);
		if (answer == GTK_RESPONSE_OK)
			return FALSE;

		return TRUE;
	}

	if (!g_file_info_get_attribute_boolean (info, G_FILE_ATTRIBUTE_ACCESS_CAN_WRITE)) {
		gint answer;
		gchar *string;
		GtkWidget *dialog;
		GtkWidget *toplevel;

		toplevel = gtk_widget_get_toplevel (GTK_WIDGET (self));
		dialog = gtk_message_dialog_new (GTK_WINDOW (toplevel),
						 GTK_DIALOG_DESTROY_WITH_PARENT |
						 GTK_DIALOG_MODAL,
						 GTK_MESSAGE_WARNING,
						 GTK_BUTTONS_NONE,
						 _("Do you really want to choose this location?"));

		string = g_strdup_printf ("%s.", _("You do not have the required permission to write at this location"));
		gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog), "%s", string);
		g_free (string);

		gtk_dialog_add_buttons (GTK_DIALOG (dialog),
					_("_Keep Current Location"), GTK_RESPONSE_CANCEL,
					_("_Change Location"), GTK_RESPONSE_OK,
					NULL);

		gtk_widget_show_all (dialog);
		answer = gtk_dialog_run (GTK_DIALOG (dialog));
		gtk_widget_destroy (dialog);

		g_object_unref (info);
		g_object_unref (file);
		if (answer == GTK_RESPONSE_OK)
			return FALSE;

		return TRUE;
	}

	g_object_unref (info);
	info = g_file_query_filesystem_info (file,
					     G_FILE_ATTRIBUTE_FILESYSTEM_TYPE,
					     NULL,
					     &error);
	g_object_unref (file);

	/* NOTE/FIXME: also check, probably best at start or in a special dialog
	 * whether quotas or any other limitation enforced on the system may not
	 * get in out way. Think getrlimit (). */

	/* check the filesystem type: the problem here is that some
	 * filesystems have a maximum file size limit of 4 GiB and more than
	 * often we need a temporary file size of 4 GiB or more. */
	filesystem = g_file_info_get_attribute_string (info, G_FILE_ATTRIBUTE_FILESYSTEM_TYPE);
	if (filesystem && !strcmp (filesystem, "msdos")) {
		gint answer;
		GtkWidget *dialog;
		GtkWidget *toplevel;

		toplevel = gtk_widget_get_toplevel (GTK_WIDGET (self));
		dialog = gtk_message_dialog_new (GTK_WINDOW (toplevel),
						 GTK_DIALOG_DESTROY_WITH_PARENT |
						 GTK_DIALOG_MODAL,
						 GTK_MESSAGE_WARNING,
						 GTK_BUTTONS_NONE,
						 _("Do you really want to choose this location?"));

		gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog),
							  _("The filesystem on this volume does not support large files (size over 2 GiB)."
							    "\nThis can be a problem when writing DVDs or large images."));

		gtk_dialog_add_buttons (GTK_DIALOG (dialog),
					_("_Keep Current Location"), GTK_RESPONSE_CANCEL,
					_("_Change Location"), GTK_RESPONSE_OK,
					NULL);

		gtk_widget_show_all (dialog);
		answer = gtk_dialog_run (GTK_DIALOG (dialog));
		gtk_widget_destroy (dialog);

		g_object_unref (info);
		if (answer == GTK_RESPONSE_OK)
			return FALSE;
	}
	else if (info)
		g_object_unref (info);

	return TRUE;
}

static void
brasero_drive_properties_response (GtkDialog *dialog,
				   gint response,
				   gpointer NULL_data)
{
	BraseroDrivePropertiesPrivate *priv;
	gchar *path;

	priv = BRASERO_DRIVE_PROPERTIES_PRIVATE (dialog);
	if (response != GTK_RESPONSE_ACCEPT)
		return;

	path = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (priv->tmpdir));
	if (!brasero_drive_properties_check_tmpdir (BRASERO_DRIVE_PROPERTIES (dialog), path))
		g_signal_stop_emission_by_name (dialog, "response");
	g_free (path);
}

static void
brasero_drive_properties_set_toggle_state (GtkWidget *toggle,
					   BraseroBurnFlag flag,
					   BraseroBurnFlag flags,
					   BraseroBurnFlag supported,
					   BraseroBurnFlag compulsory)
{
	if (!(supported & flag)) {
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (toggle), FALSE);
		gtk_widget_set_sensitive (toggle, FALSE);
		return;
	}

	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (toggle), (flags & flag));
	gtk_widget_set_sensitive (toggle, (compulsory & flag) == 0);
}

void
brasero_drive_properties_set_flags (BraseroDriveProperties *self,
				    BraseroBurnFlag flags,
				    BraseroBurnFlag supported,
				    BraseroBurnFlag compulsory)
{
	BraseroDrivePropertiesPrivate *priv;

	priv = BRASERO_DRIVE_PROPERTIES_PRIVATE (self);

	flags &= BRASERO_DRIVE_PROPERTIES_FLAGS;
	supported &= BRASERO_DRIVE_PROPERTIES_FLAGS;
	compulsory &= BRASERO_DRIVE_PROPERTIES_FLAGS;

	/* flag properties */
	brasero_drive_properties_set_toggle_state (priv->dummy,
						   BRASERO_BURN_FLAG_DUMMY,
						   flags,
						   supported,
						   compulsory);
	brasero_drive_properties_set_toggle_state (priv->eject,
						   BRASERO_BURN_FLAG_EJECT,
						   flags,
						   supported,
						   compulsory);						   
	brasero_drive_properties_set_toggle_state (priv->burnproof,
						   BRASERO_BURN_FLAG_BURNPROOF,
						   flags,
						   supported,
						   compulsory);
	brasero_drive_properties_set_toggle_state (priv->notmp,
						   BRASERO_BURN_FLAG_NO_TMP_FILES,
						   flags,
						   supported,
						   compulsory);
	brasero_drive_properties_set_toggle_state (priv->multi,
						   BRASERO_BURN_FLAG_MULTI,
						   flags,
						   supported,
						   compulsory);
}

static gchar *
brasero_drive_properties_format_disc_speed (BraseroMedia media,
					    gint64 rate)
{
	gchar *text;

	if (media & BRASERO_MEDIUM_DVD)
		text = g_strdup_printf (_("%.1f x (DVD)"),
					BRASERO_RATE_TO_SPEED_DVD (rate));
	else if (media & BRASERO_MEDIUM_CD)
		text = g_strdup_printf (_("%.1f x (CD)"),
					BRASERO_RATE_TO_SPEED_CD (rate));
	else if (media & BRASERO_MEDIUM_BD)
		text = g_strdup_printf (_("%.1f x (BD)"),
					BRASERO_RATE_TO_SPEED_BD (rate));
	else
		text = g_strdup_printf (_("%.1f x (BD) %.1f x (DVD) %.1f x (CD)"),
					BRASERO_RATE_TO_SPEED_BD (rate),
					BRASERO_RATE_TO_SPEED_DVD (rate),
					BRASERO_RATE_TO_SPEED_CD (rate));

	return text;
}

void
brasero_drive_properties_set_drive (BraseroDriveProperties *self,
				    BraseroDrive *drive,
				    gint64 default_rate)
{
	BraseroDrivePropertiesPrivate *priv;
	BraseroMedium *medium;
	BraseroMedia media;
	GtkTreeModel *model;
	gchar *display_name;
	GtkTreeIter iter;
	gint64 *rates;
	gchar *header;
	gchar *text;
	guint i;

	priv = BRASERO_DRIVE_PROPERTIES_PRIVATE (self);

	/* set the header of the dialog */
	display_name = brasero_drive_get_display_name (drive);
	header = g_strdup_printf (_("Properties of %s"), display_name);
	g_free (display_name);

	gtk_window_set_title (GTK_WINDOW (self), header);
	g_free (header);

	/* Speed combo */
	medium = brasero_drive_get_medium (drive);
	media = brasero_medium_get_status (medium);
	rates = brasero_medium_get_write_speeds (medium);
	model = gtk_combo_box_get_model (GTK_COMBO_BOX (priv->speed));

	if (!rates) {
		gtk_widget_set_sensitive (priv->speed, FALSE);
		gtk_list_store_append (GTK_LIST_STORE (model), &iter);
		gtk_list_store_set (GTK_LIST_STORE (model), &iter,
				    PROP_TEXT, _("Impossible to retrieve speeds"),
				    PROP_RATE, 1764, /* Speed 1 */
				    -1);
		gtk_combo_box_set_active_iter (GTK_COMBO_BOX (priv->speed), &iter);
		return;
	}

	gtk_list_store_append (GTK_LIST_STORE (model), &iter);
	gtk_list_store_set (GTK_LIST_STORE (model), &iter,
			    PROP_TEXT, _("Max speed"),
			    PROP_RATE, rates [0],
			    -1);

	/* fill model */
	for (i = 0; rates [i] != 0; i ++) {
		text = brasero_drive_properties_format_disc_speed (media, rates [i]);
		gtk_list_store_append (GTK_LIST_STORE (model), &iter);
		gtk_list_store_set (GTK_LIST_STORE (model), &iter,
				    PROP_TEXT, text,
				    PROP_RATE, rates [i],
				    -1);
		g_free (text);
	}

	/* Set active one preferably max speed */
	gtk_tree_model_get_iter_first (model, &iter);
	do {
		gint64 rate;

		gtk_tree_model_get (model, &iter,
				    PROP_RATE, &rate,
				    -1);

		/* we do this to round things and get the closest possible speed */
		if ((rate / 1024) == (default_rate / 1024)) {
			gtk_combo_box_set_active_iter (GTK_COMBO_BOX (priv->speed), &iter);
			break;
		}

	} while (gtk_tree_model_iter_next (model, &iter));

	/* make sure at least one is active */
	if (!gtk_combo_box_get_active_iter (GTK_COMBO_BOX (priv->speed), &iter)) {
		gtk_tree_model_get_iter_first (model, &iter);
		gtk_combo_box_set_active_iter (GTK_COMBO_BOX (priv->speed), &iter);
	}
}

static void
brasero_drive_properties_init (BraseroDriveProperties *object)
{
	BraseroDrivePropertiesPrivate *priv;
	GtkCellRenderer *renderer;
	GtkTreeModel *model;
	GtkWidget *label;
	GtkWidget *box;
	gchar *string;

	priv = BRASERO_DRIVE_PROPERTIES_PRIVATE (object);

	gtk_window_set_default_size (GTK_WINDOW (object), 340, 250);
	gtk_dialog_set_has_separator (GTK_DIALOG (object), FALSE);
	gtk_dialog_add_buttons (GTK_DIALOG (object),
				GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
				GTK_STOCK_OK, GTK_RESPONSE_ACCEPT,
				NULL);

	model = GTK_TREE_MODEL (gtk_list_store_new (PROP_NUM,
						    G_TYPE_STRING,
						    G_TYPE_INT64));

	priv->speed = gtk_combo_box_new_with_model (model);
	string = g_strdup_printf ("<b>%s</b>", _("Burning speed"));
	gtk_box_pack_start (GTK_BOX (GTK_DIALOG (object)->vbox),
			    brasero_utils_pack_properties (string,
							   priv->speed, NULL),
			    FALSE, FALSE, 0);
	g_free (string);

	renderer = gtk_cell_renderer_text_new ();
	gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (priv->speed), renderer, TRUE);
	gtk_cell_layout_set_attributes (GTK_CELL_LAYOUT (priv->speed), renderer,
					"text", PROP_TEXT,
					NULL);

	priv->dummy = gtk_check_button_new_with_mnemonic (_("_Simulate before burning"));
	gtk_widget_set_tooltip_text (priv->dummy, _("Brasero will simulate the burning and if it is successful, go on with actual burning after 10 seconds"));
	priv->burnproof = gtk_check_button_new_with_mnemonic (_("Use burn_proof (decrease the risk of failures)"));
	priv->eject = gtk_check_button_new_with_mnemonic (_("_Eject after burning"));
	priv->notmp = gtk_check_button_new_with_mnemonic (_("Burn the image directly _without saving it to disc"));
	priv->multi = gtk_check_button_new_with_mnemonic (_("Leave the disc _open to add other files later"));
	gtk_widget_set_tooltip_text (priv->multi, _("Allow to add more data to the disc later"));

	string = g_strdup_printf ("<b>%s</b>", _("Options"));
	gtk_box_pack_start (GTK_BOX (GTK_DIALOG (object)->vbox),
			    brasero_utils_pack_properties (string,
							   priv->eject,
							   priv->dummy,
							   priv->burnproof,
							   priv->multi,
							   priv->notmp,
							   NULL),
			    FALSE,
			    FALSE, 0);
	g_free (string);

	priv->tmpdir = gtk_file_chooser_button_new (_("Location for Temporary Files"),
						    GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER);

	box = gtk_hbox_new (FALSE, 6);
	gtk_widget_show (box);

	label = gtk_label_new_with_mnemonic (_("_Temporary directory free space:"));
	gtk_label_set_mnemonic_widget (GTK_LABEL (label), priv->tmpdir);
	gtk_widget_show (label);
	gtk_box_pack_start (GTK_BOX (box), label, FALSE, FALSE, 0);

	priv->tmpdir_size = gtk_label_new ("");
	gtk_widget_show (priv->tmpdir_size);
	gtk_box_pack_start (GTK_BOX (box), priv->tmpdir_size, FALSE, FALSE, 0);

	string = g_strdup_printf ("<b>%s</b>", _("Temporary files"));
	gtk_box_pack_start (GTK_BOX (GTK_DIALOG (object)->vbox),
			    brasero_utils_pack_properties (string,
							   box,
							   priv->tmpdir,
							   NULL),
			    FALSE,
			    FALSE, 0);
	g_free (string);

	gtk_file_chooser_set_filename (GTK_FILE_CHOOSER (priv->tmpdir),
				       g_get_tmp_dir ());

	gtk_widget_show_all (GTK_DIALOG (object)->vbox);

	g_signal_connect (priv->tmpdir,
			  "selection-changed",
			  G_CALLBACK (brasero_drive_properties_tmpdir_changed),
			  object);
	g_signal_connect (object,
			  "response",
			  G_CALLBACK (brasero_drive_properties_response),
			  NULL);
}

static void
brasero_drive_properties_finalize (GObject *object)
{
	G_OBJECT_CLASS (brasero_drive_properties_parent_class)->finalize (object);
}

static void
brasero_drive_properties_class_init (BraseroDrivePropertiesClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	g_type_class_add_private (klass, sizeof (BraseroDrivePropertiesPrivate));

	object_class->finalize = brasero_drive_properties_finalize;
}

GtkWidget *
brasero_drive_properties_new ()
{
	return GTK_WIDGET (g_object_new (BRASERO_TYPE_DRIVE_PROPERTIES, NULL));
}
