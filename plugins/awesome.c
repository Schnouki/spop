/*
 * Copyright (C) 2010, 2011, 2012 Thomas Jost
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

#include <dbus/dbus-glib.h>
#include <glib.h>
#include <gmodule.h>
#include <libspotify/api.h>

#include "spop.h"
#include "interface.h"
#include "queue.h"
#include "spotify.h"
#include "utils.h"

#define AWESOME_DBUS_SERVICE   "org.naquadah.awesome.awful"
#define AWESOME_DBUS_PATH      "/"
#define AWESOME_DBUS_INTERFACE "org.naquadah.awesome.awful.Remote"
#define AWESOME_DBUS_METHOD    "Eval"

static DBusGConnection* g_connection = NULL;
static DBusGProxy* g_proxy = NULL;

#define col(c, txt) "<span foreground=\"" c "\">" txt "</span>"

static void set_text(const gchar* txt) {
    GError* err = NULL;
    GString* str;

    str = g_string_sized_new(256);
    g_string_printf(str, "tb_spop.text=\" %s \"\n", txt);
    if (!dbus_g_proxy_call(g_proxy, AWESOME_DBUS_METHOD, &err,
                           G_TYPE_STRING, str->str,
                           G_TYPE_INVALID, G_TYPE_INVALID))
        g_warning("Could not send command to Awesome via D-Bus: %s", err->message);
    g_string_free(str, TRUE);
}

static void notification_callback(const GString* status, gpointer data) {
    queue_status qs;
    gboolean repeat, shuffle;
    sp_track* cur_track;
    int cur_track_nb;
    int tot_tracks;

    GString* text;
    GString* rep_shuf;

    /* Read full status */
    qs = queue_get_status(&cur_track, &cur_track_nb, &tot_tracks);
    repeat = queue_get_repeat();
    shuffle = queue_get_shuffle();

    /* Prepare the data to display */
    text = g_string_sized_new(1024);
    rep_shuf = g_string_sized_new(40);

    if (repeat || shuffle)
        g_string_printf(rep_shuf, " [<b>" col("#daf", "%s") "</b>]",
                        repeat ? (shuffle ? "rs" : "r") : "s");

    if (qs == STOPPED)
        g_string_printf(text, "[stopped]%s", rep_shuf->str);
    else {
        /* Read more data */
        int track_min, track_sec, pos_min, pos_sec;
        gchar* track_name;
        gchar* track_artist;
        GString* short_title = NULL;

        track_get_data(cur_track, &track_name, &track_artist, NULL, NULL, &track_sec, NULL);
        pos_sec = session_play_time();
        track_min = track_sec / 60;
        track_sec %= 60;
        pos_min = pos_sec / 60;
        pos_sec %= 60;

        /* Prepare data to display */
        if (qs == PAUSED)
            g_string_append(text, "<b>[p]</b> ");

        short_title = g_string_new(track_name);
        if (short_title->len >= 30)
            g_string_overwrite(short_title, 30, "…");

        g_string_append_printf(text,
                               "[<b>" col("#afd", "%d") ":</b> " col("#adf", "%s") " / " col("#fad", "%s") "]"
                               " [<b>" col("#dfa", "%d:%02d") "</b>/" col("#dfa", "%d:%02d") "]%s",
                               cur_track_nb+1, track_artist, short_title->str,
                               pos_min, pos_sec, track_min, track_sec, rep_shuf->str);

        /* Free some memory */
        g_string_free(short_title, TRUE);
        g_free(track_name);
        g_free(track_artist);
    }
    g_string_free(rep_shuf, TRUE);

    /* Replace " with \" and & with &amp; */
    g_string_replace(text, "\"", "\\\"");
    g_string_replace(text, "&", "&amp;");

    /* Send to Awesome */
    set_text(text->str);

    g_string_free(text, TRUE);
}

static gboolean timeout_callback(gpointer data) {
    notification_callback(NULL, NULL);
    return TRUE;
}

G_MODULE_EXPORT void spop_awesome_init() {
    GError* err = NULL;

    /* Init D-Bus */
    g_type_init();
    g_connection = dbus_g_bus_get(DBUS_BUS_SESSION, &err);
    if (!g_connection)
        g_error("Can't connect to D-Bus: %s", err->message);

    g_proxy = dbus_g_proxy_new_for_name(g_connection,
                                        AWESOME_DBUS_SERVICE, AWESOME_DBUS_PATH, AWESOME_DBUS_INTERFACE);

    /* Add a notification callback */
    if (!interface_notify_add_callback(notification_callback, NULL))
        g_error("Could not add Awesome callback.");

    /* Add a timeout callback */
    g_timeout_add_seconds(1, timeout_callback, NULL);

    /* Display something */
    //set_text("[spop]");
    notification_callback(NULL, NULL);
}

G_MODULE_EXPORT void spop_awesome_close() {
    /* Restore a neutral message when exiting */
    set_text("[spop]");
}
