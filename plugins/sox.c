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

#include <glib.h>
#include <gmodule.h>
#include <string.h>
#include <sox.h>

#include "spop.h"
#include "audio.h"
#include "config.h"

/* The SoX API is not the most pleasant to use.
 *
 * SoX uses the concept of an effects chain. At the beginning of this chain,
 * there is an "input" effect, and at the end an "output". Here the input is a
 * custom effect that reads libspotify frames from an internal buffer and decode
 * them (from "frames" to SoX samples). The output is probably an audio device
 * (ALSA, OSS or whatever), as requested by the user in the config file, and
 * controlled by SoX.
 *
 * So here the audio_delivery callback initializes SoX, starts audio output
 * (with _sox_start()), fills buffers with data provided by libspotify, and
 * closes audio output with _sox_stop(). _sox_start() opens the SoX output,
 * creates an effects chain using the data in the config file, and starts the
 * player thread. The player thread (_sox_player()) calls sox_flow_effects() to
 * run the effects chain from input to output, hence calling the
 * _sox_input_drain() callback as needed, and then makes some cleanup (close
 * effects chain and output). The _sox_input_drain() callback actually decodes
 * frames provided by libspotify into SoX samples, and feeds the rest of libsox
 * with these samples so that other effects can be applied.
 *
 * One problem remains: because of how SoX works, there can be audio output
 * *after* playback is stopped (echo, reverb, etc.). So to be able to precisely
 * control *when* the output end, we have to add another effect at the end of
 * the effects chain: that's _sox_output_flow() (and it sucks).
 *
 * Because of effects, stopping playback can take a little while. This is
 * probably not a desired behaviour.
 */

#define BUFSIZE 8192
#define BUFNB   8

/* Buffers */
typedef struct {
    size_t   size;
    gpointer buf;
} sox_buf;
static GQueue* g_free_bufs = NULL;
static GQueue* g_full_bufs = NULL;
static GMutex  g_buf_mutex;
static GCond   g_buf_cond;

/* Player thread control */
static GThread* g_player_thread = NULL;
static gboolean g_player_stop   = FALSE;

/* SoX settings */
static gboolean     g_sox_init     = FALSE;
static const gchar* g_sox_out_type = NULL;
static const gchar* g_sox_out_name = NULL;
static gchar**      g_sox_effects  = NULL;
static gsize        g_sox_effects_size;

/* SoX output */
static sox_format_t* g_sox_out = NULL;
static size_t        g_sox_frame_size;

/* SoX effects */
static sox_effects_chain_t* g_effects_chain = NULL;

/* Prototypes */
static void* _sox_player(gpointer data);
static void _sox_log_handler(unsigned level, const char* filename, const char* fmt, va_list ap);
static int _sox_input_drain(sox_effect_t*, sox_sample_t*, size_t*);
static int _sox_output_flow(sox_effect_t*, const sox_sample_t*, sox_sample_t*, size_t*, size_t*);
static sox_effect_handler_t g_sox_input = { "spop_input", NULL, SOX_EFF_MCHAN, NULL, NULL, NULL,
                                            _sox_input_drain, NULL, NULL, 0 };
static sox_effect_handler_t g_sox_output = { "spop_output", NULL, SOX_EFF_MCHAN, NULL, NULL, _sox_output_flow,
                                             NULL, NULL, NULL, 0 };

/* "Private" function used to set up SoX */
static void _sox_init() {
    size_t i;
    if (!g_sox_init) {
        if (sox_init() != SOX_SUCCESS)
            g_error("Can't initialize SoX");

        sox_globals.output_message_handler = _sox_log_handler;

        g_free_bufs = g_queue_new();
        g_full_bufs = g_queue_new();
        sox_buf* bufs = g_new(sox_buf, BUFNB);
        for (i=0; i < BUFNB; i++) {
            bufs[i].buf = g_malloc(BUFSIZE);
            g_queue_push_tail(g_free_bufs, &bufs[i]);
        }

        g_sox_out_type = config_get_string_opt_group("sox", "output_type", NULL);
        g_sox_out_name = config_get_string_group("sox", "output_name");
        g_sox_effects = config_get_string_list_group("sox", "effects", &g_sox_effects_size);

        g_sox_init = TRUE;
    }
}

/* "Private" SoX log handler */
static void _sox_log_handler(unsigned level, const char* filename, const char* fmt, va_list ap) {
    /* SoX levels: 1 = FAIL, 2 = WARN, 3 = INFO, 4 = DEBUG, 5 = DEBUG_MORE, 6 = DEBUG_MOST. */
    gchar* msg = g_strdup_vprintf(fmt, ap);
    switch (level) {
    case 1:
        g_warning("libsox: %s: %s", filename, msg); break;
    case 2:
        g_info("libsox: %s: %s", filename, msg); break;
    case 3:
        g_debug("libsox: %s: %s", filename, msg); break;
    }
    g_free(msg);
}


