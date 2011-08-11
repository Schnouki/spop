/*
 * Copyright (C) 2010 Thomas Jost
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

#include <arpa/inet.h>
#include <errno.h>
#include <glib.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "spop.h"
#include "commands.h"
#include "config.h"
#include "interface.h"

const char proto_greetings[] = "spop " SPOP_VERSION "\n";

/* Channels and plugins that have to be notified when something changes
   ("idle" command) */
static GList* g_idle_channels = NULL;
static GList* g_notification_callbacks = NULL;
typedef struct {
    spop_notify_callback_ptr func;
    gpointer data;
} notification_callback;

/* Functions called directly from spop */
void interface_init() {
    const char* ip_addr;
    int port;
    int _true = 1;
    struct sockaddr_in addr;
    int sock;
    GIOChannel* chan;

    ip_addr = config_get_string_opt("listen_address", "127.0.0.1");
    port = config_get_int_opt("listen_port", 6602);

    /* Create the socket */
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0)
        g_error("Can't create socket: %s", g_strerror(errno));
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &_true, sizeof(int)) == -1)
        g_error("Can't set socket options: %s", g_strerror(errno));

    /* Bind the socket */
    bzero(&addr, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = inet_addr(ip_addr);
    if (bind(sock, (struct sockaddr*) &addr, sizeof(struct sockaddr)) == -1)
        g_error("Can't bind socket: %s", g_strerror(errno));

    /* Start listening */
    if (listen(sock, 5) == -1)
        g_error("Can't listen on socket: %s", g_strerror(errno));

    /* Create an IO channel and add it to the main loop */
    chan = g_io_channel_unix_new(sock);
    if (!chan)
        g_error("Can't create IO channel for the main socket.");
    g_io_add_watch(chan, G_IO_IN|G_IO_PRI|G_IO_ERR|G_IO_HUP|G_IO_NVAL, interface_event, NULL);

    g_info("Listening on %s:%d", ip_addr, port);
}

/* Interface event -- accept connections, create IO channels for clients */
gboolean interface_event(GIOChannel* source, GIOCondition condition, gpointer data) {
    int sock;
    struct sockaddr_in client_addr;
    socklen_t sin_size;
    int client;
    GIOChannel* client_chan;
    GError* err = NULL;

    sock = g_io_channel_unix_get_fd(source);

    /* Accept the connection */
    sin_size = sizeof(struct sockaddr_in);
    client = accept(sock, (struct sockaddr*) &client_addr, &sin_size);
    if (client == -1)
        g_error("Can't accept connection");

    g_info("[%d] Connection from (%s, %d)", client, inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));

    /* Create IO channel for the client, send greetings, and add it to the main loop */
    client_chan = g_io_channel_unix_new(client);
    if (!client_chan)
        g_error("[%d] Can't create IO channel for the client socket.", client);

    if (g_io_channel_write_chars(client_chan, proto_greetings, -1, NULL, &err) != G_IO_STATUS_NORMAL) {
        g_debug("[%d] Can't write to IO channel: %s", client, err->message);
        goto ie_client_clean;
    }
    if (g_io_channel_flush(client_chan, &err) != G_IO_STATUS_NORMAL) {
        g_debug("[%d] Can't flush IO channel: %s", client, err->message);
        goto ie_client_clean;
    }

    g_io_add_watch(client_chan, G_IO_IN|G_IO_PRI|G_IO_ERR|G_IO_HUP|G_IO_NVAL, interface_client_event, NULL);

    return TRUE;

 ie_client_clean:
    g_io_channel_shutdown(client_chan, TRUE, NULL);
    g_io_channel_unref(client_chan);
    close(client);
    g_debug("[%d] Connection closed.", client);

    return TRUE;
}

