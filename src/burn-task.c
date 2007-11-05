/***************************************************************************
 *            burn-task.c
 *
 *  mer sep 13 09:16:29 2006
 *  Copyright  2006  Rouquier Philippe
 *  bonfire-app@wanadoo.fr
 ***************************************************************************/

/*
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Library General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <glib.h>
#include <glib-object.h>
#include <glib/gi18n-lib.h>

#include "burn-basics.h"
#include "burn-debug.h"
#include "burn-session.h"
#include "burn-task.h"
#include "burn-task-item.h"
#include "burn-task-ctx.h"


static void brasero_task_class_init (BraseroTaskClass *klass);
static void brasero_task_init (BraseroTask *sp);
static void brasero_task_finalize (GObject *object);

typedef struct _BraseroTaskPrivate BraseroTaskPrivate;

struct _BraseroTaskPrivate {
	/* The loop for the task */
	GMainLoop *loop;

	/* used to poll for progress (every 0.5 sec) */
	gint clock_id;

	BraseroTaskItem *leader;
	BraseroTaskItem *first;

	/* result of the task */
	BraseroBurnResult retval;
	GError *error;
};

#define BRASERO_TASK_PRIVATE(o)  (G_TYPE_INSTANCE_GET_PRIVATE ((o), BRASERO_TYPE_TASK, BraseroTaskPrivate))
G_DEFINE_TYPE (BraseroTask, brasero_task, BRASERO_TYPE_TASK_CTX);

static GObjectClass *parent_class = NULL;

void
brasero_task_add_item (BraseroTask *task, BraseroTaskItem *item)
{
	BraseroTaskPrivate *priv;

	g_return_if_fail (BRASERO_IS_TASK (task));
	g_return_if_fail (BRASERO_IS_TASK_ITEM (item));

	priv = BRASERO_TASK_PRIVATE (task);

	if (priv->leader) {
		brasero_task_item_link (priv->leader, item);
		g_object_unref (priv->leader);
	}

	if (!priv->first)
		priv->first = item;

	priv->leader = item;
	g_object_ref (priv->leader);
}

static void
brasero_task_reset_real (BraseroTask *task)
{
	BraseroTaskPrivate *priv;

	priv = BRASERO_TASK_PRIVATE (task);

	if (priv->loop)
		g_main_loop_unref (priv->loop);

	priv->loop = NULL;
	priv->clock_id = 0;
	priv->retval = BRASERO_BURN_OK;

	if (priv->error) {
		g_error_free (priv->error);
		priv->error = NULL;
	}

	brasero_task_ctx_reset (BRASERO_TASK_CTX (task));
}

void
brasero_task_reset (BraseroTask *task)
{
	BraseroTaskPrivate *priv;

	priv = BRASERO_TASK_PRIVATE (task);

	if (brasero_task_is_running (task))
		brasero_task_cancel (task, TRUE);

	g_object_unref (priv->leader);
	brasero_task_reset_real (task);
}

static gboolean
brasero_task_clock_tick (gpointer data)
{
	BraseroTask *task = BRASERO_TASK (data);
	BraseroTaskPrivate *priv;
	BraseroTaskItem *item;

	priv = BRASERO_TASK_PRIVATE (task);

	/* some jobs need to be called periodically to update their status
	 * because the main process run in a thread. We do it before calling
	 * progress/action changed so they can update the task on time */
	for (item = priv->leader; item; item = brasero_task_item_previous (item)) {
		BraseroTaskItemIFace *klass;

		klass = BRASERO_TASK_ITEM_GET_CLASS (item);
		if (klass->clock_tick)
			klass->clock_tick (item, BRASERO_TASK_CTX (task), NULL);
	}

	/* now call ctx to update progress */
	brasero_task_ctx_report_progress (BRASERO_TASK_CTX (data));

	return TRUE;
}

/**
 * Used to run/stop task
 */

