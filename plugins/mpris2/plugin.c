/*
 * Copyright (C) 2012, 2013, 2014 Thomas Jost
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
#include <stdlib.h>

#include "spop.h"

#include "spop-mpris2.h"

/* Global variables */
static GDBusConnection* g_conn = NULL;
static guint g_bus_name_id = 0;
static Mpris2* g_iface = NULL;
static Mpris2Player* g_iface_player = NULL;
static Mpris2TrackList* g_iface_tracklist = NULL;

/* Plugin management */
static void on_bus_acquired(GDBusConnection* conn, const gchar* name, gpointer user_data) {
    GError* err = NULL;

    g_debug("mpris2: bus acquired on %p, exporting interfaces...", conn, g_iface);
    g_conn = conn;

    if (!g_dbus_interface_skeleton_export(G_DBUS_INTERFACE_SKELETON(g_iface),
                                          g_conn, "/org/mpris/MediaPlayer2", &err))
        g_error("mpris2: can't export MediaPlayer2: %s", err->message);

    if (!g_dbus_interface_skeleton_export(G_DBUS_INTERFACE_SKELETON(g_iface_player),
                                          g_conn, "/org/mpris/MediaPlayer2", &err))
        g_error("mpris2: can't export MediaPlayer2.Player: %s", err->message);

    if (!g_dbus_interface_skeleton_export(G_DBUS_INTERFACE_SKELETON(g_iface_tracklist),
                                          g_conn, "/org/mpris/MediaPlayer2", &err))
        g_error("mpris2: can't export MediaPlayer2.TrackList: %s", err->message);

    g_debug("mpris2: interfaces exported.");
}

static void on_name_acquired(GDBusConnection* conn, const gchar* name, gpointer user_data) {
    g_debug("mpris2: name acquired on %p", conn);
}

static void on_name_lost(GDBusConnection* conn, const gchar* name, gpointer user_data) {
    g_error("mpris2: name lost on %p", conn);
}

G_MODULE_EXPORT void spop_mpris2_init() {
    /* Prepare interfaces */
    g_iface = spop_mpris2_skeleton_new();
    if (!g_iface)
        g_error("Can't init Mpris2");

    g_iface_player = spop_mpris2_player_skeleton_new();
    if (!g_iface_player)
        g_error("Can't init Mpris2Player");

    g_iface_tracklist = spop_mpris2_tracklist_skeleton_new();
    if (!g_iface_tracklist)
        g_error("Can't init Mpris2TrackList");

    /* Connect to D-Bus and acquire name */
    g_bus_name_id = g_bus_own_name(G_BUS_TYPE_SESSION, "org.mpris.MediaPlayer2.spopd",
                                   G_BUS_NAME_OWNER_FLAGS_NONE | G_BUS_NAME_OWNER_FLAGS_REPLACE,
                                   on_bus_acquired, on_name_acquired, on_name_lost,
                                   NULL, NULL);
}

G_MODULE_EXPORT void spop_mpris2_close() {
    if (g_bus_name_id != 0) {
        g_bus_unown_name(g_bus_name_id);
        g_bus_name_id = 0;
    }

    if (g_conn) {
        g_object_unref(g_conn);
        g_conn = NULL;
    }

    if (g_iface) {
        g_object_unref(g_iface);
        g_iface = NULL;
    }

    if (g_iface_player) {
        g_object_unref(g_iface_player);
        g_iface_player = NULL;
    }
}
