/***************************************************************************
 *            growisofs.c
 *
 *  dim jan  15:8:51 6
 *  Copyright  6  Rouquier Philippe
 *  brasero-app@wanadoo.fr
 ***************************************************************************/

/*
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version  of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Library General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite , Boston, MA 111-17, USA.
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

#include <glib.h>
#include <glib-object.h>
#include <glib/gi18n-lib.h>

#include <gmodule.h>

#include "burn-basics.h"
#include "burn-plugin.h"
#include "burn-job.h"
#include "burn-process.h"
#include "brasero-ncb.h"
#include "burn-growisofs.h"
#include "burn-growisofs-common.h"

BRASERO_PLUGIN_BOILERPLATE (BraseroGrowisofs, brasero_growisofs, BRASERO_TYPE_PROCESS, BraseroProcess);

struct BraseroGrowisofsPrivate {
	gint64 sectors_num;
	guint use_utf8:1;
};
typedef struct BraseroGrowisofsPrivate BraseroGrowisofsPrivate;

#define BRASERO_GROWISOFS_PRIVATE(o)  (G_TYPE_INSTANCE_GET_PRIVATE ((o), BRASERO_TYPE_GROWISOFS, BraseroGrowisofsPrivate))

static GObjectClass *parent_class = NULL;

/* Process start */
static BraseroBurnResult
brasero_growisofs_read_stdout (BraseroProcess *process, const gchar *line)
{
	int perc_1, perc_2;
	int speed_1, speed_2;
	long long b_written, b_total;

	if (sscanf (line, "%10lld/%lld (%2d.%1d%%) @%2d.%1dx, remaining %*d:%*d",
		    &b_written, &b_total, &perc_1, &perc_2, &speed_1, &speed_2) == 6) {
		BraseroJobAction action;

		brasero_job_get_action (BRASERO_JOB (process), &action);
		if (action == BRASERO_JOB_ACTION_ERASE
		&&  b_written >= 65536) {
			/* we nullified 65536 that's enough. A signal SIGTERM
			 * will be sent in process.c. That's not the best way
			 * to do it but it works. */
			brasero_job_finished_session (BRASERO_JOB (process));
			return BRASERO_BURN_OK;
		}

		brasero_job_set_written_session (BRASERO_JOB (process), b_written);
		brasero_job_set_rate (BRASERO_JOB (process), (gdouble) (speed_1 * 10 + speed_2) / 10.0 * (gdouble) DVD_RATE);

		if (action == BRASERO_JOB_ACTION_ERASE) {
			brasero_job_set_current_action (BRASERO_JOB (process),
							BRASERO_BURN_ACTION_BLANKING,
							NULL,
							FALSE);
		}
		else
			brasero_job_set_current_action (BRASERO_JOB (process),
							BRASERO_BURN_ACTION_RECORDING,
							NULL,
							FALSE);

		brasero_job_start_progress (BRASERO_JOB (process), FALSE);
	}
	else if (strstr (line, "About to execute") || strstr (line, "Executing"))
		brasero_job_set_dangerous (BRASERO_JOB (process), TRUE);

	return BRASERO_BURN_OK;
}

