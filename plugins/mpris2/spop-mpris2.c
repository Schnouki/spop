#include <glib.h>
#include <stdlib.h>
#include <string.h>

#include "spop.h"
#include "interface.h"
#include "queue.h"
#include "spotify.h"

#include "spop-mpris2.h"

/**
 * TODO:
 * - in the TrackList interface, if a track is present several times in the
 *   list, each occurence should have a different ID.
 */

/* {{{ Prototypes and global variables */
/* Helpers */
static gchar* spop_uri_to_trackid(gchar* uri);
static GVariant* spop_get_track_metadata(sp_track* track);

/* Global variables */
static GMutex last_trackids_mutex;
static gchar** last_trackids = NULL;

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

static gboolean spop_mpris2_tracklist_add_track(Mpris2TrackList* obj, GDBusMethodInvocation* invoc, const gchar* arg_Uri, const gchar* arg_AfterTrack, gboolean arg_SetAsCurrent);
static gboolean spop_mpris2_tracklist_get_tracks_metadata(Mpris2TrackList* obj, GDBusMethodInvocation* invoc, const gchar* const* arg_TrackIds);
static gboolean spop_mpris2_tracklist_go_to(Mpris2TrackList* obj, GDBusMethodInvocation* invoc, const gchar* arg_TrackId);
static gboolean spop_mpris2_tracklist_remove_track(Mpris2TrackList* obj, GDBusMethodInvocation* invoc, const gchar* arg_TrackId);

/* Prototypes for properties handling */
static void spop_mpris2_player_notification_callback(const GString* status, gpointer data);
static void spop_mpris2_player_update_properties(Mpris2Player* obj);
static gboolean spop_mpris2_player_update_position(Mpris2Player* obj);
static void on_spop_mpris2_player_set_loop_status(Mpris2Player* obj, GParamSpec* pspec, gpointer user_data);
static void on_spop_mpris2_player_set_rate(Mpris2Player* obj, GParamSpec* pspec, gpointer user_data);
static void on_spop_mpris2_player_set_shuffle(Mpris2Player* obj, GParamSpec* pspec, gpointer user_data);
static void on_spop_mpris2_player_set_volume(Mpris2Player* obj, GParamSpec* pspec, gpointer user_data);

static void spop_mpris2_tracklist_notification_callback(const GString* status, gpointer data);
static void spop_mpris2_tracklist_update_tracks(Mpris2TrackList* obj);

/* Useful constants */
static const gchar* MPRIS2_URI_SCHEMES[] = {"spotify", NULL};
/* }}} */
/* {{{ Helpers */
gchar* spop_uri_to_trackid(gchar* uri) {
    gchar* trackid = g_strdup_printf("/net/schnouki/spop/%s", uri);
    g_strdelimit(trackid, ":", '/');
    return trackid;
}

GVariant* spop_get_track_metadata(sp_track* track) {
    gchar* track_name;
    gchar* track_artist;
    gchar* track_album;
    gchar* track_link;
    int duration;
    int popularity;
    bool starred;

    track_get_data(track, &track_name, &track_artist, &track_album, &track_link, &duration, &popularity, &starred);

    /* Turn artist into a GVariant array of strings */
    gchar* artists[] = {track_artist, NULL};
    GVariant* va = g_variant_new_strv((const gchar* const*) artists, 1);

    /* Turn Spotify URI into a D-Bus object path */
    gchar* trackid = spop_uri_to_trackid(track_link);

    /* Get filename for the image and turn it into an URI */
    gchar* image_filename;
    track_get_image_file(track, &image_filename);
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
    g_variant_builder_add_parsed(vb, "{'xesam:autoRating', <%d>}", popularity / 100.);
    if (starred)
        g_variant_builder_add_parsed(vb, "{'xesam:userRating', <1.0>}");

    GVariant* metadata = g_variant_builder_end(vb);

    /* Cleanup */
    g_variant_builder_unref(vb);
    g_free(trackid);
    g_free(image_filename);
    if (image_uri)
        g_free(image_uri);

    g_free(track_name);
    g_free(track_artist);
    g_free(track_album);
    g_free(track_link);

    return metadata;
}
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

