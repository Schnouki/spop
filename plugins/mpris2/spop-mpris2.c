#include <glib.h>
#include <stdlib.h>

#include "spop.h"
#include "interface.h"
#include "queue.h"
#include "spotify.h"

#include "spop-mpris2.h"

/* {{{ Prototypes and global variables */
/* Prototypes of handled methods */
static gboolean spop_mpris2_raise(Mpris2* obj, GDBusMethodInvocation* invoc, gpointer user_data);
static gboolean spop_mpris2_quit(Mpris2* obj, GDBusMethodInvocation* invoc, gpointer user_data);

static gboolean spop_mpris2_player_next(Mpris2Player* obj, GDBusMethodInvocation* invoc);
static gboolean spop_mpris2_player_open_uri(Mpris2Player* obj, GDBusMethodInvocation* invoc, const gchar* arg_Uri);
static gboolean spop_mpris2_player_pause(Mpris2Player* obj, GDBusMethodInvocation* invoc);
static gboolean spop_mpris2_player_play(Mpris2Player* obj, GDBusMethodInvocation* invoc);
static gboolean spop_mpris2_player_play_pause(Mpris2Player* obj, GDBusMethodInvocation* invoc);
static gboolean spop_mpris2_player_previous(Mpris2Player* obj, GDBusMethodInvocation* invoc);
static gboolean spop_mpris2_player_seek(Mpris2Player* obj,  GDBusMethodInvocation* invoc, gint64 arg_Offset);
static gboolean spop_mpris2_player_set_position(Mpris2Player* obj, GDBusMethodInvocation* invoc, const gchar* arg_TrackId, gint64 arg_Position);
static gboolean spop_mpris2_player_stop(Mpris2Player* obj, GDBusMethodInvocation* invoc);

/* Prototypes for properties handling */
static void spop_mpris2_player_notification_callback(const GString* status, gpointer data);
static void spop_mpris2_player_update_properties(Mpris2Player* obj);
static gboolean spop_mpris2_player_update_position(Mpris2Player* obj);
static void on_spop_mpris2_player_set_loop_status(Mpris2Player* obj, GParamSpec* pspec, gpointer user_data);
static void on_spop_mpris2_player_set_rate(Mpris2Player* obj, GParamSpec* pspec, gpointer user_data);
static void on_spop_mpris2_player_set_shuffle(Mpris2Player* obj, GParamSpec* pspec, gpointer user_data);
static void on_spop_mpris2_player_set_volume(Mpris2Player* obj, GParamSpec* pspec, gpointer user_data);

/* Useful constants */
static const gchar* MPRIS2_URI_SCHEMES[] = {"spotify", NULL};
/* }}} */
/* {{{ Init interfaces skeleton */
Mpris2* spop_mpris2_skeleton_new() {
    Mpris2* obj = mpris2__skeleton_new();
    if (!obj)
        g_error("Can't init Mpris2 object");

    /* Methods: connect signals */
    g_signal_connect(obj, "handle-raise", G_CALLBACK(spop_mpris2_raise), NULL);
    g_signal_connect(obj, "handle-quit",  G_CALLBACK(spop_mpris2_quit),  NULL);

    /* Properties: set values */
    mpris2__set_can_quit(obj, TRUE);
    mpris2__set_can_raise(obj, FALSE);
    mpris2__set_fullscreen(obj, FALSE);
    mpris2__set_can_set_fullscreen(obj, FALSE);
    mpris2__set_has_track_list(obj, FALSE); // TODO
    mpris2__set_identity(obj, "spop");
    mpris2__set_desktop_entry(obj, NULL); // FIXME?
    mpris2__set_supported_uri_schemes(obj, MPRIS2_URI_SCHEMES);
    mpris2__set_supported_mime_types(obj, NULL); // FIXME?

    return obj;
}

