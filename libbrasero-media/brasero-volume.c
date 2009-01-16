/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/*
 * brasero
 * Copyright (C) Philippe Rouquier 2005-2008 <bonfire-app@wanadoo.fr>
 *
 * Brasero is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * Brasero is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Library General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor Boston, MA 02110-1301,  USA
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <string.h>

#include <glib.h>
#include <glib/gi18n-lib.h>

#include <gio/gio.h>

#include "brasero-media-private.h"
#include "brasero-volume.h"
#include "brasero-gio-operation.h"

typedef struct _BraseroVolumePrivate BraseroVolumePrivate;
struct _BraseroVolumePrivate
{
	GCancellable *cancel;
};

#define BRASERO_VOLUME_PRIVATE(o)  (G_TYPE_INSTANCE_GET_PRIVATE ((o), BRASERO_TYPE_VOLUME, BraseroVolumePrivate))

G_DEFINE_TYPE (BraseroVolume, brasero_volume, BRASERO_TYPE_MEDIUM);

GVolume *
brasero_volume_get_gvolume (BraseroVolume *self)
{
	const gchar *volume_path = NULL;
	BraseroVolumePrivate *priv;
	GVolumeMonitor *monitor;
	GVolume *volume = NULL;
	BraseroDrive *drive;
	GList *volumes;
	GList *iter;

	priv = BRASERO_VOLUME_PRIVATE (self);

	drive = brasero_medium_get_drive (BRASERO_MEDIUM (self));

#if defined(HAVE_STRUCT_USCSI_CMD)
	volume_path = brasero_drive_get_block_device (drive);
#else
	volume_path = brasero_drive_get_device (drive);
#endif

	/* NOTE: medium-monitor already holds a reference for GVolumeMonitor */
	monitor = g_volume_monitor_get ();
	volumes = g_volume_monitor_get_volumes (monitor);
	g_object_unref (monitor);

	for (iter = volumes; iter; iter = iter->next) {
		gchar *device_path;
		GVolume *tmp;

		tmp = iter->data;
		device_path = g_volume_get_identifier (tmp, G_VOLUME_IDENTIFIER_KIND_UNIX_DEVICE);
		if (!device_path)
			continue;

		BRASERO_MEDIA_LOG ("Found volume %s", device_path);
		if (!strcmp (device_path, volume_path)) {
			volume = tmp;
			g_free (device_path);
			g_object_ref (volume);
			break;
		}

		g_free (device_path);
	}
	g_list_foreach (volumes, (GFunc) g_object_unref, NULL);
	g_list_free (volumes);

	if (!volume)
		BRASERO_MEDIA_LOG ("No volume found for medium");

	return volume;
}

gboolean
brasero_volume_is_mounted (BraseroVolume *volume)
{
	gchar *path;

	/* NOTE: that's the surest way to know if a drive is really mounted. For
	 * GIO a blank medium can be mounted to burn:/// which is not really 
	 * what we're interested in. So the mount path must be also local. */
	path = brasero_volume_get_mount_point (volume, NULL);
	if (path) {
		g_free (path);
		return TRUE;
	}

	return FALSE;
}

gchar *
brasero_volume_get_mount_point (BraseroVolume *volume,
				GError **error)
{
	BraseroVolumePrivate *priv;
	gchar *local_path = NULL;
	GVolume *gvolume;
	GMount *mount;
	GFile *root;

	priv = BRASERO_VOLUME_PRIVATE (volume);

	gvolume = brasero_volume_get_gvolume (volume);
	if (!gvolume)
		return NULL;

	/* get the uri for the mount point */
	mount = g_volume_get_mount (gvolume);
	g_object_unref (gvolume);
	if (!mount)
		return NULL;

	root = g_mount_get_root (mount);
	g_object_unref (mount);

	if (!root) {
		g_set_error (error,
			     BRASERO_MEDIA_ERROR,
			     BRASERO_MEDIA_ERROR_GENERAL,
			     _("The disc mount point could not be retrieved"));
	}
	else {
		local_path = g_file_get_path (root);
		g_object_unref (root);
	}

	return local_path;
}

gboolean
brasero_volume_umount (BraseroVolume *volume,
		       gboolean wait,
		       GError **error)
{
	gboolean result;
	GVolume *gvolume;
	BraseroVolumePrivate *priv;

	if (!volume)
		return TRUE;

	priv = BRASERO_VOLUME_PRIVATE (volume);

	gvolume = brasero_volume_get_gvolume (volume);
	if (!gvolume)
		return TRUE;

	result = brasero_gio_operation_umount (gvolume,
					       priv->cancel,
					       wait,
					       error);
	g_object_unref (gvolume);

	return result;
}

