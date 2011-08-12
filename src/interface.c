/*
 * Copyright (C) 2010, 2011 Thomas Jost
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
#include <json-glib/json-glib.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "spop.h"
#include "commands.h"
#include "config.h"
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

typedef enum { FUNC=0, BYE, QUIT, IDLE } command_type;
typedef struct {
    gchar*       name;
    int          nb_args;
    command_type type;
    void*        func;
} command_descriptor;
static command_descriptor g_commands[] = {
    { "ls",      0, FUNC, list_playlists },
    { "ls",      1, FUNC, list_tracks },

    { "status",  0, FUNC, status },
    { "repeat",  0, FUNC, repeat },
    { "shuffle", 0, FUNC, shuffle },

    { "qls",     0, FUNC, list_queue },
    { "qclear",  0, FUNC, clear_queue },
    { "qrm",     1, FUNC, remove_queue_item },
    { "qrm",     2, FUNC, remove_queue_items },

    { "play",    1, FUNC, play_playlist },
    { "play",    2, FUNC, play_track },

    { "add",     1, FUNC, add_playlist },
    { "add",     2, FUNC, add_track },

    { "play",    0, FUNC, play },
    { "toggle",  0, FUNC, toggle },
    { "stop",    0, FUNC, stop },
    { "seek",    1, FUNC, seek },

    { "next",    0, FUNC, goto_next },
    { "prev",    0, FUNC, goto_prev },
    { "goto",    1, FUNC, goto_nb },

    { "image",   0, FUNC, image },

    { "bye",     0, BYE,  NULL },
    { "quit",    0, QUIT, NULL },
    { "idle",    0, IDLE, NULL },

    {  NULL, 0, 0, NULL }
};

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
    
    /* Parse and run the command, send its result to the IO channel */
    result = g_string_sized_new(1024);
    g_string_assign(result, "");
    keep_alive = interface_handle_command(buffer->str, result, &must_idle);

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
gboolean interface_handle_command(gchar* command, GString* result, gboolean* must_idle) {
    GError* err = NULL;
    gint argc;
    gchar** argv;
    gchar *endptr;
    gchar* cmd;

    /* Parse the command in a shell-like fashion */
    if (!g_shell_parse_argv(g_strstrip(command), &argc, &argv, &err)) {
        g_debug("Command parser error: %s", err->message);
        g_string_assign(result, "{ \"error\": \"invalid command\" }");
        return TRUE;
    }
    cmd = g_strdup(argv[0]);
    g_debug("Command: [%s] with %d parameter(s)", cmd, argc-1);

    /* Parse arguments as needed */
    int arg1=-1, arg2=-1;
    if (argc >= 2) {
        arg1 = strtol(argv[1], &endptr, 0);
        if ((endptr == argv[1]) || (arg1 < 0)) {
            g_debug("Invalid argument: %s", argv[1]);
            g_string_assign(result, "{ \"error\": \"invalid argument 1\" }");
            g_strfreev(argv);
            return TRUE;
        }
    }
    if (argc >= 3) {
        arg2 = strtol(argv[2], &endptr, 0);
        if ((endptr == argv[2]) || (arg2 < 0)) {
            g_debug("Invalid argument: %s", argv[2]);
            g_string_assign(result, "{ \"error\": \"invalid argument 2\" }");
            g_strfreev(argv);
            return TRUE;
        }
    }
    g_strfreev(argv);

    /* Now execute the command */
    size_t i;
    command_descriptor* cmd_desc = NULL;
    void (*cmd0)(JsonBuilder*);
    void (*cmd1)(JsonBuilder*, int);
    void (*cmd2)(JsonBuilder*, int, int);

    for (i=0; g_commands[i].name != NULL; i++) {
        if ((strcmp(g_commands[i].name, cmd) == 0) && (g_commands[i].nb_args == argc-1)) {
            cmd_desc = &(g_commands[i]);
            break;
        }
    }
    if (!cmd_desc) {
        g_string_assign(result, "{ \"error\": \"unknown command\" }");
        return TRUE;
    }

    /* Handle "normal" and "special" commands separately. */
    switch (cmd_desc->type) {
    case FUNC: {
        JsonBuilder* jb = json_builder_new();
        json_builder_begin_object(jb);

        switch (cmd_desc->nb_args) {
        case 0:
            cmd0 = cmd_desc->func;
            cmd0(jb);
            break;
        case 1:
            cmd1 = cmd_desc->func;
            cmd1(jb, arg1);
            break;
        case 2:
            cmd2 = cmd_desc->func;
            cmd2(jb, arg1, arg2);
            break;
        default:
            g_object_unref(jb);
            g_string_assign(result, "{ \"error\": \"invalid number of arguments\" }");
            return TRUE;
        }

        json_builder_end_object(jb);

        /* Set result using the JSON object */
        JsonGenerator *gen = json_generator_new();
        g_object_set(gen, "pretty", config_get_bool_opt("pretty_json", FALSE), NULL);
        json_generator_set_root(gen, json_builder_get_root(jb));

        gchar *str = json_generator_to_data(gen, NULL);
        g_string_assign(result, str);
        g_string_append(result, "\n");

        g_object_unref(gen);
        g_object_unref(jb);
        g_free(str);

        break;
    }

    case BYE:
        g_string_assign(result, "Bye bye!");
        return FALSE;

    case QUIT:
        g_message("Got a quit command, exiting...");
        exit(0);

    case IDLE:
        *must_idle = TRUE;
        return TRUE;
    }

    return TRUE;
}

/* Notify clients (channels or plugins) that are waiting for an update */
void interface_notify() {
    GString* str = g_string_sized_new(1024);
    JsonBuilder* jb = json_builder_new();

    json_builder_begin_object(jb);
    status(jb);
    json_builder_end_object(jb);

    JsonGenerator *gen = json_generator_new();
    g_object_set(gen, "pretty", config_get_bool_opt("pretty_json", FALSE), NULL);
    json_generator_set_root(gen, json_builder_get_root(jb));

    gchar *tmp = json_generator_to_data(gen, NULL);
    g_string_assign(str, tmp);
    g_string_append(str, "\n");

    g_object_unref(gen);
    g_object_unref(jb);
    g_free(tmp);

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