Mpris2Player* spop_mpris2_player_skeleton_new() {
    Mpris2Player* obj = mpris2_player_skeleton_new();
    if (!obj)
        g_error("Can't init Mpris2Player object");

    /* Methods: connect signals */
    g_signal_connect(obj, "handle-next",         G_CALLBACK(spop_mpris2_player_next),         NULL);
    g_signal_connect(obj, "handle-open-uri",     G_CALLBACK(spop_mpris2_player_open_uri),     NULL);
    g_signal_connect(obj, "handle-pause",        G_CALLBACK(spop_mpris2_player_pause),        NULL);
    g_signal_connect(obj, "handle-play",         G_CALLBACK(spop_mpris2_player_play),         NULL);
    g_signal_connect(obj, "handle-play-pause",   G_CALLBACK(spop_mpris2_player_play_pause),   NULL);
    g_signal_connect(obj, "handle-previous",     G_CALLBACK(spop_mpris2_player_previous),     NULL);
    g_signal_connect(obj, "handle-seek",         G_CALLBACK(spop_mpris2_player_seek),         NULL);
    g_signal_connect(obj, "handle-set-position", G_CALLBACK(spop_mpris2_player_set_position), NULL);
    g_signal_connect(obj, "handle-stop",         G_CALLBACK(spop_mpris2_player_stop),         NULL);

    /* Properties: set values for constants */
    mpris2_player_set_minimum_rate(obj, 1.0);
    mpris2_player_set_maximum_rate(obj, 1.0);
    mpris2_player_set_rate(obj, 1.0);
    mpris2_player_set_volume(obj, 1.0);
    mpris2_player_set_can_control(obj, TRUE);
    mpris2_player_set_can_go_next(obj, TRUE);
    mpris2_player_set_can_go_previous(obj, TRUE);
    mpris2_player_set_can_pause(obj, TRUE);
    mpris2_player_set_can_seek(obj, TRUE);

    /* Set initial values for properties */
    spop_mpris2_player_update_properties(obj);

    /* Connect signals to be notified when a client changes a property */
    g_signal_connect(obj, "notify::loop-status",     G_CALLBACK(on_spop_mpris2_player_set_loop_status),     NULL);
    g_signal_connect(obj, "notify::rate",            G_CALLBACK(on_spop_mpris2_player_set_rate),            NULL);
    g_signal_connect(obj, "notify::shuffle",         G_CALLBACK(on_spop_mpris2_player_set_shuffle),         NULL);
    g_signal_connect(obj, "notify::volume",          G_CALLBACK(on_spop_mpris2_player_set_volume),          NULL);

    /* Add some callbacks to be update the timer and to be notified of track changes */
    if (!interface_notify_add_callback(spop_mpris2_player_notification_callback, (gpointer) obj))
        g_error("Could not add MPRIS2 callback.");

    g_timeout_add(200, (GSourceFunc) spop_mpris2_player_update_position, (gpointer) obj);

    return obj;
}
/* }}} */
/* {{{ org.mpris.MediaPlayer2 methods implementation */
gboolean spop_mpris2_raise(Mpris2* obj, GDBusMethodInvocation* invoc, gpointer user_data) {
    g_debug("mpris2: raise");
    mpris2__complete_raise(obj, invoc);
    return TRUE;
}

gboolean spop_mpris2_quit(Mpris2* obj, GDBusMethodInvocation* invoc, gpointer user_data) {
    g_debug("mpris2: quit");
    mpris2__complete_quit(obj, invoc);
    exit(0);
    return TRUE;
}
/* }}} */
/* {{{ org.mpris.MediaPlayer2.Player methods implementation */
gboolean spop_mpris2_player_play(Mpris2Player* obj, GDBusMethodInvocation* invoc) {
    g_debug("mpris2: play");
    queue_play(TRUE);
    mpris2_player_complete_play(obj, invoc);
    return TRUE;
}
gboolean spop_mpris2_player_play_pause(Mpris2Player* obj, GDBusMethodInvocation* invoc) {
    g_debug("mpris2: play_pause");
    queue_toggle(TRUE);
    mpris2_player_complete_play_pause(obj, invoc);
    return TRUE;
}
gboolean spop_mpris2_player_pause(Mpris2Player* obj, GDBusMethodInvocation* invoc) {
    g_debug("mpris2: pause");
    queue_status qs = queue_get_status(NULL, NULL, NULL);
    if (qs == PLAYING)
        queue_toggle(TRUE);
    mpris2_player_complete_pause(obj, invoc);
    return TRUE;
}
gboolean spop_mpris2_player_stop(Mpris2Player* obj, GDBusMethodInvocation* invoc) {
    g_debug("mpris2: stop");
    queue_stop(TRUE);
    mpris2_player_complete_stop(obj, invoc);
    return TRUE;
}