static BraseroBurnResult
brasero_task_deactivate_item (BraseroTask *task,
			      BraseroTaskItem *item,
			      GError **error)
{
	BraseroBurnResult result = BRASERO_BURN_OK;
	BraseroTaskItemIFace *klass;

	if (!brasero_task_item_is_active (item)) {
		BRASERO_BURN_LOG ("%s already stopped", G_OBJECT_TYPE_NAME (item));
		return BRASERO_BURN_OK;
	}

	/* stop task for real now */
	BRASERO_BURN_LOG ("stopping %s", G_OBJECT_TYPE_NAME (item));

	klass = BRASERO_TASK_ITEM_GET_CLASS (item);
	if (klass->stop)
		result = klass->stop (item,
				      BRASERO_TASK_CTX (task),
				      error);

	BRASERO_BURN_LOG ("stopped %s", G_OBJECT_TYPE_NAME (item));

	return result;
}

static BraseroBurnResult
brasero_task_send_stop_signal (BraseroTask *task,
			       BraseroBurnResult retval,
			       GError **error)
{
	BraseroTaskItem *item;
	BraseroTaskPrivate *priv;
	GError *local_error = NULL;
	BraseroBurnResult result = retval;

	priv = BRASERO_TASK_PRIVATE (task);

	item = priv->leader;
	while (brasero_task_item_previous (item))
		item = brasero_task_item_previous (item);

	/* we stop all the slaves first and then go up the list */
	for (; item; item = brasero_task_item_next (item)) {
		GError *item_error;

		item_error = NULL;

		/* stop task for real now */
		result = brasero_task_deactivate_item (task, item, &item_error);
		if (item_error) {
			g_error_free (local_error);
			local_error = item_error;
		}
	};

	if (local_error) {
		if (error && *error == NULL)
			g_propagate_error (error, local_error);
		else
			g_error_free (local_error);
	}

	/* we don't want to lose the original result if it was not OK */
	return (result == BRASERO_BURN_OK? retval:result);
}

static BraseroBurnResult
brasero_task_start_item (BraseroTask *task,
			 BraseroTaskItem *item,
			 GError **error)
{
	BraseroBurnResult result;
	BraseroTaskItemIFace *klass;

	klass = BRASERO_TASK_ITEM_GET_CLASS (item);
	if (!klass->start)
		return BRASERO_BURN_ERR;

	BRASERO_BURN_LOG ("::start method %s", G_OBJECT_TYPE_NAME (item));

	result = klass->start (item, error);
	return result;
}

static BraseroBurnResult
brasero_task_activate_item (BraseroTask *task,
			    BraseroTaskItem *item,
			    GError **error)
{
	BraseroTaskItemIFace *klass;
	BraseroBurnResult result = BRASERO_BURN_OK;

	klass = BRASERO_TASK_ITEM_GET_CLASS (item);
	if (!klass->activate)
		return BRASERO_BURN_ERR;

	BRASERO_BURN_LOG ("::activate method %s", G_OBJECT_TYPE_NAME (item));

	result = klass->activate (item, BRASERO_TASK_CTX (task), error);
	return result;
}

static void
brasero_task_stop (BraseroTask *task,
		   BraseroBurnResult retval,
		   GError *error)
{
	BraseroBurnResult result;
	BraseroTaskPrivate *priv;

	priv = BRASERO_TASK_PRIVATE (task);

	/* brasero_job_error/brasero_job_finished should not be called during
	 * ::init and ::start. Instead a job should return any error directly */
	result = brasero_task_send_stop_signal (task, retval, &error);

	priv->retval = retval;
	priv->error = error;

	if (priv->loop && g_main_loop_is_running (priv->loop))
		g_main_loop_quit (priv->loop);
	else
		BRASERO_BURN_LOG ("task was asked to stop (%i/%i) during ::init or ::start",
				  result, retval);
}

