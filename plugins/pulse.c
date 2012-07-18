/*
 * Copyright (C) 2012 Thomas Jost
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
#include <pulse/pulseaudio.h>
#include <pulse/glib-mainloop.h>
#include <string.h>

#include "audio.h"

static gboolean g_starting     = FALSE;
static gboolean g_started      = FALSE;
static gboolean g_stream_ready = FALSE;

static pa_glib_mainloop* g_main_loop = NULL;
static pa_mainloop_api*  g_api       = NULL;
static pa_context*       g_ctx       = NULL;
static pa_stream*        g_stream    = NULL;

static int               g_latency = 500000;
static pa_sample_spec    g_ss;
static pa_buffer_attr    g_bufattr;

static GStaticRecMutex g_pulse_mutex = G_STATIC_REC_MUTEX_INIT;

#define pulse_check(op) { int ret = (op); if (ret != 0) { g_error("PulseAudio: %s: %s", #op, pa_strerror(ret)); } }
#define pulse_check_val(val) { if (!(val)) { int err = pa_context_errno(g_ctx); g_error("PulseAudio: %s is NULL: %s", #val, pa_strerror(err)); } }

/* Private callbacks */
static void pulse_state_cb(pa_context* c, gpointer userdata) {
    g_static_rec_mutex_lock(&g_pulse_mutex);
    pa_context_state_t state = pa_context_get_state(c);
    gchar* st;
    switch(state) {
    case PA_CONTEXT_UNCONNECTED : st = "unconnected";  break;
    case PA_CONTEXT_CONNECTING  : st = "connecting"; break;
    case PA_CONTEXT_AUTHORIZING : st = "authorizing"; break;
    case PA_CONTEXT_SETTING_NAME: st = "setting name"; break;
    case PA_CONTEXT_READY       : st = "ready"; break;
    case PA_CONTEXT_FAILED      : st = "failed"; break;
    case PA_CONTEXT_TERMINATED  : st = "terminated"; break;
    default: st = "unknown";
    }
    g_debug("PulseAudio context state: %s (%d)", st, state);

    g_started = (state == PA_CONTEXT_READY);
    if (g_started) g_starting = FALSE;
    g_static_rec_mutex_unlock(&g_pulse_mutex);
}

static void pulse_stream_state_cb(pa_stream* s, gpointer userdata) {
    g_static_rec_mutex_lock(&g_pulse_mutex);
    pa_stream_state_t state = pa_stream_get_state(s);
    gchar* st;
    switch(state) {
    case PA_STREAM_UNCONNECTED: st = "unconnected"; break;
    case PA_STREAM_CREATING:    st = "creating"; break;
    case PA_STREAM_READY:       st = "ready"; break;
    case PA_STREAM_FAILED:      st = "failed"; break;
    case PA_STREAM_TERMINATED:  st = "terminated"; break;
    default: st = "unknown";
    }
    g_debug("PulseAudio stream state: %s (%d)", st, state);

    g_stream_ready = (state == PA_STREAM_READY);
    g_static_rec_mutex_unlock(&g_pulse_mutex);
}

static void pulse_underflow_cb(pa_stream* s, gpointer userdata) {
    g_static_rec_mutex_lock(&g_pulse_mutex);
    static int underflows = 0;
    g_debug("PulseAudio: underflow");
    if ((++underflows >= 5) && (g_latency < 4000000)) {
        underflows = 0;
        g_latency = (g_latency * 3) / 2;
        g_bufattr.maxlength = pa_usec_to_bytes(g_latency, &g_ss);
        g_bufattr.tlength = pa_usec_to_bytes(g_latency, &g_ss);
        pulse_check_val(pa_stream_set_buffer_attr(s, &g_bufattr, NULL, NULL));
        g_debug("PulseAudio: latency increased to %d", g_latency);
    }
    g_static_rec_mutex_unlock(&g_pulse_mutex);
}