gboolean spop_mpris2_player_seek(Mpris2Player* obj,  GDBusMethodInvocation* invoc, gint64 arg_Offset) {
    g_debug("mpris2: seek");
    guint pos = session_play_time() + (arg_Offset / 1000);
    session_seek(pos);
    mpris2_player_emit_seeked(obj, pos * 1000);
    mpris2_player_complete_seek(obj, invoc);
    return TRUE;
}
gboolean spop_mpris2_player_set_position(Mpris2Player* obj, GDBusMethodInvocation* invoc, const gchar* arg_TrackId, gint64 arg_Position) {
    g_debug("mpris2: set_position");
    int pos = arg_Position / 1000;
    sp_track* track = NULL;
    queue_get_status(&track, NULL, NULL);

    if ((track != NULL) && (pos >= 0) && (pos <= sp_track_duration(track))) {
        // TODO: check arg_TrackId
        session_seek(pos);
        mpris2_player_emit_seeked(obj, pos * 1000);
    }
    mpris2_player_complete_set_position(obj, invoc);
    return TRUE;
}

gboolean spop_mpris2_player_next(Mpris2Player* obj, GDBusMethodInvocation* invoc) {
    g_debug("mpris2: next");
    queue_next(TRUE);
    mpris2_player_complete_next(obj, invoc);
    return TRUE;
}
gboolean spop_mpris2_player_previous(Mpris2Player* obj, GDBusMethodInvocation* invoc) {
    g_debug("mpris2: previous");
    queue_prev(TRUE);
    mpris2_player_complete_previous(obj, invoc);
    return TRUE;
}

gboolean spop_mpris2_player_open_uri(Mpris2Player* obj, GDBusMethodInvocation* invoc, const gchar* arg_Uri) {
    g_debug("mpris2: open_uri");
    // TODO
    mpris2_player_complete_open_uri(obj, invoc);
    return TRUE;
}
/* }}} */
/* {{{ org.mpris.MediaPlayer2.Player properties getters */
/* Callback to update properties */
void spop_mpris2_player_notification_callback(const GString* status, gpointer data) {
    spop_mpris2_player_update_properties((Mpris2Player*) data);
}