Mpris2TrackList* spop_mpris2_tracklist_skeleton_new() {
    Mpris2TrackList* obj = mpris2_track_list_skeleton_new();
    if (!obj)
        g_error("Can't init Mpris2TrackList object");
    Mpris2TrackListIface* iface = MPRIS2_TRACK_LIST_GET_IFACE(obj);

    /* Methods: connect signals */
    g_signal_connect(obj, "handle-add-track",           G_CALLBACK(spop_mpris2_tracklist_add_track), NULL);
    g_signal_connect(obj, "handle-get-tracks-metadata", G_CALLBACK(spop_mpris2_tracklist_get_tracks_metadata), NULL);
    g_signal_connect(obj, "handle-go-to",               G_CALLBACK(spop_mpris2_tracklist_go_to), NULL);
    g_signal_connect(obj, "handle-remove-track",        G_CALLBACK(spop_mpris2_tracklist_remove_track), NULL);

    /* Properties: set values for constants */
    mpris2_track_list_set_can_edit_tracks(obj, FALSE);

    /* Add some callbacks to be notified of track changes */
    if (!interface_notify_add_callback(spop_mpris2_tracklist_notification_callback, (gpointer) obj))
        g_error("Could not add MPRIS2 callback.");

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
        GVariant* metadata = spop_get_track_metadata(cur_track);
        mpris2_player_set_metadata(obj, metadata);
    }
    g_object_thaw_notify(G_OBJECT(obj));
}