BraseroBurnResult
brasero_task_cancel (BraseroTask *task,
		     gboolean protect)
{
	BraseroTaskPrivate *priv;

	priv = BRASERO_TASK_PRIVATE (task);
	if (protect && brasero_task_ctx_get_dangerous (BRASERO_TASK_CTX (task)))
		return BRASERO_BURN_DANGEROUS;

	brasero_task_stop (task, BRASERO_BURN_CANCEL, NULL);
	return BRASERO_BURN_OK;
}

gboolean
brasero_task_is_running (BraseroTask *task)
{
	BraseroTaskPrivate *priv;

	priv = BRASERO_TASK_PRIVATE (task);
	return (priv->loop && g_main_loop_is_running (priv->loop));
}

static void
brasero_task_finished (BraseroTaskCtx *ctx,
		       BraseroBurnResult retval,
		       GError *error)
{
	BraseroTask *self;
	BraseroTaskPrivate *priv;

	self = BRASERO_TASK (ctx);
	priv = BRASERO_TASK_PRIVATE (self);

	/* see if we have really started a loop */
	/* FIXME: shouldn't it be an error if it is called while the loop is 
	 * not running ? */
	if (!brasero_task_is_running (self))
		return;
		
	if (retval == BRASERO_BURN_RETRY) {
		BraseroTaskItem *item;
		GError *error_item = NULL;

		/* There are some tracks left, get the first task item and
		 * restart it. */
		item = priv->leader;
		while (brasero_task_item_previous (item))
			item = brasero_task_item_previous (item);

		if (brasero_task_item_start (item, &error_item) != BRASERO_BURN_OK)
			brasero_task_stop (self, BRASERO_BURN_ERR, error_item);

		return;
	}

	brasero_task_stop (self, retval, error);
}

static BraseroBurnResult
brasero_task_run_loop (BraseroTask *self,
		       GError **error)
{
	BraseroTaskPrivate *priv;

	priv = BRASERO_TASK_PRIVATE (self);

	brasero_task_ctx_report_progress (BRASERO_TASK_CTX (self));

	priv->clock_id = g_timeout_add (500,
					brasero_task_clock_tick,
					self);

	priv->loop = g_main_loop_new (NULL, FALSE);
	BRASERO_BURN_LOG ("entering loop");
	g_main_loop_run (priv->loop);
	BRASERO_BURN_LOG ("got out of loop");
	g_main_loop_unref (priv->loop);
	priv->loop = NULL;

	if (priv->error) {
		g_propagate_error (error, priv->error);
		priv->error = NULL;
	}

	/* stop all progress reporting thing */
	if (priv->clock_id) {
		g_source_remove (priv->clock_id);
		priv->clock_id = 0;
	}

	if (priv->retval == BRASERO_BURN_OK
	&&  brasero_task_ctx_get_progress (BRASERO_TASK_CTX (self), NULL) == BRASERO_BURN_OK) {
		brasero_task_ctx_set_progress (BRASERO_TASK_CTX (self), 1.0);
		brasero_task_ctx_report_progress (BRASERO_TASK_CTX (self));
	}

	brasero_task_ctx_stop_progress (BRASERO_TASK_CTX (self));
	return priv->retval;	
}

static BraseroBurnResult
brasero_task_set_track_output_size_default (BraseroTask *self,
					    GError **error)
{
	BraseroTrackType input = { 0 };
	BraseroTrack *track = NULL;

	BRASERO_BURN_LOG ("Trying to set a default output size");

	brasero_task_ctx_get_current_track (BRASERO_TASK_CTX (self), &track);
	brasero_track_get_type (track, &input);
	if (input.type == BRASERO_TRACK_TYPE_IMAGE) {
		BraseroBurnResult result;
		gint64 sectors = 0;
		gint64 size = 0;

		result = brasero_track_get_image_size (track,
						       NULL,
						       &sectors,
						       &size,
						       error);
		if (result != BRASERO_BURN_OK)
			return result;

		BRASERO_BURN_LOG ("Got a default image track length %lli", sectors);

		brasero_task_ctx_set_output_size_for_current_track (BRASERO_TASK_CTX (self),
								    sectors,
								    size);
	}
	else if (input.type == BRASERO_TRACK_TYPE_AUDIO) {
		gint64 length;

		length = 0;
		brasero_track_get_audio_length (track, &length);
		BRASERO_BURN_LOG ("Got a default audio track length %lli", length);

		brasero_task_ctx_set_output_size_for_current_track (BRASERO_TASK_CTX (self),
								    BRASERO_DURATION_TO_SECTORS (length),
								    BRASERO_DURATION_TO_BYTES (length));
	}

	return BRASERO_BURN_OK;
}

