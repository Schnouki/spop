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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include "spop.h"
#include "commands.h"
#include "config.h"
#include "interface.h"

const char proto_greetings[] = "spop " SPOP_VERSION "\n";

static GList* g_idle = NULL;

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
    if (sock < 0) {
        perror("Can't create socket");
        exit(1);
    }
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &_true, sizeof(int)) == -1) {
        perror("Can't set socket options");
        exit(1);
    }

    /* Bind the socket */
    bzero(&addr, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = inet_addr(ip_addr);
    if (bind(sock, (struct sockaddr*) &addr, sizeof(struct sockaddr)) == -1) {
        perror("Can't bind socket");
        exit(1);
    }

    /* Start listening */
    if (listen(sock, 5) == -1) {
        perror("Can't listen on socket");
        exit(1);
    }

    /* Create an IO channel and add it to the main loop */
    chan = g_io_channel_unix_new(sock);
    if (!chan) {
        fprintf(stderr, "Can't create IO channel for the main socket.\n");
        exit(1);
    }
    g_io_add_watch(chan, G_IO_IN|G_IO_PRI|G_IO_ERR|G_IO_HUP|G_IO_NVAL, interface_event, NULL);

    if (g_debug)
        fprintf(stderr, "Listening on %s:%d\n", ip_addr, port);
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

    if (g_debug) {
        const char* ev;
        if (condition & G_IO_IN)   ev = "G_IO_IN";
        if (condition & G_IO_OUT)  ev = "G_IO_OUT";
        if (condition & G_IO_PRI)  ev = "G_IO_PRI";
        if (condition & G_IO_ERR)  ev = "G_IO_ERR";
        if (condition & G_IO_HUP)  ev = "G_IO_HUP";
        if (condition & G_IO_NVAL) ev = "G_IO_NVAL";
        fprintf(stderr, "Got interface event: %s\n", ev);
    }

    /* Accept the connection */
    sin_size = sizeof(struct sockaddr_in);
    client = accept(sock, (struct sockaddr*) &client_addr, &sin_size);
    if (client == -1) {
        fprintf(stderr, "Can't accept connection");
        exit(1);
    }

    if (g_debug)
        fprintf(stderr, "[%d] Connection from (%s, %d)\n", client, inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));

    /* Create IO channel for the client, send greetings, and add it to the main loop */
    client_chan = g_io_channel_unix_new(client);
    if (!client_chan) {
        fprintf(stderr, "[%d] Can't create IO channel for the client socket.\n", client);
        exit(1);
    }
    if (g_io_channel_write_chars(client_chan, proto_greetings, -1, NULL, &err) != G_IO_STATUS_NORMAL) {
        fprintf(stderr, "[%d] Can't write to IO channel: %s\n", client, err->message);
        goto ie_client_clean;
    }
    if (g_io_channel_flush(client_chan, &err) != G_IO_STATUS_NORMAL) {
        fprintf(stderr, "[%d] Can't flush IO channel: %s\n", client, err->message);
        goto ie_client_clean;
    }

    g_io_add_watch(client_chan, G_IO_IN|G_IO_PRI|G_IO_ERR|G_IO_HUP|G_IO_NVAL, interface_client_event, NULL);

    return TRUE;

 ie_client_clean:
    g_io_channel_shutdown(client_chan, TRUE, NULL);
    g_io_channel_unref(client_chan);
    close(client);
    if (g_debug)
        fprintf(stderr, "[%d] Connection closed.\n", client);

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

    if (g_debug) {
        const char* ev;
        if (condition & G_IO_IN)   ev = "G_IO_IN";
        if (condition & G_IO_OUT)  ev = "G_IO_OUT";
        if (condition & G_IO_PRI)  ev = "G_IO_PRI";
        if (condition & G_IO_ERR)  ev = "G_IO_ERR";
        if (condition & G_IO_HUP)  ev = "G_IO_HUP";
        if (condition & G_IO_NVAL) ev = "G_IO_NVAL";
        fprintf(stderr, "[%d] Got client event: %s\n", client, ev);
    }

    buffer = g_string_sized_new(1024);
    if (!buffer) {
        fprintf(stderr, "[%d] Can't allocate buffer.\n", client);
        goto ice_client_clean;
    }

    /* Read exactly one command  */
    status = g_io_channel_read_line_string(source, buffer, NULL, &err);
    if (status == G_IO_STATUS_EOF) {
        if (g_debug)
            fprintf(stderr, "[%d] Connection reset by peer.\n", client);
        goto ice_client_clean;
    }
    else if (status != G_IO_STATUS_NORMAL) {
        if (g_debug)
            fprintf(stderr, "[%d] Can't read from IO channel: %s\n", client, err->message);
        goto ice_client_clean;
    }

    if (g_debug)
        fprintf(stderr, "[%d] Received command: %s", client, buffer->str);

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
        g_idle = g_list_prepend(g_idle, source);
        g_string_free(result, TRUE);
    }
    else {
        /* Write the command result to the channel */
        status = g_io_channel_write_chars(source, result->str, -1, NULL, &err);
        g_string_free(result, TRUE);
        if (status != G_IO_STATUS_NORMAL) {
            fprintf(stderr, "[%d] Can't write to IO channel: %s\n", client, err->message);
            goto ice_client_clean;
        }
    }

    if (g_io_channel_flush(source, &err) != G_IO_STATUS_NORMAL) {
        fprintf(stderr, "[%d] Can't flush IO channel: %s\n", client, err->message);
        goto ice_client_clean;
    }

    if (keep_alive)
        return TRUE;

 ice_client_clean:
    if (buffer)
        g_string_free(buffer, TRUE);
    g_idle = g_list_remove(g_idle, source);
    g_io_channel_shutdown(source, TRUE, NULL);
    g_io_channel_unref(source);
    close(client);
    if (g_debug)
        fprintf(stderr, "[%d] Connection closed.\n", client);

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
    if (g_debug) fprintf(stderr, "Command: [%s]", cmd);

    /* Parse the 2 optional numeric arguments */
    if (len >= 2) {
        arg1 = strtol(command[1], &endptr, 0);
        if (endptr == command[1]) {
            arg1 = -1;
            if (g_debug)
                fprintf(stderr, "Invalid argument: %s\n", command[1]);
            g_string_assign(result, "- invalid argument 1\n");
            return TRUE;
        }
        if (g_debug) fprintf(stderr, ", arg1: %d", arg1);
    }
    if (len >= 3) {
        arg2 = strtol(command[2], &endptr, 0);
        if (endptr == command[2]) {
            arg1 = -1;
            if (g_debug)
                fprintf(stderr, "Invalid argument: %s\n", command[2]);
            g_string_assign(result, "- invalid argument 2\n");
            return TRUE;
        }
        if (g_debug) fprintf(stderr, ", arg2: %d", arg2);
    }
    if (g_debug) fprintf(stderr, "\n");


    /* Now parse the command... */
    if (strcmp(cmd, "ls") == 0) {
        if (arg1 == -1)
            list_playlists(result);
        else
            list_tracks(arg1, result);
    }
    else if (strcmp(cmd, "qls") == 0)
        list_queue(result);
    else if (strcmp(cmd, "add") == 0) {
        if (arg1 == -1) {
            g_string_assign(result, "- missing argument\n");
            return TRUE;
        }
        else if (arg2 == -1)
            add_playlist(arg1, result);
        else
            add_track(arg1, arg2, result);
    }
    else if (strcmp(cmd, "play") == 0) {
        if (arg1 == -1)
            play(result);
        else if (arg2 == -1)
            play_playlist(arg1, result);
        else
            play_track(arg1, arg2, result);
    }
    else if (strcmp(cmd, "seek") == 0) {
        if (arg1 == -1) {
            g_string_assign(result, "- missing argument\n");
            return TRUE;
        }
        else
            seek(arg1, result);
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
    else if (strcmp(cmd, "quit") == 0)
        exit(0);
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

/* Notify clients that issued the "idle" command */
void interface_notify_idle() {
    GString* str = g_string_sized_new(1024);
    status(str);
    g_string_append(str, "+ OK\n");

    g_list_foreach(g_idle, interface_notify_idle_chan, str);
    g_list_free(g_idle);
    g_idle = NULL;

    g_string_free(str, TRUE);
}

void interface_notify_idle_chan(gpointer data, gpointer user_data) {
    GIOChannel* chan = data;
    GString* str = user_data;
    GIOStatus status;
    GError* err = NULL;
    int client = g_io_channel_unix_get_fd(chan);

    status = g_io_channel_write_chars(chan, str->str, -1, NULL, &err);
    if (status != G_IO_STATUS_NORMAL) {
        fprintf(stderr, "[%d] Can't write to IO channel: %s\n", client, err->message);
        goto inic_client_clean;
    }
    if (g_io_channel_flush(chan, &err) != G_IO_STATUS_NORMAL) {
        fprintf(stderr, "[%d] Can't flush IO channel: %s\n", client, err->message);
        goto inic_client_clean;
    }
    return;

 inic_client_clean:
    g_io_channel_shutdown(chan, TRUE, NULL);
    g_io_channel_unref(chan);
    close(client);
    if (g_debug)
        fprintf(stderr, "[%d] Connection closed.\n", client);
}
