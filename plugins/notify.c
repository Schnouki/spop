/*
 * Copyright (C) 2010, 2011, 2012, 2013, 2014 Thomas Jost
 *
 * This file is part of spop.
 *
 * spop is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version.
 *
 * spop is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
 * A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * spop. If not, see <http://www.gnu.org/licenses/>.
 *
 * Additional permission under GNU GPL version 3 section 7
 *
 * If you modify this Program, or any covered work, by linking or combining it
 * with libspotify (or a modified version of that library), containing parts
 * covered by the terms of the Libspotify Terms of Use, the licensors of this
 * Program grant you additional permission to convey the resulting work.
 */

#include <glib.h>
#include <gmodule.h>
#include <libnotify/notify.h>
#include <libspotify/api.h>

#include "spop.h"
#include "config.h"
#include "interface.h"
#include "queue.h"
#include "spotify.h"
#include "utils.h"

#define NOTIF_TITLE         "spop update"
#define NOTIF_TIMEOUT       8000
#define NOTIF_IMAGE_TIMEOUT 100

static NotifyNotification* g_notif = NULL;
static gchar* g_notif_image_path = NULL;

#define col(c, txt) "<span foreground=\"" c "\">" txt "</span>"

static gboolean notif_set_image(sp_track* track) {
    GdkPixbuf* pb = NULL;
    GError* err = NULL;
    guchar* img_data = NULL;
    gsize img_size;
    gboolean res;

    if (!track_get_image_data(track, (gpointer*) &img_data, &img_size))
        /* Not loaded yet */
        return FALSE;

    if (!img_data) {
        /* No cover: we're done */
        g_debug("notif: image has no cover");
        return TRUE;
    }

    /* An image is present: save it to a file and update the notification */
    g_debug("notif: image loaded!");
    res = g_file_set_contents(g_notif_image_path, (gchar*) img_data, img_size, &err);
    g_free(img_data);
    if (!res) {
        g_info("notif: can't save image to file: %s", err->message);
        return TRUE;
    }

    gint wh = config_get_int_opt_group("notify", "image_size", 120);
    pb = gdk_pixbuf_new_from_file_at_size(g_notif_image_path, wh, wh, &err);
    if (!pb) {
        g_info("notif: can't create pixbuf from file: %s", err->message);
        return TRUE;
    }
    notify_notification_set_image_from_pixbuf(g_notif, pb);
    g_object_unref(pb);
    return TRUE;
}

/* Callback to update the notification once a track cover is loaded */
typedef struct {
    gchar* body;
    sp_track* track;
    guint nb_calls;
} notif_image_data;
static gboolean notif_image_callback(notif_image_data* nid) {
    queue_status qs;
    sp_track* cur_track;
    GError* err = NULL;
    int time_left = NOTIF_TIMEOUT - (nid->nb_calls * NOTIF_IMAGE_TIMEOUT);

    g_debug("notif: in image callback...");

    /* Is this timeout still valid/relevant? */
    qs = queue_get_status(&cur_track, NULL, NULL);
    if ((qs != STOPPED) && (cur_track == nid->track) && (time_left > 0)) {
        /* Yes: try to add the image */
        if (notif_set_image(cur_track)) {
            /* Ok: show the timeout and destroy it */
            notify_notification_set_timeout(g_notif, time_left);
            if (!notify_notification_show(g_notif, &err))
                g_info("Can't show notification: %s", err->message);
            goto nic_destroy;
        }
        else {
            /* Not loaded yet: wait again... */
            nid->nb_calls++;
            return TRUE;
        }
    }
    else {
        /* No: clean it */
        g_debug("notif: destroying old image callback");
        goto nic_destroy;
    }

 nic_destroy:
    g_free(nid->body);
    g_free(nid);
    return FALSE;
}

static void notification_callback(const GString* status, gpointer data) {
    queue_status qs;
    sp_track* cur_track;
    int cur_track_nb;
    int tot_tracks;

    GString* body;

    GError* err = NULL;

    /* Read full status */
    qs = queue_get_status(&cur_track, &cur_track_nb, &tot_tracks);

    /* Prepare the data to display */
    body = g_string_sized_new(1024);

    if (qs == STOPPED)
        g_string_printf(body, "<b>[stopped]</b>\n%d tracks in queue", tot_tracks);
    else {
        /* Read more data */
        gboolean repeat, shuffle;
        gchar* track_name;
        gchar* track_artist;
        gchar* track_album;

        repeat = queue_get_repeat();
        shuffle = queue_get_shuffle();
        track_get_data(cur_track, &track_name, &track_artist, &track_album, NULL, NULL, NULL);

        /* Prepare data to display */
        if (qs == PAUSED)
            g_string_append(body, "<b>[paused]</b>\n");

        g_string_append_printf(body, "\nNow playing track <b>" col("#afd", "%d") "</b>/" col("#afd", "%d") ":\n\n"
                               "\t<b>" col("#fad", "%s") "</b>\n"    /* title */
                               "by\t<b>" col("#adf", "%s") "</b>\n"  /* artist */
                               "from\t" col("#fda", "%s"),           /* album */
                               cur_track_nb+1, tot_tracks, track_name, track_artist, track_album);
        if (repeat || shuffle) {
            g_string_append(body, "\n\nMode: ");
            if (repeat) {
                g_string_append(body, "<b>" col("#daf", "repeat") "</b>");
                if (shuffle)
                    g_string_append(body, ", <b>" col("#daf", "shuffle") "</b>");
            }
            else if (shuffle)
                g_string_append(body, "<b>" col("#daf", "shuffle") "</b>");
        }

        /* Free some memory */
        g_free(track_name);
        g_free(track_artist);
        g_free(track_album);

        /* Replace "&" with "&amp;" */
        g_string_replace(body, "&", "&amp;");
    }

    /* Create or update the notification */
    if (!g_notif) {
        g_notif = notify_notification_new(NOTIF_TITLE, body->str, NULL);
        notify_notification_set_urgency(g_notif, NOTIFY_URGENCY_LOW);
    }
    else
        notify_notification_update(g_notif, NOTIF_TITLE, body->str, NULL);
    notify_notification_set_timeout(g_notif, NOTIF_TIMEOUT);
    notify_notification_set_image_from_pixbuf(g_notif, NULL);

    /* Add an image if needed */
    if ((qs != STOPPED) && config_get_bool_opt_group("notify", "use_images", TRUE)) {
        if (!notif_set_image(cur_track)) {
            /* Not loaded: add a timeout to try again later */
            notif_image_data* nid = g_malloc(sizeof(notif_image_data));
            nid->body = g_strdup(body->str);
            nid->track = cur_track;
            nid->nb_calls = 1;
            g_timeout_add(100, (GSourceFunc) notif_image_callback, (gpointer) nid);
        }
    }

    if (!notify_notification_show(g_notif, &err))
        g_info("Can't show notification: %s", err->message);

    g_string_free(body, TRUE);
}

G_MODULE_EXPORT void spop_notify_init() {
    if (!notify_init(g_get_prgname()))
        g_error("Can't initialize libnotify.");

    if (!interface_notify_add_callback(notification_callback, NULL))
        g_error("Could not add libnotify callback.");

    g_notif_image_path = g_build_filename(g_get_user_cache_dir(), g_get_prgname(), "notify-image.jpg", NULL);
}

G_MODULE_EXPORT void spop_notify_close() {
    GError* err = NULL;
    if (g_notif && !notify_notification_close(g_notif, &err))
        g_warning("Can't close notification: %s", err->message);
    notify_uninit();
    g_free(g_notif_image_path);
}
