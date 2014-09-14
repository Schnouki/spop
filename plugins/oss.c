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

#include <errno.h>
#include <fcntl.h>
#include <glib.h>
#include <gmodule.h>
#include <poll.h>
#include <sys/soundcard.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "audio.h"
#include "config.h"

static int g_oss_fd = -1;
static struct pollfd g_pfd;
static size_t g_oss_frame_size;
static GMutex g_oss_mutex;

/* "Private" functions, used to set up the OSS device */
static void oss_open() {
    const gchar *oss_dev;

    oss_dev = config_get_string_opt_group("oss", "device", "/dev/dsp");

    /* Open the device */
    g_debug("Opening OSS device");
    g_oss_fd = open(oss_dev, O_WRONLY);
    if (g_oss_fd == -1)
        g_error("Can't open OSS device: %s", g_strerror(errno));

    /* Populate the pollfd struct used by poll() */
    g_pfd.fd = g_oss_fd;
    g_pfd.events = POLLOUT;
}

static void oss_close() {
    g_debug("Closing OSS device");
    if (close(g_oss_fd) == -1)
        g_error("Can't close OSS device: %s", g_strerror(errno));
    g_oss_fd = -1;
}

/* Set OSS parameters using "format" from libspotify */
static void oss_setup(const sp_audioformat* format) {
    int tmp;

    /* sample_type is an enum */
    int sample_type;
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
}

/* "Public" function, called from a libspotify callback */
G_MODULE_EXPORT int audio_delivery(const sp_audioformat* format, const void* frames, int num_frames) {
    int ret;

    g_mutex_lock(&g_oss_mutex);

    /* What are we supposed to do here? */
    if (num_frames == 0) {
        /* Pause: close the device */
        if (g_oss_fd != -1)
            oss_close();
        ret = 0;
    }
    else {
        if (g_oss_fd == -1) {
            /* Some frames to play, but the device is closed: open it and set it up */
            oss_open();
            oss_setup(format);
        }

        /* Is the device ready to be written to? */
        ret = poll(&g_pfd, 1, 0);
        if (ret == -1)
            g_error("Can't poll OSS device: %s", g_strerror(errno));
        else if (ret != 0) {
            /* Ok, we can write to the device without blocking */
            ret = write(g_oss_fd, frames, g_oss_frame_size * num_frames);
            if (ret == -1)
                g_error("Can't write to OSS device: %s", g_strerror(errno));

            ret /= g_oss_frame_size;
        }
    }

    g_mutex_unlock(&g_oss_mutex);
    return ret;
}
