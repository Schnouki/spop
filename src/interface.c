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

static int g_sockfd;
static GThread* g_if_t;

const char proto_greetings[] = "spop " SPOP_VERSION "\n";

/* Functions called directly from spop */
void interface_init() {
    const char* ip_addr;
    int port;
    int true = 1;
    struct sockaddr_in addr;
    GError* err;

    ip_addr = config_get_string_opt("listen_address", "127.0.0.1");
    port = config_get_int_opt("listen_port", 6602);

    /* Create the socket */
    g_sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (g_sockfd < 0) {
        perror("Can't create socket");
        exit(1);
    }
    if (setsockopt(g_sockfd, SOL_SOCKET, SO_REUSEADDR, &true, sizeof(int)) == -1) {
        perror("Can't set socket options");
        exit(1);
    }

    /* Bind the socket */
    bzero(&addr, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = inet_addr(ip_addr);
    if (bind(g_sockfd, (struct sockaddr*) &addr, sizeof(struct sockaddr)) == -1) {
        perror("Can't bind socket");
        exit(1);
    }

    /* Start listening */
    if (listen(g_sockfd, 5) == -1) {
        perror("Can't listen on socket");
        exit(1);
    }

    /* Create the interface thread */
    g_if_t = g_thread_create(interface_thread, NULL, FALSE, &err);
    if (!g_if_t) {
        fprintf(stderr, "Can't create interface thread: %s\n", err->message);
        exit(1);
    }
    if (g_debug)
        fprintf(stderr, "Listening on %s:%d\n", ip_addr, port);

}

/* Interface thread -- accept connections, read commands, execute them */
void* interface_thread(void* data) {
    struct sockaddr_in client_addr;
    socklen_t sin_size;
    int client;
    int* client_ptr = NULL;
    GError* err = NULL;

    while (1) {
        sin_size = sizeof(struct sockaddr_in);
        client = accept(g_sockfd, (struct sockaddr*) &client_addr, &sin_size);
        if (client == -1) {
            fprintf(stderr, "Can't accept connection");
            exit(1);
        }

        if (g_debug)
            fprintf(stderr, "[%d] Connection from (%s, %d)\n", client, inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));

        /* Handle the client in a new thread */
        client_ptr = (int*) malloc(sizeof(int));
        if (!client_ptr) {
            fprintf(stderr, "Can't allocate a single int.\n");
            exit(1);
        }
        *client_ptr = client;
        g_thread_create(interface_handle_client, (gpointer) client_ptr, FALSE, &err);
        if (err) {
            fprintf(stderr, "Can't create client thread: %s\n", err->message);
            exit(1);
        }
        client_ptr = NULL;
    }

    return NULL;
}

/* Handle communications with the client. This function is run in its own thread. */
void* interface_handle_client(void* data) {
    GIOChannel* channel = NULL;
    GString* buffer = NULL;
    GError* err = NULL;
    GIOStatus status;
    gchar** command;
    GString* result;
    int client;

    client = *((int*) data);
    free(data);
    channel = g_io_channel_unix_new(client);
   
    /* Send greetings to the client */
    if (g_io_channel_write_chars(channel, proto_greetings, -1, NULL, &err) != G_IO_STATUS_NORMAL) {
        fprintf(stderr, "[%d] Can't write to IO channel: %s\n", client, err->message);
        goto client_clean;
    }
    if (g_io_channel_flush(channel, &err) != G_IO_STATUS_NORMAL) {
        fprintf(stderr, "[%d] Can't flush IO channel: %s\n", client, err->message);
        goto client_clean;
    }

    buffer = g_string_sized_new(1024);
    if (!buffer) {
        fprintf(stderr, "[%d] Can't allocate buffer.\n", client);
        goto client_clean;
    }

    /* Read commands */
    while (1) {
        status = g_io_channel_read_line_string(channel, buffer, NULL, &err);
        if (status == G_IO_STATUS_EOF) {
            if (g_debug) {
                fprintf(stderr, "[%d] Connection reset by peer.\n", client);
                goto client_clean;
            }
        }
        else if (status != G_IO_STATUS_NORMAL) {
            fprintf(stderr, "[%d] Can't read from IO channel: %s\n", client, err->message);
            goto client_clean;
        }

        if (g_debug)
            fprintf(stderr, "[%d] Received command: %s", client, buffer->str);

        /* Extract commands */
        command = g_strsplit(g_strstrip(buffer->str), " ", 0);

        /* Parse and run the command, send its result to the IO channel */
        result = g_string_sized_new(1024);
        g_string_assign(result, "");
        interface_handle_command(command, result);
        status = g_io_channel_write_chars(channel, result->str, -1, NULL, &err);

        /* Free allocated memory and deal with errors */
        g_string_free(result, TRUE);
        g_strfreev(command);
        if (status != G_IO_STATUS_NORMAL) {
            fprintf(stderr, "[%d] Can't write to IO channel: %s\n", client, err->message);
            goto client_clean;
        }
        if (g_io_channel_flush(channel, &err) != G_IO_STATUS_NORMAL) {
            fprintf(stderr, "[%d] Can't flush IO channel: %s\n", client, err->message);
            goto client_clean;
        }
    }

 client_clean:
    if (buffer)
        g_string_free(buffer, TRUE);
    err = NULL;
    if (g_io_channel_shutdown(channel, TRUE, &err) != G_IO_STATUS_NORMAL) {
        fprintf(stderr, "[%d] Can't shutdown IO channel: %s\n", client, err->message);
        return NULL;
    }
    g_io_channel_unref(channel);
    close(client);
    if (g_debug)
        fprintf(stderr, "[%d] Connection closed.\n", client);

    return NULL;
}

/* Parse the command and execute it */
void interface_handle_command(gchar** command, GString* result) {
    int len;
    gchar* cmd;
    gchar* endptr;
    int arg1 = -1;
    int arg2 = -1;

    /* Number of items in the command array */
    len = g_strv_length(command);
    if (len == 0)
        return;

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
            return;
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
            return;
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
            g_string_assign(result, "- missing argument");
            return;
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
            return;
        }
        else
            goto_nb(result, arg1);
    }
    else if (strcmp(cmd, "status") == 0)
        status(result);
    else if (strcmp(cmd, "idle") == 0)
        idle(result);
    else if (strcmp(cmd, "quit") == 0) {
        exit(0);
    }
    else {
        g_string_assign(result, "- unknown command\n");
        return;
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
}