static BraseroBurnResult
brasero_growisofs_read_stderr (BraseroProcess *process, const gchar *line)
{
	int perc_1, perc_2;

	if (sscanf (line, " %2d.%1d%% done, estimate finish", &perc_1, &perc_2) == 2) {
		gdouble fraction;
		BraseroBurnAction action;

		fraction = (gdouble) ((gdouble) perc_1 +
			   ((gdouble) perc_2 / (gdouble) 10.0)) /
			   (gdouble) 100.0;

		brasero_job_set_progress (BRASERO_JOB (process), fraction);

		brasero_job_get_current_action (BRASERO_JOB (process), &action);
		if (action == BRASERO_BURN_ACTION_BLANKING
		&&  fraction >= 0.01) {
			/* we nullified 1% (more than 65536) that's enough. A 
			 * signal SIGTERM will be sent. */
			brasero_job_finished_session (BRASERO_JOB (process));
			return BRASERO_BURN_OK;
		}

		brasero_job_set_current_action (BRASERO_JOB (process),
						BRASERO_BURN_ACTION_RECORDING,
						NULL,
						FALSE);
		brasero_job_start_progress (BRASERO_JOB (process), FALSE);
	}
	else if (strstr (line, "Total extents scheduled to be written = ")) {
		BraseroJobAction action;

		line += strlen ("Total extents scheduled to be written = ");
		brasero_job_get_action (BRASERO_JOB (process), &action);
		if (action == BRASERO_JOB_ACTION_SIZE) {
			gint64 sectors;

			sectors = strtoll (line, NULL, 10);

			/* NOTE: this has to be a multiple of 2048 */
			brasero_job_set_output_size_for_current_track (BRASERO_JOB (process),
								       sectors,
								       sectors * 2048);

			/* we better tell growisofs to stop here as it returns 
			 * a value of 1 when mkisofs is run with --print-size */
			brasero_job_finished_session (BRASERO_JOB (process));
		}
	}
	else if (strstr (line, "flushing cache") != NULL) {
		brasero_job_set_progress (BRASERO_JOB (process), 1.0);
		brasero_job_set_current_action (BRASERO_JOB (process),
						BRASERO_BURN_ACTION_FIXATING,
						NULL,
						FALSE);
	}
	else if (strstr (line, "already carries isofs") && strstr (line, "FATAL:")) {
		brasero_job_error (BRASERO_JOB (process), 
				   g_error_new (BRASERO_BURN_ERROR,
						BRASERO_BURN_ERROR_MEDIA_NOT_WRITABLE,
						_("The disc is already burnt")));
	}
	else if (strstr (line, "unable to open")
	     ||  strstr (line, "unable to stat")) {
		brasero_job_error (BRASERO_JOB (process), 
				   g_error_new (BRASERO_BURN_ERROR,
						BRASERO_BURN_ERROR_BUSY_DRIVE,
						_("The recorder could not be accessed")));
	}
	else if (strstr (line, "not enough space available") != NULL) {
		brasero_job_error (BRASERO_JOB (process), 
				   g_error_new (BRASERO_BURN_ERROR,
						BRASERO_BURN_ERROR_GENERAL,
						_("Not enough space available on the disc")));
	}
	else if (strstr (line, "end of user area encountered on this track") != NULL) {
		brasero_job_error (BRASERO_JOB (process), 
				   g_error_new (BRASERO_BURN_ERROR,
						BRASERO_BURN_ERROR_GENERAL,
						_("The files selected did not fit on the CD")));
	}
	else if (strstr (line, "blocks are free") != NULL) {
		brasero_job_error (BRASERO_JOB (process), 
				   g_error_new (BRASERO_BURN_ERROR,
						BRASERO_BURN_ERROR_GENERAL,
						_("The files selected did not fit on the CD")));
	}
	else if (strstr (line, "unable to proceed with recording: unable to unmount")) {
		brasero_job_error (BRASERO_JOB (process),
				   g_error_new_literal (BRASERO_BURN_ERROR,
							BRASERO_BURN_ERROR_JOLIET_TREE,
							_("the drive seems to be busy")));
	}
	else if (strstr (line, ":-(") != NULL || strstr (line, "FATAL")) {
		brasero_job_error (BRASERO_JOB (process), 
				   g_error_new (BRASERO_BURN_ERROR,
						BRASERO_BURN_ERROR_GENERAL,
						_("Unhandled error, aborting")));
	}
	else if (strstr (line, "Incorrectly encoded string")) {
		brasero_job_error (BRASERO_JOB (process),
				   g_error_new_literal (BRASERO_BURN_ERROR,
							BRASERO_BURN_ERROR_JOLIET_TREE,
							_("Some files have invalid filenames")));
	}
	else if (strstr (line, "Joliet tree sort failed.")) {
		brasero_job_error (BRASERO_JOB (process), 
				   g_error_new_literal (BRASERO_BURN_ERROR,
							BRASERO_BURN_ERROR_JOLIET_TREE,
							_("the image can't be created")));
	}

	return BRASERO_BURN_OK;
}