gboolean spop_mpris2_player_update_position(Mpris2Player* obj) {
    guint64 pos = session_play_time() * 1000;
    guint64 prev_pos = mpris2_player_get_position(obj);
    mpris2_player_set_position(obj, pos);
    if (ABS(pos - prev_pos) >= 1000000) {
        mpris2_player_emit_seeked(obj, pos);
    }
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
/* {{{ org.mpris.MediaPlayer2.TrackList methods implementation */
gboolean spop_mpris2_tracklist_add_track(Mpris2TrackList* obj, GDBusMethodInvocation* invoc, const gchar* arg_Uri, const gchar* arg_AfterTrack, gboolean arg_SetAsCurrent) {
    /* NO-OP */
    g_debug("mpris2: tracklist_add_track");
    g_dbus_method_invocation_return_dbus_error(invoc, "org.mpris2.MediaPlayer2.spop", "This operation is not supported.");
    return TRUE;
}
gboolean spop_mpris2_tracklist_get_tracks_metadata(Mpris2TrackList* obj, GDBusMethodInvocation* invoc, const gchar* const* arg_TrackIds) {
    g_debug("mpris2: tracklist_get_tracks_metadata");

    /* Array of results */
    GVariantBuilder* vb;
    vb = g_variant_builder_new(G_VARIANT_TYPE_ARRAY);

    /* Tracks in the queue */
    GArray* queue = queue_tracks();

    /* Iterate over the requested trackids and add results if they are in the queue */
    size_t idx;
    const gchar* const* trackid;
    for (trackid = arg_TrackIds; *trackid != NULL; trackid++) {
        gboolean found = FALSE;
        for (idx = 0; !found && (idx < queue->len); idx++) {
            sp_track* tr = g_array_index(queue, sp_track*, idx);
            gchar* uri;
            track_get_data(tr, NULL, NULL, NULL, &uri, NULL, NULL, NULL);
            gchar* this_trackid = spop_uri_to_trackid(uri);
            if (g_strcmp0(this_trackid, *trackid) == 0) {
                /* Hit: get the metadata */
                found = TRUE;
                GVariant* metadata = spop_get_track_metadata(tr);
                g_variant_builder_add_value(vb, metadata);
            }
            g_free(uri);
            g_free(this_trackid);
        }
    }

    /* Finalize the builder */
    GVariant* tracks_metadata = g_variant_builder_end(vb);
    g_variant_builder_unref(vb);
    g_array_free(queue, TRUE);

    mpris2_track_list_complete_get_tracks_metadata(obj, invoc, tracks_metadata);
    return TRUE;
}
gboolean spop_mpris2_tracklist_go_to(Mpris2TrackList* obj, GDBusMethodInvocation* invoc, const gchar* arg_TrackId) {
    g_debug("mpris2: tracklist_go_to %s", arg_TrackId);

    gboolean found = FALSE;
    int track_idx = -1;
    gchar** trackid;
    g_mutex_lock(&last_trackids_mutex);
    for (trackid = last_trackids; *trackid != NULL; trackid++) {
        track_idx += 1;
        if (g_strcmp0(arg_TrackId, *trackid) == 0) {
            found = TRUE;
            break;
        }
    }
    g_mutex_unlock(&last_trackids_mutex);

    if (found) {
        g_debug("mpris2: tracklist_go_to %s --> %d", arg_TrackId, track_idx);
        queue_goto(TRUE, track_idx, TRUE);
        mpris2_track_list_complete_go_to(obj, invoc);
    }
    else {
        g_dbus_method_invocation_return_dbus_error(invoc, "org.mpris2.MediaPlayer2.spop", "This track is not in the current queue.");
    }

    return TRUE;
}
gboolean spop_mpris2_tracklist_remove_track(Mpris2TrackList* obj, GDBusMethodInvocation* invoc, const gchar* arg_TrackId) {
    /* NO-OP */
    g_debug("mpris2: tracklist_remove_track");
    g_dbus_method_invocation_return_dbus_error(invoc, "org.mpris2.MediaPlayer2.spop", "This operation is not supported.");
    return TRUE;
}
/* }}} */
/* {{{ org.mpris.MediaPlayer2.TrackList properties getters */
/* Callback to update properties */
void spop_mpris2_tracklist_notification_callback(const GString* status, gpointer data) {
    spop_mpris2_tracklist_update_tracks((Mpris2TrackList*) data);
}
void spop_mpris2_tracklist_update_tracks(Mpris2TrackList* obj) {
    g_debug("mpris2: tracklist_update_tracks");
    g_object_freeze_notify(G_OBJECT(obj));

    size_t idx;
    GArray* tracks = queue_tracks();
    gint nb_tracks = tracks->len;

    gchar** trackids = g_new0(gchar*, nb_tracks + 1);
    for (idx = 0; idx < nb_tracks; idx++) {
        gchar* uri;
        sp_track* tr = g_array_index(tracks, sp_track*, idx);
        track_get_data(tr, NULL, NULL, NULL, &uri, NULL, NULL, NULL);
        trackids[idx] = spop_uri_to_trackid(uri);
        g_free(uri);
    }
    trackids[nb_tracks] = NULL;
    g_array_free(tracks, TRUE);
    mpris2_track_list_set_tracks(obj, (const gchar* const*) trackids);

    /* Has the tracklist changed? */
    gboolean tracklist_changed = FALSE;
    g_mutex_lock(&last_trackids_mutex);
    if (!last_trackids) {
        tracklist_changed = TRUE;
    }
    else {
        for (idx = 0; idx <= nb_tracks; idx++) {
            if (g_strcmp0(trackids[idx], last_trackids[idx]) != 0) {
                tracklist_changed = TRUE;
                break;
            }
        }
    }
    if (tracklist_changed) {
        g_strfreev(last_trackids);
        last_trackids = trackids;

        /* Info about the current track */
        gint track_idx;
        gchar* current_track;
        queue_get_status(NULL, &track_idx, NULL);
        if (track_idx >= 0) {
            current_track = trackids[track_idx];
        }
        else {
            current_track = g_strdup("/org/mpris/MediaPlayer2/TrackList/NoTrack");
        }

        /* Emit the signal */
        mpris2_track_list_emit_track_list_replaced(obj, (const gchar* const*) trackids, current_track);
    }
    g_mutex_unlock(&last_trackids_mutex);

    g_object_thaw_notify(G_OBJECT(obj));
}
/* }}} */