/* "Private" heler function to add parse and add a SoX effect to the effects chain */
static void _sox_parse_effect(gint argc, gchar** argv) {
    sox_effect_t* effp;
    const sox_effect_handler_t* effhp;

    if (argc < 1)
        g_error("Can't parse empty SoX effect");

    g_debug("SoX: adding effect: %s", argv[0]);

    if (strcmp(argv[0], "spop_input") == 0)
        effhp = &g_sox_input;
    else if (strcmp(argv[0], "spop_output") == 0)
        effhp = &g_sox_output;
    else
        effhp = sox_find_effect(argv[0]);

    effp = sox_create_effect(effhp);
    if (!effp)
        g_error("SoX: unknown effect: %s", argv[0]);

    if (sox_effect_options(effp, argc-1, &argv[1]) != SOX_SUCCESS)
        g_error("SoX: can't parse options for effect %s", argv[0]);

    if (sox_add_effect(g_effects_chain, effp, &g_sox_out->signal, &g_sox_out->signal) != SOX_SUCCESS)
        g_error("SoX: could not add effect %s to effects chain", argv[0]);
    g_free(effp);
}

/* "Private" function used when starting playback with SoX */
static void _sox_start(const sp_audioformat* format) {
    GError* err = NULL;
    sox_signalinfo_t si;
    sox_encodinginfo_t ei;

    g_debug("SoX: starting playback...");

    /* Set up sample format */
    if (format->sample_type != SP_SAMPLETYPE_INT16_NATIVE_ENDIAN)
        g_error("Unsupported sample type");

    si.rate = format->sample_rate;
    si.channels = format->channels;
    si.precision = 16;
    si.length = SOX_IGNORE_LENGTH;
    si.mult = NULL;

    sox_init_encodinginfo(&ei);

    g_sox_frame_size = sizeof(int16_t) * format->channels;

    /* Open SoX output */
    g_debug("Opening SoX output (type: %s, name: %s)...", g_sox_out_type, g_sox_out_name);
    g_sox_out = sox_open_write(g_sox_out_name, &si, NULL, g_sox_out_type, NULL, NULL);
    if (!g_sox_out)
        g_error("Can't open SoX output");

    /* Effects */
    if (g_effects_chain) {
        sox_delete_effects_chain(g_effects_chain);
        g_effects_chain = NULL;
    }
    g_effects_chain = sox_create_effects_chain(&ei, &g_sox_out->encoding);
    if (!g_effects_chain)
        g_error("Can't create SoX effects chain");

    /* Add input effect */
    gchar* args[2];
    args[0] = "spop_input";
    _sox_parse_effect(1, args);

    /* Add user effects */
    gsize i;
    for (i=0; i < g_sox_effects_size; i++) {
        gint argc;
        gchar** argv;
        GError* err = NULL;

        if (!g_shell_parse_argv(g_strstrip(g_sox_effects[i]), &argc, &argv, &err))
            g_error("Can't parse SoX effect \"%s\": %s", g_sox_effects[i], err->message);
        _sox_parse_effect(argc, argv);
        g_strfreev(argv);
    }

    /* Add our output control effect */
    args[0] = "spop_output";
    _sox_parse_effect(1, args);

    /* Add output effect */
    args[0] = "output";
    args[1] = (gchar*) g_sox_out;
    _sox_parse_effect(2, args);

    /* Start the player thread */
    g_player_stop = FALSE;
    g_player_thread = g_thread_try_new("sox_player", _sox_player, NULL, &err);
    if (!g_player_thread)
        g_error("Error while creating SoX player thread: %s", err->message);
}

static void _sox_stop() {
    g_debug("SoX: requesting player thread to stop.");

    /* Flush the queue */
    g_mutex_lock(&g_buf_mutex);
    g_player_stop = TRUE;
    while (g_queue_get_length(g_full_bufs) > 0) {
        sox_buf* buf = g_queue_pop_tail(g_full_bufs);
        g_queue_push_tail(g_free_bufs, buf);
    }
    g_cond_signal(&g_buf_cond);
    g_mutex_unlock(&g_buf_mutex);

    /* Wait until the thread has actually stopped */
    if (g_player_thread) {
        g_thread_join(g_player_thread);
        g_player_thread = NULL;
    }

    /* It's now safe to do some cleanup */
    if (g_effects_chain) {
        sox_delete_effects_chain(g_effects_chain);
        g_effects_chain = NULL;
    }
    if (g_sox_out) {
        sox_close(g_sox_out);
        g_sox_out = NULL;
    }
}