static BraseroBurnResult
brasero_task_activate_items (BraseroTask *self,
			     GError **error)
{
	BraseroTaskPrivate *priv;
	BraseroBurnResult retval;
	BraseroTaskItem *item;

	priv = BRASERO_TASK_PRIVATE (self);

	retval = BRASERO_BURN_NOT_RUNNING;
	for (item = priv->first; item; item = brasero_task_item_next (item)) {
		BraseroBurnResult result;

		result = brasero_task_activate_item (self, item, error);
		if (result == BRASERO_BURN_NOT_RUNNING) {
			BRASERO_BURN_LOG ("::start skipped for %s",
					  G_OBJECT_TYPE_NAME (item));
			continue;
		}

		if (result != BRASERO_BURN_OK)
			return result;

		retval = BRASERO_BURN_OK;
	}

	return retval;
}

static BraseroBurnResult
brasero_task_start_items (BraseroTask *self,
			  GError **error)
{
	BraseroBurnResult retval;
	BraseroTaskPrivate *priv;
	BraseroTaskItem *item;

	priv = BRASERO_TASK_PRIVATE (self);

	/* start from the master down to the slave */
	retval = BRASERO_BURN_NOT_SUPPORTED;
	for (item = priv->leader; item; item = brasero_task_item_previous (item)) {
		BraseroBurnResult result;

		if (!brasero_task_item_is_active (item))
			continue;

		result = brasero_task_start_item (self, item, error);
		if (result == BRASERO_BURN_NOT_SUPPORTED) {
			BRASERO_BURN_LOG ("%s doesn't support action",
					  G_OBJECT_TYPE_NAME (item));

			/* "fake mode" to get size. Forgive the jobs that cannot
			 * retrieve the size for one track. Just deactivate and
			 * go on with the next.
			 * NOTE: after this result the job is no longer active
			 */
			continue;
		}

		/* if the following is true don't stop everything */
		if (result == BRASERO_BURN_NOT_RUNNING)
			return result;

		if (result != BRASERO_BURN_OK)
			return result;

		retval = BRASERO_BURN_OK;
	}

	if (retval == BRASERO_BURN_NOT_SUPPORTED) {
		/* if all jobs did not want/could not run then resort to a
		 * default function and return BRASERO_BURN_NOT_RUNNING */
		retval = brasero_task_set_track_output_size_default (self, error);
		if (retval != BRASERO_BURN_OK)
			return retval;

		return BRASERO_BURN_NOT_RUNNING;
	}

	return brasero_task_run_loop (self, error);
}

