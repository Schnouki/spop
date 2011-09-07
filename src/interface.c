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

typedef enum { CT_FUNC=0, CT_BYE, CT_QUIT, CT_IDLE } command_type;
typedef struct {
    gchar*             name;
    command_type       type;
    command_descriptor desc;
} command_full_descriptor;
static command_full_descriptor g_commands[] = {
    { "ls",      CT_FUNC, { list_playlists, {CA_NONE}}},
    { "ls",      CT_FUNC, { list_tracks,    {CA_INT, CA_NONE}}},

    { "status",  CT_FUNC, { status,  {CA_NONE}}},
    { "repeat",  CT_FUNC, { repeat,  {CA_NONE}}},
    { "shuffle", CT_FUNC, { shuffle, {CA_NONE}}},

    { "qls",     CT_FUNC, { list_queue,         {CA_NONE}}},
    { "qclear",  CT_FUNC, { clear_queue,        {CA_NONE}}},
    { "qrm",     CT_FUNC, { remove_queue_item,  {CA_INT, CA_NONE}}},
    { "qrm",     CT_FUNC, { remove_queue_items, {CA_INT, CA_INT}}},

    { "play",    CT_FUNC, { play_playlist, {CA_INT, CA_NONE}}},
    { "play",    CT_FUNC, { play_track,    {CA_INT, CA_INT}} },

    { "add",     CT_FUNC, { add_playlist, {CA_INT, CA_NONE}}},
    { "add",     CT_FUNC, { add_track,    {CA_INT, CA_INT}} },

    { "play",    CT_FUNC, { play,   {CA_NONE}}},
    { "toggle",  CT_FUNC, { toggle, {CA_NONE}}},
    { "stop",    CT_FUNC, { stop,   {CA_NONE}}},
    { "seek",    CT_FUNC, { seek,   {CA_INT, CA_NONE}}},

    { "next",    CT_FUNC, { goto_next, {CA_NONE}}},
    { "prev",    CT_FUNC, { goto_prev, {CA_NONE}}},
    { "goto",    CT_FUNC, { goto_nb,   {CA_INT, CA_NONE}}},

    { "image",   CT_FUNC, { image, {CA_NONE}}},

    { "uinfo",   CT_FUNC, { uri_info, {CA_URI, CA_NONE}}},

    { "bye",     CT_BYE,  {}},
    { "quit",    CT_QUIT, {}},
    { "idle",    CT_IDLE, {}},

    {  NULL, 0, {}}
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
    g_io_channel_set_close_on_unref(chan, TRUE);
    g_io_add_watch(chan, G_IO_IN|G_IO_HUP, interface_event, NULL);

    g_info("Listening on %s:%d", ip_addr, port);
}

/* Interface event -- accept connections, create IO channels for clients */
gboolean interface_event(GIOChannel* source, GIOCondition condition, gpointer data) {
    int sock;
    struct sockaddr_in client_addr;
    socklen_t sin_size;
    int client;
    GIOChannel* client_chan;

    sock = g_io_channel_unix_get_fd(source);

    /* Accept the connection */
    sin_size = sizeof(struct sockaddr_in);
    client = accept(sock, (struct sockaddr*) &client_addr, &sin_size);
    if (client == -1)
        g_error("Can't accept connection");

    g_info("[ie:%d] Connection from (%s, %d)", client, inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));

    /* Create IO channel for the client, send greetings, and add it to the main loop */
    client_chan = g_io_channel_unix_new(client);
    if (!client_chan)
        g_error("[ie:%d] Can't create IO channel for the client socket.", client);
    g_io_channel_set_close_on_unref(client_chan, TRUE);

    if (!interface_write(client_chan, proto_greetings))
        goto ie_client_clean;

    g_io_add_watch(client_chan, G_IO_IN|G_IO_HUP, interface_client_event, NULL);

    return TRUE;

 ie_client_clean:
    g_io_channel_shutdown(client_chan, TRUE, NULL);
    g_io_channel_unref(client_chan);
    g_debug("[ie:%d] Connection closed.", client);

    return TRUE;
}