/* Audio player thread */
static void* _sox_player(gpointer data) {
    g_debug("SoX: player thread started.");
    sox_flow_effects(g_effects_chain, NULL, NULL);

    g_debug("SoX: player thread stopped.");

    return NULL;
}

/* Input callback */
static int _sox_input_drain(sox_effect_t* effp, sox_sample_t* obuf, size_t* osamp) {
    /* Is a buffer available? */
    g_mutex_lock(&g_buf_mutex);
    while ((g_queue_get_length(g_full_bufs) == 0) && !g_player_stop)
        g_cond_wait(&g_buf_cond, &g_buf_mutex);

    /* Should we stop now? */
    if (g_player_stop) {
        g_mutex_unlock(&g_buf_mutex);
        g_debug("SoX: stopping playback.");
        *osamp = 0;
        return SOX_EOF;
    }

    sox_buf* buf = g_queue_pop_head(g_full_bufs);
    g_mutex_unlock(&g_buf_mutex);

    /* Decode that buffer */
    size_t max_samples = *osamp;
    size_t avail_samples = buf->size / (g_sox_frame_size / sizeof(int16_t));
    if (avail_samples > max_samples)
        g_error("SoX: avail_samples (%zu) > max_samples (%zu)", avail_samples, max_samples);

    int16_t* frm = (int16_t*) buf->buf;
    size_t i;
    for (i=0; i < avail_samples; i++)
        obuf[i] = SOX_SIGNED_16BIT_TO_SAMPLE(frm[i],);
    *osamp = avail_samples;

    /* Make the buffer available */
    g_mutex_lock(&g_buf_mutex);
    g_queue_push_tail(g_free_bufs, buf);
    g_mutex_unlock(&g_buf_mutex);

    return SOX_SUCCESS;
}

/* Output callback */
static int _sox_output_flow(sox_effect_t* effp, const sox_sample_t* ibuf, sox_sample_t* obuf,
                            size_t* isamp, size_t* osamp) {
    /* Should we stop now? */
    /* TODO: is the mutex really needed? */
    g_mutex_lock(&g_buf_mutex);
    gboolean stop = g_player_stop;
    g_mutex_unlock(&g_buf_mutex);
    if (stop) {
        *osamp = 0;
        return SOX_EOF;
    }

    /* Minimal safety check... */
    if (*osamp < *isamp)
        g_error("SoX: osamp (%zu) < isamp (%zu)", *osamp, *isamp);

    memcpy(obuf, ibuf, *isamp * sizeof(sox_sample_t));
    *osamp = *isamp;

    return SOX_SUCCESS;
}

/* "Public" function, called from a libspotify callback */
G_MODULE_EXPORT int audio_delivery(const sp_audioformat* format, const void* frames, int num_frames) {
    static sp_audioformat old_fmt = { .sample_type = SP_SAMPLETYPE_INT16_NATIVE_ENDIAN,
                                      .sample_rate = 0,
                                      .channels = 0 };

    /* (Maybe) init SoX */
    _sox_init();

    /* What are we supposed to do here? */
    if (num_frames == 0) {
        /* Pause */
        _sox_stop();
        return 0;
    }
    else {
        /* Was there a format change? */
        if ((format->sample_type != old_fmt.sample_type) ||
            (format->sample_rate != old_fmt.sample_rate) ||
            (format->channels != old_fmt.channels)) {
            g_debug("SoX: format change detected");
            _sox_stop();

            old_fmt.sample_type = format->sample_type;
            old_fmt.sample_rate = format->sample_rate;
            old_fmt.channels = format->channels;
        }

        /* Is there a free buffer? */
        g_mutex_lock(&g_buf_mutex);
        if (g_queue_get_length(g_free_bufs) == 0) {
            g_mutex_unlock(&g_buf_mutex);
            return 0;
        }

        /* Has playback started? */
        if (!g_player_thread) {
            _sox_start(format);
        }

        /* Copy frames to a free buffer */
        sox_buf* buf = g_queue_pop_head(g_free_bufs);
        size_t max_frames = BUFSIZE / g_sox_frame_size;
        size_t copied_frames = num_frames < max_frames ? num_frames : max_frames;
        size_t copied_size = copied_frames * g_sox_frame_size;
        memcpy(buf->buf, frames, copied_size);
        buf->size = copied_size;

        /* Make the buffer available to the player */
        g_queue_push_tail(g_full_bufs, buf);
        g_cond_signal(&g_buf_cond);
        g_mutex_unlock(&g_buf_mutex);

        return copied_frames;
    }
}