gboolean
brasero_volume_mount (BraseroVolume *volume,
		      GtkWindow *parent_window,
		      gboolean wait,
		      GError **error)
{
	gboolean result;
	GVolume *gvolume;
	BraseroVolumePrivate *priv;

	if (!volume)
		return TRUE;

	priv = BRASERO_VOLUME_PRIVATE (volume);

	gvolume = brasero_volume_get_gvolume (volume);
	if (!gvolume)
		return TRUE;

	result = brasero_gio_operation_mount (gvolume,
					      parent_window,
					      priv->cancel,
					      wait,
					      error);
	g_object_unref (gvolume);

	return result;
}

void
brasero_volume_cancel_current_operation (BraseroVolume *self)
{
	BraseroVolumePrivate *priv;

	priv = BRASERO_VOLUME_PRIVATE (self);	
	g_cancellable_cancel (priv->cancel);
}

GIcon *
brasero_volume_get_icon (BraseroVolume *self)
{
	GVolume *volume;
	GMount *mount;
	GIcon *icon;

	if (!self)
		return g_themed_icon_new_with_default_fallbacks ("drive-optical");

	if (brasero_medium_get_status (BRASERO_MEDIUM (self)) == BRASERO_MEDIUM_FILE)
		return g_themed_icon_new_with_default_fallbacks ("iso-image-new");

	volume = brasero_volume_get_gvolume (self);
	if (!volume)
		return g_themed_icon_new_with_default_fallbacks ("drive-optical");

	mount = g_volume_get_mount (volume);
	if (mount) {
		icon = g_mount_get_icon (mount);
		g_object_unref (mount);
	}
	else
		icon = g_volume_get_icon (volume);

	g_object_unref (volume);

	return icon;
}

gchar *
brasero_volume_get_name (BraseroVolume *self)
{
	BraseroVolumePrivate *priv;
	BraseroMedia media;
	const gchar *type;
	GVolume *volume;
	gchar *name;

	priv = BRASERO_VOLUME_PRIVATE (self);

	media = brasero_medium_get_status (BRASERO_MEDIUM (self));
	if (media & BRASERO_MEDIUM_FILE) {
		/* Translators: This is a fake drive, a file, and means that
		 * when we're writing, we're writing to a file and create an
		 * image on the hard drive. */
		return g_strdup (_("Image File"));
	}

	if (media & BRASERO_MEDIUM_HAS_AUDIO) {
		const gchar *audio_name;

		audio_name = brasero_medium_get_CD_TEXT_title (BRASERO_MEDIUM (self));
		if (audio_name)
			return g_strdup (audio_name);
	}

	volume = brasero_volume_get_gvolume (self);
	if (!volume)
		goto last_chance;

	name = g_volume_get_name (volume);
	g_object_unref (volume);

	if (name)
		return name;

last_chance:

	type = brasero_medium_get_type_string (BRASERO_MEDIUM (self));
	name = NULL;
	if (media & BRASERO_MEDIUM_BLANK) {
		/* NOTE for translators: the first %s is the disc type. */
		name = g_strdup_printf (_("Blank %s"), type);
	}
	else if (BRASERO_MEDIUM_IS (media, BRASERO_MEDIUM_HAS_AUDIO|BRASERO_MEDIUM_HAS_DATA)) {
		/* NOTE for translators: the first %s is the disc type. */
		name = g_strdup_printf (_("Audio and data %s"), type);
	}
	else if (media & BRASERO_MEDIUM_HAS_AUDIO) {
		/* NOTE for translators: the first %s is the disc type. */
		name = g_strdup_printf (_("Audio %s"), type);
	}
	else if (media & BRASERO_MEDIUM_HAS_DATA) {
		/* NOTE for translators: the first %s is the disc type. */
		name = g_strdup_printf (_("Data %s"), type);
	}
	else {
		/* NOTE for translators: the first %s is the disc type. */
		name = g_strdup (type);
	}

	return name;
}

static void
brasero_volume_init (BraseroVolume *object)
{
	BraseroVolumePrivate *priv;

	priv = BRASERO_VOLUME_PRIVATE (object);
	priv->cancel = g_cancellable_new ();
}

static void
brasero_volume_finalize (GObject *object)
{
	BraseroVolumePrivate *priv;

	priv = BRASERO_VOLUME_PRIVATE (object);

	if (priv->cancel) {
		g_cancellable_cancel (priv->cancel);
		g_object_unref (priv->cancel);
		priv->cancel = NULL;
	}

	G_OBJECT_CLASS (brasero_volume_parent_class)->finalize (object);
}

static void
brasero_volume_class_init (BraseroVolumeClass *klass)
{
	GObjectClass* object_class = G_OBJECT_CLASS (klass);

	g_type_class_add_private (klass, sizeof (BraseroVolumePrivate));

	object_class->finalize = brasero_volume_finalize;
}

BraseroVolume *
brasero_volume_new (BraseroDrive *drive,
		    const gchar *udi)
{
	BraseroVolume *volume;

	g_return_val_if_fail (drive != NULL, NULL);
	volume = g_object_new (BRASERO_TYPE_VOLUME,
			       "drive", drive,
			       "udi", udi,
			       NULL);

	return volume;
}