/* Handle communications with the client. */
gboolean interface_client_event(GIOChannel* source, GIOCondition condition, gpointer data) {
    GString* buffer = NULL;
    GError* err = NULL;
    GIOStatus status;
    gchar** command;
    GString* result;
    int client;
    gboolean keep_alive = TRUE;
    gboolean must_idle = FALSE;

    client = g_io_channel_unix_get_fd(source);

    buffer = g_string_sized_new(1024);
    if (!buffer) {
        g_warning("[%d] Can't allocate buffer.", client);
        goto ice_client_clean;
    }

    /* Read exactly one command  */
    status = g_io_channel_read_line_string(source, buffer, NULL, &err);
    if (status == G_IO_STATUS_EOF) {
        g_debug("[%d] Connection reset by peer.", client);
        goto ice_client_clean;
    }
    else if (status != G_IO_STATUS_NORMAL) {
        g_debug("[%d] Can't read from IO channel: %s", client, err->message);
        goto ice_client_clean;
    }

    buffer->str[buffer->len-1] = '\0';
    g_debug("[%d] Received command: %s", client, buffer->str);
    buffer->str[buffer->len-1] = '\n';

    /* Extract commands */
    command = g_strsplit(g_strstrip(buffer->str), " ", 0);
    
    /* Parse and run the command, send its result to the IO channel */
    result = g_string_sized_new(1024);
    g_string_assign(result, "");
    keep_alive = interface_handle_command(command, result, &must_idle);
    g_strfreev(command);

    /* "idle" command? */
    if (must_idle) {
        /* Add to list of idle channels */
        g_idle_channels = g_list_prepend(g_idle_channels, source);
        g_string_free(result, TRUE);
    }
    else {
        /* Write the command result to the channel */
        status = g_io_channel_write_chars(source, result->str, -1, NULL, &err);
        g_string_free(result, TRUE);
        if (status != G_IO_STATUS_NORMAL) {
            g_debug("[%d] Can't write to IO channel: %s", client, err->message);
            goto ice_client_clean;
        }
    }

    if (g_io_channel_flush(source, &err) != G_IO_STATUS_NORMAL) {
        g_debug("[%d] Can't flush IO channel: %s", client, err->message);
        goto ice_client_clean;
    }

    if (keep_alive)
        return TRUE;

 ice_client_clean:
    if (buffer)
        g_string_free(buffer, TRUE);
    g_idle_channels = g_list_remove(g_idle_channels, source);
    g_io_channel_shutdown(source, TRUE, NULL);
    g_io_channel_unref(source);
    close(client);
    g_info("[%d] Connection closed.", client);

    return FALSE;
}

/* Parse the command and execute it */
gboolean interface_handle_command(gchar** command, GString* result, gboolean* must_idle) {
    int len;
    gchar* cmd;
    gchar* endptr;
    int arg1 = -1;
    int arg2 = -1;

    /* Number of items in the command array */
    len = g_strv_length(command);
    if (len == 0)
        return TRUE;

    cmd = command[0];
    g_debug("Command: [%s]", cmd);

    /* Parse the 2 optional numeric arguments */
    if (len >= 2) {
        arg1 = strtol(command[1], &endptr, 0);
        if (endptr == command[1]) {
            arg1 = -1;
            g_debug("Invalid argument: %s", command[1]);
            g_string_assign(result, "- invalid argument 1\n");
            return TRUE;
        }
        else if (arg1 < 0) {
            g_debug("Invalid value for command argument: %s", command[1]);
            g_string_assign(result, "- invalid value for argument 1\n");
            return TRUE;
        }
    }
    if (len >= 3) {
        arg2 = strtol(command[2], &endptr, 0);
        if (endptr == command[2]) {
            arg2 = -1;
            g_debug("Invalid argument: %s", command[2]);
            g_string_assign(result, "- invalid argument 2\n");
            return TRUE;
        }
        else if (arg2 < 0) {
            g_debug("Invalid value for command argument: %s", command[2]);
            g_string_assign(result, "- invalid value for argument 2\n");
            return TRUE;
        }
    }

    /* Now parse the command... */
    if (strcmp(cmd, "ls") == 0) {
        if (arg1 == -1)
            list_playlists(result);
        else
            list_tracks(result, arg1);
    }
    else if (strcmp(cmd, "add") == 0) {
        if (arg1 == -1) {
            g_string_assign(result, "- missing argument\n");
            return TRUE;
        }
        else if (arg2 == -1)
            add_playlist(result, arg1);
        else
            add_track(result, arg1, arg2);
    }
    else if (strcmp(cmd, "play") == 0) {
        if (arg1 == -1)
            play(result);
        else if (arg2 == -1)
            play_playlist(result, arg1);
        else
            play_track(result, arg1, arg2);
    }
    else if (strcmp(cmd, "seek") == 0) {
        if (arg1 == -1) {
            g_string_assign(result, "- missing argument\n");
            return TRUE;
        }
        else
            seek(result, arg1);
    }
    else if ((strcmp(cmd, "toggle") == 0) || (strcmp(cmd, "pause") == 0))
        toggle(result);
    else if (strcmp(cmd, "stop") == 0)
        stop(result);
    else if (strcmp(cmd, "next") == 0)
        goto_next(result);
    else if (strcmp(cmd, "prev") == 0)
        goto_prev(result);
    else if (strcmp(cmd, "goto") == 0) {
        if (arg1 == -1) {
            g_string_assign(result, "- missing argument\n");
            return TRUE;
        }
        else
            goto_nb(result, arg1);
    }
    else if (strcmp(cmd, "status") == 0)
        status(result);
    else if (strcmp(cmd, "idle") == 0) {
        *must_idle = TRUE;
        return TRUE;
    }
    else if (strcmp(cmd, "repeat") == 0)
        repeat(result);
    else if (strcmp(cmd, "shuffle") == 0)
        shuffle(result);
    else if (strcmp(cmd, "qls") == 0)
        list_queue(result);
    else if (strcmp(cmd, "qclear") == 0)
        clear_queue(result);
    else if (strcmp(cmd, "qrm") == 0) {
        if ((arg1 == -1)) {
            g_string_assign(result, "- missing argument\n");
            return TRUE;
        }
        else {
            if (arg2 == -1)
                remove_queue_items(result, arg1, 1);
            else
                remove_queue_items(result, arg1, arg2);
        }
    }
    else if (strcmp(cmd, "quit") == 0) {
        g_message("Got a quit command, exiting...");
        exit(0);
    }
    else if (strcmp(cmd, "bye") == 0) {
        g_string_assign(result, "+ OK Bye bye!\n");
        return FALSE;
    }
    else {
        g_string_assign(result, "- unknown command\n");
        return TRUE;
    }

    if (result->len == 0)
        g_string_append(result, "- ERR\n");
    else if ((result->str[0] != '-') && (result->str[0] != '+')) {
        /* Is there something to add ?... */
        gchar* needle = "\n";
        gchar* pos = g_strrstr_len(result->str, result->len-1, needle);
        if (!pos || (pos && (pos < &(result->str[result->len])) && (pos[1] != '+') && (pos[1] != '-')))
            g_string_append(result, "+ OK\n");
    }
    return TRUE;
}