static BraseroBurnResult
brasero_task_start (BraseroTask *self,
		    gboolean fake,
		    GError **error)
{
	BraseroBurnResult result = BRASERO_BURN_OK;
	BraseroTaskPrivate *priv;

	priv = BRASERO_TASK_PRIVATE (self);

	BRASERO_BURN_LOG ("Starting %s task (%i)",
			  fake ? "fake":"normal",
			  brasero_task_ctx_get_action (BRASERO_TASK_CTX (self)));

	/* check the task is not running */
	if (brasero_task_is_running (self)) {
		BRASERO_BURN_LOG ("task is already running");
		return BRASERO_BURN_RUNNING;
	}

	if (!priv->leader) {
		BRASERO_BURN_LOG ("no jobs");
		return BRASERO_BURN_RUNNING;
	}

	brasero_task_ctx_set_fake (BRASERO_TASK_CTX (self), fake);
	brasero_task_ctx_reset (BRASERO_TASK_CTX (self));

	/* Activate all items that can be. If no item can be then skip */
	result = brasero_task_activate_items (self, error);
	if (result == BRASERO_BURN_NOT_RUNNING) {
		BRASERO_BURN_LOG ("task skipped");
		return BRASERO_BURN_OK;
	}

	if (result != BRASERO_BURN_OK)
		return result;

	result = brasero_task_start_items (self, error);
	while (result == BRASERO_BURN_NOT_RUNNING) {
		BRASERO_BURN_LOG ("current track skipped");

		/* this track was skipped without actual loop therefore see if
		 * there is another track and, if there is, start again */
		result = brasero_task_ctx_next_track (BRASERO_TASK_CTX (self));
		if (result != BRASERO_BURN_RETRY) {
			brasero_task_send_stop_signal (self, result, NULL);
			return result;
		}

		result = brasero_task_start_items (self, error);
	}

	if (result != BRASERO_BURN_OK)
		brasero_task_send_stop_signal (self, result, NULL);

	return result;
}

BraseroBurnResult
brasero_task_check (BraseroTask *self,
		    GError **error)
{
	BraseroTaskAction action;

	g_return_val_if_fail (BRASERO_IS_TASK (self), BRASERO_BURN_ERR);

	/* the task MUST be of a BRASERO_TASK_ACTION_NORMAL type */
	action = brasero_task_ctx_get_action (BRASERO_TASK_CTX (self));
	if (action != BRASERO_TASK_ACTION_NORMAL)
		return BRASERO_BURN_OK;

	/* The main purpose of this function is to get the final size of the
	 * task output whether it be recorded to disc or stored as a file later.
	 * That size will be stored by task-ctx.
	 * To do this we run all the task in fake mode that means we don't write
	 * anything to disc or hard drive. Only the last running job in the
	 * chain will be aware that we're running in fake mode / get-size mode.
	 * All others will be told to image. To determine what should be the
	 * last job and therefore the one telling the final size of the output,
	 * we start to call ::init for each job starting from the leader. we
	 * don't skip recording jobs in case they modify the contents (and
	 * therefore the output size). When a job returns NOT_RUNNING after
	 * ::init then we skip it; this return value will mean that it won't
	 * change the output size or that it has already determined the output
	 * size. Only the last running job is allowed to set the final size (see
	 * burn-jobs.c), all values from other jobs will be ignored. */

	return brasero_task_start (self, TRUE, error);
}

BraseroBurnResult
brasero_task_run (BraseroTask *self,
		  GError **error)
{
	g_return_val_if_fail (BRASERO_IS_TASK (self), BRASERO_BURN_ERR);

	return brasero_task_start (self, FALSE, error);
}

static void
brasero_task_class_init (BraseroTaskClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	BraseroTaskCtxClass *ctx_class = BRASERO_TASK_CTX_CLASS (klass);

	g_type_class_add_private (klass, sizeof (BraseroTaskPrivate));

	parent_class = g_type_class_peek_parent(klass);
	object_class->finalize = brasero_task_finalize;

	ctx_class->finished = brasero_task_finished;
}

static void
brasero_task_init (BraseroTask *obj)
{ }

static void
brasero_task_finalize (GObject *object)
{
	BraseroTask *cobj;
	BraseroTaskPrivate *priv;

	cobj = BRASERO_TASK (object);
	priv = BRASERO_TASK_PRIVATE (cobj);

	if (priv->leader) {
		g_object_unref (priv->leader);
		priv->leader = NULL;
	}

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

BraseroTask *
brasero_task_new ()
{
	BraseroTask *obj;

	obj = BRASERO_TASK (g_object_new (BRASERO_TYPE_TASK, NULL));
	return obj;
}