static BraseroBurnResult
brasero_growisofs_set_mkisofs_argv (BraseroGrowisofs *growisofs,
				    GPtrArray *argv,
				    GError **error)
{
	BraseroGrowisofsPrivate *priv;
	BraseroTrack *track = NULL;
	gchar *excluded_path = NULL;
	gchar *grafts_path = NULL;
	BraseroJobAction action;
	BraseroBurnResult result;
	BraseroTrackType input;
	gchar *emptydir = NULL;

	g_ptr_array_add (argv, g_strdup ("-r"));

	brasero_job_get_current_track (BRASERO_JOB (growisofs), &track);
	brasero_job_get_input_type (BRASERO_JOB (growisofs), &input);

	if (input.type & BRASERO_IMAGE_FS_JOLIET)
		g_ptr_array_add (argv, g_strdup ("-J"));

	if (input.type & BRASERO_IMAGE_FS_VIDEO)
		g_ptr_array_add (argv, g_strdup ("-dvd-video"));

	priv = BRASERO_GROWISOFS_PRIVATE (growisofs);
	if (priv->use_utf8) {
		g_ptr_array_add (argv, g_strdup ("-input-charset"));
		g_ptr_array_add (argv, g_strdup ("utf8"));
	}

	g_ptr_array_add (argv, g_strdup ("-graft-points"));
	g_ptr_array_add (argv, g_strdup ("-D"));	// This is dangerous the manual says but apparently it works well

	result = brasero_job_get_tmp_file (BRASERO_JOB (growisofs),
					   NULL,
					   &grafts_path,
					   error);
	if (result != BRASERO_BURN_OK)
		return result;

	result = brasero_job_get_tmp_file (BRASERO_JOB (growisofs),
					   NULL,
					   &excluded_path,
					   error);
	if (result != BRASERO_BURN_OK) {
		g_free (grafts_path);
		return result;
	}

	result = brasero_job_get_tmp_dir (BRASERO_JOB (growisofs),
					  &emptydir,
					  error);
	if (result != BRASERO_BURN_OK) {
		g_free (grafts_path);
		g_free (excluded_path);
		return result;
	}

	result = brasero_track_get_data_paths (track,
					       grafts_path,
					       excluded_path,
					       emptydir,
					       error);
	g_free (emptydir);

	if (result != BRASERO_BURN_OK) {
		g_free (grafts_path);
		g_free (excluded_path);
		return result;
	}

	g_ptr_array_add (argv, g_strdup ("-path-list"));
	g_ptr_array_add (argv, grafts_path);

	g_ptr_array_add (argv, g_strdup ("-exclude-list"));
	g_ptr_array_add (argv, excluded_path);

	brasero_job_get_action (BRASERO_JOB (growisofs), &action);
	if (action != BRASERO_JOB_ACTION_SIZE) {
		gchar *label = NULL;

		brasero_job_get_data_label (BRASERO_JOB (growisofs), &label);
		if (label) {
			g_ptr_array_add (argv, g_strdup ("-V"));
			g_ptr_array_add (argv, label);
		}

		g_ptr_array_add (argv, g_strdup ("-A"));
		g_ptr_array_add (argv, g_strdup_printf ("Brasero-%i.%i.%i",
							BRASERO_MAJOR_VERSION,
							BRASERO_MINOR_VERSION,
							BRASERO_SUB));
	
		g_ptr_array_add (argv, g_strdup ("-sysid"));
		g_ptr_array_add (argv, g_strdup ("LINUX"));
	
		/* FIXME! -sort is an interesting option allowing to decide where the 
		 * files are written on the disc and therefore to optimize later reading */
		/* FIXME: -hidden --hidden-list -hide-jolie -hide-joliet-list will allow to hide
		 * some files when we will display the contents of a disc we will want to merge */
		/* FIXME: support preparer publisher options */

		g_ptr_array_add (argv, g_strdup ("-v"));
	}
	else {
		/* we don't specify -q as there wouldn't be anything */
		g_ptr_array_add (argv, g_strdup ("-print-size"));
	}

	return BRASERO_BURN_OK;
}

/**
 * Some info about use-the-force-luke options
 * dry-run => stops after invoking mkisofs
 * no_tty => avoids fatal error if an isofs exists and an image is piped
 *  	  => skip the five seconds waiting
 * 
 */
 
