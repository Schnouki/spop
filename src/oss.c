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

#include <errno.h>
#include <fcntl.h>
#include <glib.h>
#include <sys/soundcard.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <unistd.h>

#include "audio.h"

#define BUFSIZE  8192
#define BUFNB    8

typedef struct {
    size_t size;
    char* buf;
} loss_buf;

static int g_oss_fd = -1;
static GIOChannel* g_chan = NULL;
static guint g_oss_ev_id = 0;
static size_t g_oss_frame_size;

static GQueue* g_free_bufs = NULL;
static GQueue* g_full_bufs = NULL;
static GStaticMutex g_oss_mutex = G_STATIC_MUTEX_INIT;

/* "Private" functions, used to set up the OSS device */
static void oss_open(const sp_audioformat* format) {
    int sample_type, i, tmp;
    loss_buf* bufs = NULL;
    GError* err = NULL;

    g_debug("Opening OSS device");
    g_static_mutex_lock(&g_oss_mutex);

    /* Open the device */
    g_oss_fd = open("/dev/dsp", O_WRONLY);
    if (g_oss_fd == -1)
        g_error("Can't open OSS device: %s", g_strerror(errno));

    /* sample_type is an enum */
    switch (format->sample_type) {
    case SP_SAMPLETYPE_INT16_NATIVE_ENDIAN:
        sample_type = AFMT_S16_NE;
        g_oss_frame_size = sizeof(int16_t) * format->channels;
        break;
    default:
        g_error("Unknown sample type");
    }

    /* Now really setup the device. The order of the initialization is the one
       suggested in the OSS doc for some old devices, just in case...
       (http://manuals.opensound.com/developer/callorder.html) */

    tmp = format->channels;
    if (ioctl(g_oss_fd, SNDCTL_DSP_CHANNELS, &tmp) == -1)
        g_error("Error setting OSS channels: %s", g_strerror(errno));
    if (tmp != format->channels)
        g_error( "Could not set OSS channels to %d (set to %d instead)", format->channels, tmp);

    tmp = sample_type;
    if (ioctl(g_oss_fd, SNDCTL_DSP_SETFMT, &tmp) == -1)
        g_error("Error setting OSS sample type: %s", g_strerror(errno));
    if (tmp != sample_type)
        g_error("Could not set OSS sample type to %d (set to %d instead)", sample_type, tmp);

    tmp = format->sample_rate;
    if (ioctl(g_oss_fd, SNDCTL_DSP_SPEED, &tmp) == -1)
        g_error("Error setting OSS sample rate: %s", g_strerror(errno));
    /* Sample rate: the OSS doc that differences up to 10% should be accepted */
    if (((100*abs(format->sample_rate - tmp))/format->sample_rate) > 10)
        g_error("Could not set OSS sample rate to %d (set to %d instead)", format->sample_rate, tmp);

    /* Create buffers */
    if (!g_free_bufs) {
        g_debug("Creating OSS buffers");

        g_free_bufs = g_queue_new();
        if (!g_free_bufs)
            g_error("Can't allocate a queue of free buffers");
        g_full_bufs = g_queue_new();
        if (!g_full_bufs)
            g_error("Can't allocate a queue of full buffers");

        bufs = (loss_buf*) malloc(BUFNB*sizeof(loss_buf));
        if (!bufs)
            g_error("Can't allocate buffer structures");
        for (i=0; i < BUFNB; i++) {
            bufs[i].buf = malloc(BUFSIZE);
            if (!(bufs[i].buf))
                g_error("Can't allocate buffer");
            g_queue_push_tail(g_free_bufs, &(bufs[i]));
        }
    }

    /* Create a matching IO channel */
    g_chan = g_io_channel_unix_new(g_oss_fd);
    if (!g_chan)
        g_error("Could not create an IO channel for the OSS device");
    if (g_io_channel_set_encoding(g_chan, NULL, &err) != G_IO_STATUS_NORMAL)
        g_error("Could not set the encoding for the OSS IO channel: %s", err->message);

    g_static_mutex_unlock(&g_oss_mutex);
}

static void oss_close() {
    loss_buf* buf;
    GError* err = NULL;

    g_debug("Closing OSS device");
    g_static_mutex_lock(&g_oss_mutex);

    /* Flush the queue */
    while (g_queue_get_length(g_full_bufs) > 0) {
        buf = g_queue_pop_tail(g_full_bufs);
        g_queue_push_tail(g_free_bufs, buf);
    }

    /* Remove the event source */
    if ((g_oss_ev_id > 0) && !g_source_remove(g_oss_ev_id))
        g_error("Can't remove OSS event source");

    /* Shutdown the IO channel */
    if (g_io_channel_shutdown(g_chan, FALSE, &err) != G_IO_STATUS_NORMAL)
        g_error("Can't shutdown OSS IO channel: %s", err->message);
    g_chan = NULL;

    /* Close the OSS device */
    if (close(g_oss_fd) == -1)
        g_error("Can't close OSS device: %s", g_strerror(errno));
    g_oss_fd = -1;

    g_static_mutex_unlock(&g_oss_mutex);
}

/* OSS event -- write data from a buffer to the OSS device */
static gboolean oss_event(GIOChannel* source, GIOCondition condition, gpointer data) {
    GError* err = NULL;
    loss_buf* buf = NULL;

    g_static_mutex_lock(&g_oss_mutex);
    buf = g_queue_pop_head(g_full_bufs);

    if (buf) {
        /* There is something to play */
        if (g_io_channel_write_chars(source, buf->buf, buf->size, NULL, &err) != G_IO_STATUS_NORMAL)
            g_error("Could not write to OSS device: %s", err->message);
        
        g_queue_push_tail(g_free_bufs, buf);
        g_static_mutex_unlock(&g_oss_mutex);

        return TRUE;
    }
    else {
        /* Nothing to play: delete the event source */
        g_oss_ev_id = 0;
        g_static_mutex_unlock(&g_oss_mutex);
        return FALSE;
    }
}

/* "Public" function, called from a libspotify callback */
int audio_delivery(const sp_audioformat* format, const void* frames, int num_frames) {
    loss_buf* buf;
    size_t size;

    /* What are we supposed to do here? */
    if (num_frames == 0) {
        /* Pause: close the device */
        oss_close();
        return 0;
    }
    else {
        if (g_oss_fd == -1)
            /* Some frames to play, but the device is closed: open it and set it up */
            oss_open(format);

        /* Try to add data to the queue */
        g_static_mutex_lock(&g_oss_mutex);
        if (g_queue_get_length(g_free_bufs) == 0) {
            /* Can't add anything */
            g_static_mutex_unlock(&g_oss_mutex);
            return 0;
        }

        /* Copy data to a free buffer and make it available to the player */
        buf = g_queue_pop_head(g_free_bufs);
        size = BUFSIZE < (num_frames * g_oss_frame_size) ? BUFSIZE : (num_frames * g_oss_frame_size);
        memcpy(buf->buf, frames, size);
        buf->size = size;
        g_queue_push_tail(g_full_bufs, buf);

        /* Add the event source if it's not here already */
        if (g_oss_ev_id == 0)
            g_oss_ev_id = g_io_add_watch_full(g_chan, G_PRIORITY_HIGH, G_IO_OUT|G_IO_ERR|G_IO_NVAL, oss_event, NULL, NULL);

        g_static_mutex_unlock(&g_oss_mutex);
        return size / g_oss_frame_size;
    }
}