/* Notify clients (channels or plugins) that are waiting for an update */
void interface_notify() {
    GString* str = g_string_sized_new(1024);

    status(str);
    g_string_append(str, "+ OK\n");

    /* First notify idle channels */
    g_list_foreach(g_idle_channels, interface_notify_chan, str);
    g_list_free(g_idle_channels);
    g_idle_channels = NULL;

    /* Then call callbacks from plugins */
    g_list_foreach(g_notification_callbacks, interface_notify_callback, str);

    g_string_free(str, TRUE);
}

void interface_notify_chan(gpointer data, gpointer user_data) {
    GIOChannel* chan = data;
    GString* str = user_data;
    GIOStatus status;
    GError* err = NULL;
    int client = g_io_channel_unix_get_fd(chan);

    status = g_io_channel_write_chars(chan, str->str, -1, NULL, &err);
    if (status != G_IO_STATUS_NORMAL) {
        g_debug("[%d] Can't write to IO channel: %s", client, err->message);
        goto inic_client_clean;
    }
    if (g_io_channel_flush(chan, &err) != G_IO_STATUS_NORMAL) {
        g_debug("[%d] Can't flush IO channel: %s", client, err->message);
        goto inic_client_clean;
    }
    return;

 inic_client_clean:
    g_io_channel_shutdown(chan, TRUE, NULL);
    g_io_channel_unref(chan);
    close(client);
    g_debug("[%d] Connection closed.", client);
}

void interface_notify_callback(gpointer data, gpointer user_data) {
    notification_callback* ncb = (notification_callback*) data;
    const GString* status = (const GString*) user_data;

    ncb->func(status, ncb->data);
}

gboolean interface_notify_add_callback(spop_notify_callback_ptr func, gpointer data) {
    notification_callback* ncb;
    GList* cur;

    /* Is there already such a callback/data couple in the list? */
    cur = g_notification_callbacks;
    while (cur != NULL) {
        ncb = cur->data;
        if ((ncb->func == func) && (ncb->data == data))
            return FALSE;
        cur = cur->next;
    }
    
    /* Callback/data not in the list: add them */
    ncb = g_malloc(sizeof(notification_callback));
    ncb->func = func;
    ncb->data = data;
    g_notification_callbacks = g_list_prepend(g_notification_callbacks, ncb);
    return TRUE;
}