static BraseroBurnResult
brasero_growisofs_set_argv_record (BraseroGrowisofs *growisofs,
				   GPtrArray *argv,
				   GError **error)
{
	BraseroBurnResult result;
	BraseroJobAction action;
	BraseroBurnFlag flags;
	gint64 sectors = 0;
	gchar *device;
	guint speed;

	/* This seems to help to eject tray after burning (at least with mine) */
	g_ptr_array_add (argv, g_strdup ("growisofs"));
	g_ptr_array_add (argv, g_strdup ("-use-the-force-luke=notray"));

	brasero_job_get_flags (BRASERO_JOB (growisofs), &flags);
	if (flags & BRASERO_BURN_FLAG_DUMMY)
		g_ptr_array_add (argv, g_strdup ("-use-the-force-luke=dummy"));

	/* NOTE 1: dao is not a good thing if you want to make multisession
	 * DVD+-R. It will close the disc. Which make sense since DAO means
	 * Disc At Once. That's checked in burn-caps.c with coherency checks.
	 * NOTE 2: dao is supported for DL DVD after 6.0 (think about that for
	 * BurnCaps) */
	if (flags & BRASERO_BURN_FLAG_DAO)
		g_ptr_array_add (argv, g_strdup ("-use-the-force-luke=dao"));

	if (!(flags & BRASERO_BURN_FLAG_MULTI)) {
		/* This option seems to help creating DVD more compatible
		 * with DVD readers.
		 * NOTE: it doesn't work with DVD+RW and DVD-RW in restricted
		 * overwrite mode */
		g_ptr_array_add (argv, g_strdup ("-dvd-compat"));
	}

	brasero_job_get_speed (BRASERO_JOB (growisofs), &speed);
	if (speed > 0)
		g_ptr_array_add (argv, g_strdup_printf ("-speed=%d", speed));

	/* see if we're asked to merge some new data: in this case we MUST have
	 * a list of grafts. The image can't come through stdin or an already 
	 * made image */
	brasero_job_get_device (BRASERO_JOB (growisofs), &device);
	brasero_job_get_action (BRASERO_JOB (growisofs), &action);
	brasero_job_get_session_output_size (BRASERO_JOB (growisofs),
					     &sectors,
					     NULL);
	if (sectors) {
		/* NOTE: tracksize is in block number (2048 bytes) */
		g_ptr_array_add (argv,
				 g_strdup_printf ("-use-the-force-luke=tracksize:%"
						  G_GINT64_FORMAT,
						  sectors));
	}

	if (flags & BRASERO_BURN_FLAG_MERGE) {
		g_ptr_array_add (argv, g_strdup ("-M"));
		g_ptr_array_add (argv, device);
		
		/* this can only happen if source->type == BRASERO_TRACK_SOURCE_GRAFTS */
		if (action == BRASERO_JOB_ACTION_SIZE)
			g_ptr_array_add (argv, g_strdup ("-dry-run"));

		result = brasero_growisofs_set_mkisofs_argv (growisofs, 
							     argv,
							     error);
		if (result != BRASERO_BURN_OK)
			return result;
	}
	else {
		BraseroTrackType input;

		/* apparently we are not merging but growisofs will refuse to 
		 * write a piped image if there is one already on the disc;
		 * except with this option */
		g_ptr_array_add (argv, g_strdup ("-use-the-force-luke=tty"));

		brasero_job_get_input_type (BRASERO_JOB (growisofs), &input);
		if (brasero_job_get_fd_in (BRASERO_JOB (growisofs), NULL) == BRASERO_BURN_OK) {
			/* set the buffer. NOTE: apparently this needs to be a power of 2 */
			/* FIXME: is it right to mess with it ? 
			   g_ptr_array_add (argv, g_strdup_printf ("-use-the-force-luke=bufsize:%im", 32)); */

			if (!g_file_test ("/proc/self/fd/0", G_FILE_TEST_EXISTS)) {
				g_set_error (error,
					     BRASERO_BURN_ERROR,
					     BRASERO_BURN_ERROR_GENERAL,
					     _("the file /proc/self/fd/0 is missing"));
				return BRASERO_BURN_ERR;
			}

			/* FIXME: should we use DAO ? */
			g_ptr_array_add (argv, g_strdup ("-Z"));
			g_ptr_array_add (argv, g_strdup_printf ("%s=/proc/self/fd/0", device));
			g_free (device);
		}
		else if (input.type == BRASERO_TRACK_TYPE_IMAGE) {
			gchar *localpath;
			BraseroTrack *track;

			brasero_job_get_current_track (BRASERO_JOB (growisofs), &track);
			localpath = brasero_track_get_image_source (track, FALSE);
			if (!localpath) {
				g_set_error (error,
					     BRASERO_BURN_ERROR,
					     BRASERO_BURN_ERROR_GENERAL,
					     _("the image is not stored locally"));
				return BRASERO_BURN_ERR;
			}

			g_ptr_array_add (argv, g_strdup ("-Z"));
			g_ptr_array_add (argv, g_strdup_printf ("%s=%s",
								device,
								localpath));

			g_free (device);
			g_free (localpath);
		}
		else if (input.type == BRASERO_TRACK_TYPE_DATA) {
			g_ptr_array_add (argv, g_strdup ("-Z"));
			g_ptr_array_add (argv, device);

			/* this can only happen if source->type == BRASERO_TRACK_SOURCE_DATA */
			if (action == BRASERO_JOB_ACTION_SIZE)
				g_ptr_array_add (argv, g_strdup ("-dry-run"));

			result = brasero_growisofs_set_mkisofs_argv (growisofs, 
								     argv,
								     error);
			if (result != BRASERO_BURN_OK)
				return result;
		}
		else
			BRASERO_JOB_NOT_SUPPORTED (growisofs);
	}

	if (action == BRASERO_JOB_ACTION_SIZE)
		brasero_job_set_current_action (BRASERO_JOB (growisofs),
						BRASERO_BURN_ACTION_GETTING_SIZE,
						NULL,
						FALSE);
	else
		brasero_job_set_current_action (BRASERO_JOB (growisofs),
						BRASERO_BURN_ACTION_START_RECORDING,
						NULL,
						FALSE);

	return BRASERO_BURN_OK;
}

