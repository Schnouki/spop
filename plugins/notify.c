/*
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
 */

#include <glib.h>
#include <gmodule.h>
#include <libnotify/notify.h>
#include <libspotify/api.h>

#include "spop.h"
#include "interface.h"
#include "queue.h"
#include "spotify.h"

static NotifyNotification* g_notif = NULL;

#define col(c, txt) "<span foreground=\"" c "\">" txt "</span>"

void notification_callback(const GString* status, gpointer data) {
    queue_status qs;
    sp_track* cur_track;
    int cur_track_nb;
    int tot_tracks;

    GString* body;
    gchar* pos_beg = NULL;
    gchar* pos_ptr = NULL;

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
        int track_min, track_sec;
        const char* track_name;
        GString* track_artist = NULL;
        GString* track_album = NULL;
        GString* track_link = NULL;

        repeat = queue_get_repeat();
        shuffle = queue_get_shuffle();
        track_get_data(cur_track, &track_name, &track_artist, &track_album, &track_link, &track_min, &track_sec);

        /* Prepare data to display */
        if (qs == PAUSED)
            g_string_append(body, "<b>[paused]</b>\n");

        g_string_append_printf(body, "\nNow playing track <b>" col("#afd", "%d") "</b>/" col("#afd", "%d") ":\n\n"
                               "\t<b>" col("#fad", "%s") "</b>\n"    /* title */
                               "by\t<b>" col("#adf", "%s") "</b>\n"  /* artist */
                               "from\t" col("#fda", "%s"),           /* album */
                               cur_track_nb+1, tot_tracks, track_name, track_artist->str, track_album->str);
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
        g_string_free(track_artist, TRUE);
        g_string_free(track_album, TRUE);
        g_string_free(track_link, TRUE);
    }

    /* Replace "&" with "&amp;" */
    pos_beg = body->str;
    while ((pos_ptr = g_strstr_len(pos_beg, (body->len - (pos_beg - body->str)), "&")) != NULL) {
        gssize pos = pos_ptr - body->str;
        g_debug("Notify: escaping & at position %ld", pos);
        g_string_erase(body, pos, 1);
        g_string_insert_len(body, pos, "&amp;", 5); 
        pos_beg = pos_ptr + 1;
   }

    /* Create or update the notification */
    if (!g_notif) {
        g_notif = notify_notification_new("spop update", body->str, NULL, NULL);
        notify_notification_set_timeout(g_notif, 8000);
        notify_notification_set_urgency(g_notif, NOTIFY_URGENCY_LOW);
    }
    else
        notify_notification_update(g_notif, "spop update", body->str, NULL);

    if (!notify_notification_show(g_notif, &err))
        g_error("Can't show notification: %s", err->message);

    g_string_free(body, TRUE);
}

G_MODULE_EXPORT void spop_notify_init() {
    if (!notify_init(g_get_prgname()))
        g_error("Can't initialize libnotify.");

    if (!interface_notify_add_callback(notification_callback, NULL))
        g_error("Could not add libnotify callback.");
}