void spop_mpris2_player_update_properties(Mpris2Player* obj) {
    g_debug("mpris2: update properties");
    g_object_freeze_notify(G_OBJECT(obj));

    queue_status qs;
    sp_track* cur_track;
    int cur_track_nb, tot_tracks;

    qs = queue_get_status(&cur_track, &cur_track_nb, &tot_tracks);

    /* Easy ones first */
    mpris2_player_set_shuffle(obj,     queue_get_shuffle());
    mpris2_player_set_loop_status(obj, queue_get_repeat() ? "Playlist" : "None");
    mpris2_player_set_playback_status(obj, ((qs == PLAYING) ? "Playing" : ((qs == PAUSED) ? "Paused" : "Stopped")));
    mpris2_player_set_can_go_next(obj,     (cur_track_nb < tot_tracks));
    mpris2_player_set_can_go_previous(obj, (cur_track_nb > 0));
    mpris2_player_set_can_play(obj,  cur_track != NULL);
    mpris2_player_set_can_pause(obj, cur_track != NULL);

    /* Then boring ones */
    spop_mpris2_player_update_position(obj);

    /* And finally interesting ones */
    if (cur_track) {
        gchar* track_name;
        gchar* track_artist;
        gchar* track_album;
        gchar* track_link;
        int duration;

        track_get_data(cur_track, &track_name, &track_artist, &track_album, &track_link, &duration, NULL);

        /* Turn artist into a GVariant array of strings */
        gchar* artists[] = {track_artist, NULL};
        GVariant* va = g_variant_new_strv((const gchar* const*) artists, 1);

        /* Turn Spotify URI into a D-Bus object path */
        gchar* trackid = g_strdup_printf("/net/schnouki/spop/%s", track_link);
        g_strdelimit(trackid, ":", '/');

        /* Get filename for the image and turn it into an URI */
        gchar* image_filename;
        track_get_image_file(cur_track, &image_filename);
        gchar* image_uri = NULL;
        if (image_filename)
            image_uri = g_filename_to_uri(image_filename, NULL, NULL);

        /* Add all relevant metadata to a dictionary */
        GVariantBuilder* vb;
        vb = g_variant_builder_new(G_VARIANT_TYPE_VARDICT);

        g_variant_builder_add_parsed(vb, "{'mpris:trackid', <%o>}", trackid);
        g_variant_builder_add_parsed(vb, "{'mpris:length', <%x>}", duration*1000);
        g_variant_builder_add_parsed(vb, "{'xesam:album', <%s>}", track_album);
        g_variant_builder_add_parsed(vb, "{'xesam:artist', %v}", va);
        g_variant_builder_add_parsed(vb, "{'xesam:title', <%s>}", track_name);
        g_variant_builder_add_parsed(vb, "{'xesam:url', <%s>}", track_link);
        if (image_uri)
            g_variant_builder_add_parsed(vb, "{'mpris:artUrl', <%s>}", image_uri);

        GVariant* metadata = g_variant_builder_end(vb);
        mpris2_player_set_metadata(obj, metadata);

        /* Cleanup time */
        g_variant_builder_unref(vb);
        g_free(trackid);
        g_free(image_filename);
        if (image_uri)
            g_free(image_uri);

        g_free(track_name);
        g_free(track_artist);
        g_free(track_album);
        g_free(track_link);
    }
    g_object_thaw_notify(G_OBJECT(obj));
}

gboolean spop_mpris2_player_update_position(Mpris2Player* obj) {
    guint64 pos = session_play_time() * 1000;
    mpris2_player_set_position(obj, pos);
    return TRUE;
}
/* }}} */
/* {{{ org.mpris.MediaPlayer2.Player properties setters */
void on_spop_mpris2_player_set_loop_status(Mpris2Player* obj, GParamSpec* pspec, gpointer user_data) {
    gboolean repeat = queue_get_repeat();
    const gchar* loop_status = mpris2_player_get_loop_status(obj);
    g_debug("mpris2: set loop_status: %s, repeat: %s", loop_status, repeat ? "true" : "false");
    if (strcmp(loop_status, "None") == 0) {
        if (repeat)
            queue_set_repeat(TRUE, FALSE);
    }
    else if (strcmp(loop_status, "Playlist") == 0) {
        if (!repeat)
            queue_set_repeat(TRUE, TRUE);
    }
    else {
        /* Invalid: reset to current value */
        mpris2_player_set_loop_status(obj, repeat ? "Playlist" : "None");
    }
}
void on_spop_mpris2_player_set_rate(Mpris2Player* obj, GParamSpec* pspec, gpointer user_data) {
    gdouble rate = mpris2_player_get_rate(obj);
    if (rate != 1.0)
        mpris2_player_set_rate(obj, 1.0);
}
void on_spop_mpris2_player_set_shuffle(Mpris2Player* obj, GParamSpec* pspec, gpointer user_data) {
    gboolean new_shuffle = mpris2_player_get_shuffle(obj);
    gboolean cur_shuffle = queue_get_shuffle();
    if (new_shuffle != cur_shuffle)
        queue_set_shuffle(TRUE, new_shuffle);
}
void on_spop_mpris2_player_set_volume(Mpris2Player* obj, GParamSpec* pspec, gpointer user_data) {
    gdouble vol = mpris2_player_get_volume(obj);
    if (vol != 1.0)
        mpris2_player_set_volume(obj, 1.0);
}
/* }}} */