static BraseroBurnResult
brasero_growisofs_set_argv_blank (BraseroGrowisofs *growisofs,
				  GPtrArray *argv)
{
	BraseroBurnFlag flags;
	gchar *device;
	guint speed;

	g_ptr_array_add (argv, g_strdup ("growisofs"));
	brasero_job_get_flags (BRASERO_JOB (growisofs), &flags);
	if (!(flags & BRASERO_BURN_FLAG_FAST_BLANK))
		BRASERO_JOB_NOT_SUPPORTED (growisofs);

	g_ptr_array_add (argv, g_strdup ("-Z"));

	/* NOTE: /dev/zero works but not /dev/null. Why ? */
	brasero_job_get_device (BRASERO_JOB (growisofs), &device);
	g_ptr_array_add (argv, g_strdup_printf ("%s=%s", device, "/dev/zero"));
	g_free (device);

	/* That should fix a problem where when the DVD had an isofs
	 * growisofs warned that it had an isofs already on the disc */
	g_ptr_array_add (argv, g_strdup ("-use-the-force-luke=tty"));

	/* set maximum write speed */
	brasero_job_get_max_speed (BRASERO_JOB (growisofs), &speed);
	g_ptr_array_add (argv, g_strdup_printf ("-speed=%d", speed));

	/* we only need to nullify 64 KiB: we'll stop the process when
	 * at least 65536 bytes have been written. We put a little more
	 * so in stdout parsing function remaining time is not negative
	 * if that's too fast. */
	g_ptr_array_add (argv, g_strdup ("-use-the-force-luke=tracksize:1024"));

	if (flags & BRASERO_BURN_FLAG_DUMMY)
		g_ptr_array_add (argv, g_strdup ("-use-the-force-luke=dummy"));

	brasero_job_set_current_action (BRASERO_JOB (growisofs),
					BRASERO_BURN_ACTION_BLANKING,
					NULL,
					FALSE);
	brasero_job_start_progress (BRASERO_JOB (growisofs), FALSE);

	return BRASERO_BURN_OK;
}

static BraseroBurnResult
brasero_growisofs_set_argv (BraseroProcess *process,
			    GPtrArray *argv,
			    GError **error)
{
	BraseroJobAction action;
	BraseroBurnResult result;

	brasero_job_get_action (BRASERO_JOB (process), &action);
	if (action == BRASERO_JOB_ACTION_SIZE) {
		BraseroTrackType input;

		/* only do it if that's DATA as input */
		brasero_job_get_input_type (BRASERO_JOB (process), &input);
		if (input.type != BRASERO_TRACK_TYPE_DATA)
			return BRASERO_BURN_NOT_SUPPORTED;

		result = brasero_growisofs_set_argv_record (BRASERO_GROWISOFS (process),
							    argv,
							    error);
	}
	else if (action == BRASERO_JOB_ACTION_RECORD)
		result = brasero_growisofs_set_argv_record (BRASERO_GROWISOFS (process),
							    argv,
							    error);
	else if (action == BRASERO_JOB_ACTION_ERASE)
		result = brasero_growisofs_set_argv_blank (BRASERO_GROWISOFS (process),
							   argv);
	else
		BRASERO_JOB_NOT_READY (process);

	return result;
}
static void
brasero_growisofs_class_init (BraseroGrowisofsClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	BraseroProcessClass *process_class = BRASERO_PROCESS_CLASS (klass);

	g_type_class_add_private (klass, sizeof (BraseroGrowisofsPrivate));

	parent_class = g_type_class_peek_parent (klass);
	object_class->finalize = brasero_growisofs_finalize;

	process_class->stdout_func = brasero_growisofs_read_stdout;
	process_class->stderr_func = brasero_growisofs_read_stderr;
	process_class->set_argv = brasero_growisofs_set_argv;
	process_class->post = brasero_job_finished_session;
}

static void
brasero_growisofs_init (BraseroGrowisofs *obj)
{
	BraseroGrowisofsPrivate *priv;
	gchar *standard_error;
	gboolean res;

	priv = BRASERO_GROWISOFS_PRIVATE (obj);

	/* this code comes from ncb_mkisofs_supports_utf8 */
	res = g_spawn_command_line_sync ("mkisofs -input-charset utf8", 
					 NULL,
					 &standard_error,
					 NULL, 
					 NULL);
	if (res && !g_strrstr (standard_error, "Unknown charset"))
		priv->use_utf8 = TRUE;
	else
		priv->use_utf8 = FALSE;

	g_free (standard_error);
}