/* Handle communications with the client. */
gboolean interface_client_event(GIOChannel* source, GIOCondition condition, gpointer data) {
    GString* buffer = NULL;
    GError* err = NULL;
    GIOStatus status;
    int client;
    command_result cr = CR_OK;

    client = g_io_channel_unix_get_fd(source);

    /* Ready for reading? */
    if (condition & G_IO_IN) {
        buffer = g_string_sized_new(1024);
        if (!buffer) {
            g_warning("[ice:%d] Can't allocate buffer.", client);
            goto ice_client_clean;
        }

        /* Read exactly one command  */
        status = g_io_channel_read_line_string(source, buffer, NULL, &err);
        if (status == G_IO_STATUS_EOF) {
            g_debug("[ice:%d] Connection reset by peer.", client);
            goto ice_client_clean;
        }
        else if (status != G_IO_STATUS_NORMAL) {
            g_debug("[ice:%d] Can't read from IO channel: %s", client, err->message);
            goto ice_client_clean;
        }

        buffer->str[buffer->len-1] = '\0';
        g_debug("[ice:%d] Received command: %s", client, buffer->str);
        buffer->str[buffer->len-1] = '\n';
    
        /* Parse and run the command */
        cr = interface_handle_command(source, buffer->str);
        g_string_free(buffer, TRUE);
        buffer = NULL;

        /* "idle" command? */
        if (cr == CR_IDLE) {
            /* Add to list of idle channels */
            g_idle_channels = g_list_prepend(g_idle_channels, source);
        }
    }

    /* Received hangup? */
    if (condition & G_IO_HUP) {
        g_debug("[ice:%d] Connection hung up", client);
        goto ice_client_clean;
    }

    if (cr != CR_CLOSE)
        return TRUE;

 ice_client_clean:
    if (buffer)
        g_string_free(buffer, TRUE);
    g_idle_channels = g_list_remove(g_idle_channels, source);
    g_io_channel_shutdown(source, TRUE, NULL);
    g_io_channel_unref(source);
    g_info("[ice:%d] Connection closed.", client);

    return FALSE;
}

/* Parse the command and execute it */
command_result interface_handle_command(GIOChannel* chan, gchar* command){
    GError* err = NULL;
    gint argc;
    gchar** argv_;
    gchar** argv;
    size_t i;

    /* Parse the command in a shell-like fashion */
    if (!g_shell_parse_argv(g_strstrip(command), &argc, &argv_, &err)) {
        g_debug("Command parser error: %s", err->message);
        interface_write(chan, "{ \"error\": \"invalid command\" }\n");
        return CR_OK;
    }

    /* Copy argv_ to the stack so it's easier to use... */
    argv = g_newa(gchar*, argc);
    for(i=0; i < argc; i++) {
        argv[i] = g_newa(gchar, strlen(argv_[i]+1));
        strcpy(argv[i], argv_[i]);
    }
    g_strfreev(argv_);

    g_debug("Command: [%s] with %d parameter(s)", argv[0], argc-1);

    /* Now execute the command */
    command_full_descriptor* cmd_desc = NULL;

    for (i=0; g_commands[i].name != NULL; i++) {
        int nb_args = 0;
        while ((nb_args < MAX_CMD_ARGS) && (g_commands[i].desc.args[nb_args] != CA_NONE))
            nb_args += 1;
        if ((strcmp(g_commands[i].name, argv[0]) == 0) && (nb_args == argc-1)) {
            cmd_desc = &(g_commands[i]);
            break;
        }
    }
    if (!cmd_desc) {
        interface_write(chan, "{ \"error\": \"unknown command\" }\n");
        return CR_OK;
    }

    /* Handle "normal" and "special" commands separately. */
    switch (cmd_desc->type) {
    case CT_FUNC: {
        gboolean ret;

        ret = command_run(chan, &(cmd_desc->desc), argc, argv);
        return (ret ? CR_OK : CR_DEFERED);
    }

    case CT_BYE:
        interface_write(chan, "Bye bye!\n");
        return CR_CLOSE;

    case CT_QUIT:
        g_message("Got a quit command, exiting...");
        exit(0);

    case CT_IDLE:
        return CR_IDLE;
    }

    return CR_OK;
}

gboolean interface_write(GIOChannel* chan, const gchar* str) {
    GIOStatus status;
    GError* err = NULL;
    int client = g_io_channel_unix_get_fd(chan);

    if (str) {
        status = g_io_channel_write_chars(chan, str, -1, NULL, &err);
        if (status != G_IO_STATUS_NORMAL) {
            if (err)
                g_debug("[iw:%d] Can't write to IO channel (%d)", client, status);
            else
                g_debug("[iw:%d] Can't write to IO channel (%d): %s", client, status, err->message);
            return FALSE;
        }
    }

    status = g_io_channel_flush(chan, &err);
    if (status != G_IO_STATUS_NORMAL) {
        if (err)
            g_debug("[iw:%d] Can't flush IO channel (%d)", client, status);
        else
            g_debug("[iw:%d] Can't flush IO channel (%d): %s", client, status, err->message);
        return FALSE;
    }

    return TRUE;
}


/* Notify clients (channels or plugins) that are waiting for an update */
void interface_notify() {
    GString* str = g_string_sized_new(1024);
    JsonBuilder* jb = json_builder_new();
    command_context ctx = { NULL, jb };

    json_builder_begin_object(jb);
    status(&ctx);
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

    interface_write(chan, str->str);
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
