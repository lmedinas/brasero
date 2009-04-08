/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/*
 * Libbrasero-burn
 * Copyright (C) Philippe Rouquier 2005-2009 <bonfire-app@wanadoo.fr>
 *
 * Libbrasero-burn is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * The Libbrasero-burn authors hereby grant permission for non-GPL compatible
 * GStreamer plugins to be used and distributed together with GStreamer
 * and Libbrasero-burn. This permission is above and beyond the permissions granted
 * by the GPL license by which Libbrasero-burn is covered. If you modify this code
 * you may extend this exception to your version of the code, but you are not
 * obligated to do so. If you do not wish to do so, delete this exception
 * statement from your version.
 * 
 * Libbrasero-burn is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Library General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to:
 * 	The Free Software Foundation, Inc.,
 * 	51 Franklin Street, Fifth Floor
 * 	Boston, MA  02110-1301, USA.
 */

#ifndef _BRASERO_IMAGE_PROPERTIES_H_
#define _BRASERO_IMAGE_PROPERTIES_H_

#include <glib-object.h>

#include <gtk/gtk.h>

#include "brasero-track.h"

G_BEGIN_DECLS

#define BRASERO_TYPE_IMAGE_PROPERTIES             (brasero_image_properties_get_type ())
#define BRASERO_IMAGE_PROPERTIES(obj)             (G_TYPE_CHECK_INSTANCE_CAST ((obj), BRASERO_TYPE_IMAGE_PROPERTIES, BraseroImageProperties))
#define BRASERO_IMAGE_PROPERTIES_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST ((klass), BRASERO_TYPE_IMAGE_PROPERTIES, BraseroImagePropertiesClass))
#define BRASERO_IS_IMAGE_PROPERTIES(obj)          (G_TYPE_CHECK_INSTANCE_TYPE ((obj), BRASERO_TYPE_IMAGE_PROPERTIES))
#define BRASERO_IS_IMAGE_PROPERTIES_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE ((klass), BRASERO_TYPE_IMAGE_PROPERTIES))
#define BRASERO_IMAGE_PROPERTIES_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS ((obj), BRASERO_TYPE_IMAGE_PROPERTIES, BraseroImagePropertiesClass))

typedef struct _BraseroImagePropertiesClass BraseroImagePropertiesClass;
typedef struct _BraseroImageProperties BraseroImageProperties;

struct _BraseroImagePropertiesClass
{
	GtkFileChooserDialogClass parent_class;
};

struct _BraseroImageProperties
{
	GtkFileChooserDialog parent_instance;
};

GType brasero_image_properties_get_type (void) G_GNUC_CONST;

GtkWidget *brasero_image_properties_new ();

gchar *
brasero_image_properties_get_path (BraseroImageProperties *self);

gboolean
brasero_image_properties_is_path_edited (BraseroImageProperties *self);

BraseroImageFormat
brasero_image_properties_get_format (BraseroImageProperties *self);

void
brasero_image_properties_set_path (BraseroImageProperties *self,
				   const gchar *path);

void
brasero_image_properties_set_formats (BraseroImageProperties *self,
				      BraseroImageFormat formats,
				      BraseroImageFormat format);
G_END_DECLS

#endif /* _BRASERO_IMAGE_PROPERTIES_H_ */