static void
brasero_growisofs_finalize (GObject *object)
{
	G_OBJECT_CLASS (parent_class)->finalize (object);
}

static BraseroBurnResult
brasero_growisofs_export_caps (BraseroPlugin *plugin, gchar **error)
{
	gchar *prog_name;
	GSList *output;
	GSList *input;

	brasero_plugin_define (plugin,
			       "growisofs",
			       _("growisofs burns DVDs"),
			       "Philippe Rouquier",
			       0);

	/* First see if this plugin can be used, i.e. if growisofs is in
	 * the path */
	prog_name = g_find_program_in_path ("growisofs");
	if (!prog_name) {
		*error = g_strdup (_("growisofs could not be found in the path"));
		return BRASERO_BURN_ERR;
	}
	g_free (prog_name);

	/* growisofs can write images to any type of DVD as long as it's blank */
	input = brasero_caps_image_new (BRASERO_PLUGIN_IO_ACCEPT_PIPE|
					BRASERO_PLUGIN_IO_ACCEPT_FILE,
					BRASERO_IMAGE_FORMAT_BIN);

	output = brasero_caps_disc_new (BRASERO_MEDIUM_DVD|
					BRASERO_MEDIUM_PLUS|
					BRASERO_MEDIUM_SEQUENTIAL|
					BRASERO_MEDIUM_WRITABLE|
					BRASERO_MEDIUM_BLANK);

	brasero_plugin_link_caps (plugin, output, input);
	g_slist_free (output);

	/* and images to DVD RW +/-(restricted) whatever the status */
	output = brasero_caps_disc_new (BRASERO_MEDIUM_DVD|
					BRASERO_MEDIUM_PLUS|
					BRASERO_MEDIUM_RESTRICTED|
					BRASERO_MEDIUM_REWRITABLE|
					BRASERO_MEDIUM_BLANK|
					BRASERO_MEDIUM_CLOSED|
					BRASERO_MEDIUM_APPENDABLE|
					BRASERO_MEDIUM_HAS_DATA);
	brasero_plugin_link_caps (plugin, output, input);
	g_slist_free (output);
	g_slist_free (input);

	/* for DATA type recording discs can be also appendable */
	output = brasero_caps_disc_new (BRASERO_MEDIUM_DVD|
					BRASERO_MEDIUM_PLUS|
					BRASERO_MEDIUM_RESTRICTED|
					BRASERO_MEDIUM_SEQUENTIAL|
					BRASERO_MEDIUM_WRITABLE|
					BRASERO_MEDIUM_REWRITABLE|
					BRASERO_MEDIUM_BLANK|
					BRASERO_MEDIUM_APPENDABLE|
					BRASERO_MEDIUM_HAS_DATA);
	
	input = brasero_caps_data_new (BRASERO_IMAGE_FS_ISO|
				       BRASERO_IMAGE_FS_JOLIET|
				       BRASERO_IMAGE_FS_VIDEO);

	brasero_plugin_link_caps (plugin, output, input);
	g_slist_free (output);

	/* growisofs has the possibility to record to closed DVD+RW/-restricted
	 * and to append some more data to them which makes it unique */
	output = brasero_caps_disc_new (BRASERO_MEDIUM_DVD|
					BRASERO_MEDIUM_PLUS|
					BRASERO_MEDIUM_RESTRICTED|
					BRASERO_MEDIUM_REWRITABLE|
					BRASERO_MEDIUM_CLOSED|
					BRASERO_MEDIUM_HAS_DATA);
	brasero_plugin_link_caps (plugin, output, input);
	g_slist_free (output);
	g_slist_free (input);

	/* For DVD-W and DVD-RW sequential
	 * NOTE: DAO et MULTI are exclusive. */
	brasero_plugin_set_flags (plugin,
				  BRASERO_MEDIUM_DVD|
				  BRASERO_MEDIUM_SEQUENTIAL|
				  BRASERO_MEDIUM_WRITABLE|
				  BRASERO_MEDIUM_REWRITABLE|
				  BRASERO_MEDIUM_BLANK,
				  BRASERO_BURN_FLAG_BURNPROOF|
				  BRASERO_BURN_FLAG_OVERBURN|
				  BRASERO_BURN_FLAG_MULTI|
				  BRASERO_BURN_FLAG_DUMMY|
				  BRASERO_BURN_FLAG_NOGRACE|
				  BRASERO_BURN_FLAG_APPEND|
				  BRASERO_BURN_FLAG_MERGE,
				  BRASERO_BURN_FLAG_NONE);

	brasero_plugin_set_flags (plugin,
				  BRASERO_MEDIUM_DVD|
				  BRASERO_MEDIUM_SEQUENTIAL|
				  BRASERO_MEDIUM_WRITABLE|
				  BRASERO_MEDIUM_REWRITABLE|
				  BRASERO_MEDIUM_BLANK,
				  BRASERO_BURN_FLAG_DAO|
				  BRASERO_BURN_FLAG_BURNPROOF|
				  BRASERO_BURN_FLAG_OVERBURN|
				  BRASERO_BURN_FLAG_DUMMY|
				  BRASERO_BURN_FLAG_NOGRACE|
				  BRASERO_BURN_FLAG_APPEND|
				  BRASERO_BURN_FLAG_MERGE,
				  BRASERO_BURN_FLAG_NONE);

	brasero_plugin_set_flags (plugin,
				  BRASERO_MEDIUM_DVD|
				  BRASERO_MEDIUM_SEQUENTIAL|
				  BRASERO_MEDIUM_WRITABLE|
				  BRASERO_MEDIUM_REWRITABLE|
				  BRASERO_MEDIUM_APPENDABLE|
				  BRASERO_MEDIUM_HAS_DATA,
				  BRASERO_BURN_FLAG_BURNPROOF|
				  BRASERO_BURN_FLAG_OVERBURN|
				  BRASERO_BURN_FLAG_MULTI|
				  BRASERO_BURN_FLAG_DUMMY|
				  BRASERO_BURN_FLAG_NOGRACE|
				  BRASERO_BURN_FLAG_APPEND|
				  BRASERO_BURN_FLAG_MERGE,
				  BRASERO_BURN_FLAG_NONE);

	/* see NOTE for DVD-RW restricted overwrite below */
	brasero_plugin_set_flags (plugin,
				  BRASERO_MEDIUM_DVD|
				  BRASERO_MEDIUM_RESTRICTED|
				  BRASERO_MEDIUM_REWRITABLE|
				  BRASERO_MEDIUM_BLANK,
				  BRASERO_BURN_FLAG_DAO|
				  BRASERO_BURN_FLAG_MULTI|
				  BRASERO_BURN_FLAG_BURNPROOF|
				  BRASERO_BURN_FLAG_OVERBURN|
				  BRASERO_BURN_FLAG_DUMMY|
				  BRASERO_BURN_FLAG_NOGRACE|
				  BRASERO_BURN_FLAG_BLANK_BEFORE_WRITE|
				  BRASERO_BURN_FLAG_MERGE,
				  BRASERO_BURN_FLAG_NONE);

	brasero_plugin_set_flags (plugin,
				  BRASERO_MEDIUM_DVD|
				  BRASERO_MEDIUM_RESTRICTED|
				  BRASERO_MEDIUM_REWRITABLE|
				  BRASERO_MEDIUM_APPENDABLE|
				  BRASERO_MEDIUM_CLOSED|
				  BRASERO_MEDIUM_HAS_DATA,
				  BRASERO_BURN_FLAG_BURNPROOF|
				  BRASERO_BURN_FLAG_OVERBURN|
				  BRASERO_BURN_FLAG_MULTI|
				  BRASERO_BURN_FLAG_DUMMY|
				  BRASERO_BURN_FLAG_NOGRACE|
				  BRASERO_BURN_FLAG_BLANK_BEFORE_WRITE|
				  BRASERO_BURN_FLAG_MERGE,
				  BRASERO_BURN_FLAG_NONE);

	/* DVD+ R/RW don't support dummy mode */
	brasero_plugin_set_flags (plugin,
				  BRASERO_MEDIUM_DVDR_PLUS|
				  BRASERO_MEDIUM_BLANK,
				  BRASERO_BURN_FLAG_DAO|
				  BRASERO_BURN_FLAG_BURNPROOF|
				  BRASERO_BURN_FLAG_OVERBURN|
				  BRASERO_BURN_FLAG_NOGRACE|
				  BRASERO_BURN_FLAG_APPEND|
				  BRASERO_BURN_FLAG_MERGE,
				  BRASERO_BURN_FLAG_NONE);

	brasero_plugin_set_flags (plugin,
				  BRASERO_MEDIUM_DVDR_PLUS|
				  BRASERO_MEDIUM_BLANK,
				  BRASERO_BURN_FLAG_BURNPROOF|
				  BRASERO_BURN_FLAG_OVERBURN|
				  BRASERO_BURN_FLAG_MULTI|
				  BRASERO_BURN_FLAG_NOGRACE|
				  BRASERO_BURN_FLAG_APPEND|
				  BRASERO_BURN_FLAG_MERGE,
				  BRASERO_BURN_FLAG_NONE);

	brasero_plugin_set_flags (plugin,
				  BRASERO_MEDIUM_DVDR_PLUS|
				  BRASERO_MEDIUM_APPENDABLE|
				  BRASERO_MEDIUM_HAS_DATA,
				  BRASERO_BURN_FLAG_BURNPROOF|
				  BRASERO_BURN_FLAG_OVERBURN|
				  BRASERO_BURN_FLAG_MULTI|
				  BRASERO_BURN_FLAG_NOGRACE|
				  BRASERO_BURN_FLAG_APPEND|
				  BRASERO_BURN_FLAG_MERGE,
				  BRASERO_BURN_FLAG_NONE);

	/* NOTE: growisofs doesn't support APPEND flag with DVD+RW. DVD+RW and 
	 * other overwrite media have only one session and therefore you can't
	 * add another session after (APPEND). Now growisofs is still able to 
	 * merge more data nevertheless. */

	/* for DVD+RW */
	brasero_plugin_set_flags (plugin,
				  BRASERO_MEDIUM_DVDRW_PLUS|
				  BRASERO_MEDIUM_BLANK,
				  BRASERO_BURN_FLAG_MULTI|
				  BRASERO_BURN_FLAG_DAO|
				  BRASERO_BURN_FLAG_BURNPROOF|
				  BRASERO_BURN_FLAG_OVERBURN|
				  BRASERO_BURN_FLAG_NOGRACE|
				  BRASERO_BURN_FLAG_BLANK_BEFORE_WRITE|
				  BRASERO_BURN_FLAG_MERGE,
				  BRASERO_BURN_FLAG_NONE);

	brasero_plugin_set_flags (plugin,
				  BRASERO_MEDIUM_DVDRW_PLUS|
				  BRASERO_MEDIUM_APPENDABLE|
				  BRASERO_MEDIUM_CLOSED|
				  BRASERO_MEDIUM_HAS_DATA,
				  BRASERO_BURN_FLAG_BURNPROOF|
				  BRASERO_BURN_FLAG_OVERBURN|
				  BRASERO_BURN_FLAG_MULTI|
				  BRASERO_BURN_FLAG_NOGRACE|
				  BRASERO_BURN_FLAG_BLANK_BEFORE_WRITE|
				  BRASERO_BURN_FLAG_MERGE,
				  BRASERO_BURN_FLAG_NONE);

	/* blank caps for +/restricted RW*/
	output = brasero_caps_disc_new (BRASERO_MEDIUM_DVD|
					BRASERO_MEDIUM_PLUS|
					BRASERO_MEDIUM_RESTRICTED|
					BRASERO_MEDIUM_REWRITABLE|
					BRASERO_MEDIUM_APPENDABLE|
					BRASERO_MEDIUM_CLOSED|
					BRASERO_MEDIUM_HAS_DATA|
					BRASERO_MEDIUM_BLANK);
	brasero_plugin_blank_caps (plugin, output);
	g_slist_free (output);

	brasero_plugin_set_blank_flags (plugin,
					BRASERO_MEDIUM_DVD|
					BRASERO_MEDIUM_RESTRICTED|
					BRASERO_MEDIUM_REWRITABLE|
					BRASERO_MEDIUM_APPENDABLE|
					BRASERO_MEDIUM_HAS_DATA|
					BRASERO_MEDIUM_BLANK|
					BRASERO_MEDIUM_CLOSED,
					BRASERO_BURN_FLAG_NOGRACE|
					BRASERO_BURN_FLAG_FAST_BLANK,
					BRASERO_BURN_FLAG_FAST_BLANK);

	/* again DVD+RW don't support dummy */
	brasero_plugin_set_blank_flags (plugin,
					BRASERO_MEDIUM_DVDRW_PLUS|
					BRASERO_MEDIUM_APPENDABLE|
					BRASERO_MEDIUM_HAS_DATA|
					BRASERO_MEDIUM_BLANK|
					BRASERO_MEDIUM_CLOSED,
					BRASERO_BURN_FLAG_NOGRACE|
					BRASERO_BURN_FLAG_FAST_BLANK,
					BRASERO_BURN_FLAG_FAST_BLANK);

	brasero_plugin_register_group (plugin, _(GROWISOFS_DESCRIPTION));

	return BRASERO_BURN_OK;
}