/* "Private" function used to set up the PulseAudio connection */
static void pulse_open() {
    if (g_starting)
        return;

    g_debug("Starting PulseAudio");
    g_starting = TRUE;

    g_setenv("PULSE_PROP_media.role", "music", TRUE);

    if (!g_main_loop)
        g_main_loop = pa_glib_mainloop_new(NULL);
    if (!g_api)
        g_api = pa_glib_mainloop_get_api(g_main_loop);

    if (!g_ctx) {
        g_ctx = pa_context_new(g_api, "spop");
        pulse_check_val(g_ctx);
        pa_context_set_state_callback(g_ctx, pulse_state_cb, NULL);
        pulse_check(pa_context_connect(g_ctx, NULL, PA_CONTEXT_NOFLAGS, NULL));
    }
}

/* "Public" function, called from a libspotify callback */
static int pulse_audio_delivery(const sp_audioformat* format, const void* frames, int num_frames) {
    static size_t frame_size = 0;

    if (!g_started) {
        pulse_open();
        return 0;
    }

    if (!g_stream) {
        g_debug("PulseAudio: creating stream");

        g_ss.format = PA_SAMPLE_S16NE;
        g_ss.rate = format->sample_rate;
        g_ss.channels = format->channels;
        g_bufattr.fragsize = (uint32_t)-1;
        g_bufattr.maxlength = pa_usec_to_bytes(g_latency, &g_ss);
        g_bufattr.minreq = pa_usec_to_bytes(0, &g_ss);
        g_bufattr.prebuf = (uint32_t)-1;
        g_bufattr.tlength = pa_usec_to_bytes(g_latency, &g_ss);

        frame_size = sizeof(int16_t) * format->channels;
        g_stream = pa_stream_new(g_ctx, "spop output", &g_ss, NULL);
        pulse_check_val(g_stream);
        pa_stream_set_state_callback(g_stream, pulse_stream_state_cb, NULL);
        pa_stream_set_underflow_callback(g_stream, pulse_underflow_cb, NULL);
        pulse_check(pa_stream_connect_playback(g_stream, NULL, &g_bufattr,
                                               PA_STREAM_ADJUST_LATENCY | PA_STREAM_AUTO_TIMING_UPDATE | PA_STREAM_INTERPOLATE_TIMING,
                                               NULL, NULL));
    }

    if (!g_stream_ready)
        return 0;

    /* What are we supposed to do here? */
    if (num_frames == 0) {
        /* Pause: flush the stream */
        g_debug("PulseAudio: flushing output");
        pulse_check_val(pa_stream_flush(g_stream, NULL, NULL));
        return 0;
    }
    else if (pa_stream_writable_size(g_stream) == 0) {
        return 0;
    }
    else {
        gpointer buffer = NULL;
        size_t frames_size = frame_size * num_frames;
        size_t writable = -1;

        pulse_check(pa_stream_begin_write(g_stream, &buffer, &writable));
        if (!buffer) {
            g_debug("PulseAudio: null buffer");
            pulse_check(pa_stream_cancel_write(g_stream));
            return 0;
        }
        else if (writable == 0) {
            g_debug("PulseAudio: waiting");
            pulse_check(pa_stream_cancel_write(g_stream));
            return 0;
        }
        else {
            writable = (writable > frames_size) ? frames_size : writable;
            memcpy(buffer, frames, writable);
            pulse_check(pa_stream_write(g_stream, buffer, writable, NULL, 0, PA_SEEK_RELATIVE));
            return writable / frame_size;
        }
    }
}

G_MODULE_EXPORT int audio_delivery(const sp_audioformat* format, const void* frames, int num_frames) {
    g_static_rec_mutex_lock(&g_pulse_mutex);
    int ret = pulse_audio_delivery(format, frames, num_frames);
    g_static_rec_mutex_unlock(&g_pulse_mutex);
    return ret;
}
