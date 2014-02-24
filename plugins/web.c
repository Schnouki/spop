/*
 * Copyright (C) 2013, 2014 Thomas Jost
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
#include <glib.h>
#include <gmodule.h>
#include <libsoup/soup.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "spop.h"
#include "commands.h"
#include "config.h"
#include "interface.h"
#include "spotify.h"

#define WEB_DEFAULT_IP   "127.0.0.1"
#define WEB_DEFAULT_PORT 8080

typedef struct {
    SoupServer* server;
    SoupMessage* msg;
} web_context;

/* Command finalizer (for async commands) */
static void web_command_finalize(gchar* json_result, web_context* ctx) {
    /* Send the JSON response to the client */
    soup_message_set_status(ctx->msg, SOUP_STATUS_OK);
    soup_message_set_response(ctx->msg, "application/json; charset=utf-8", SOUP_MEMORY_COPY,
                              json_result, strlen(json_result));
    soup_server_unpause_message(ctx->server, ctx->msg);
    g_free(ctx);
}

/* Callback for the idle command */
static void web_idle_notify(const GString* status, web_context* ctx) {
    /* Send the JSON status to the client */
    soup_message_set_status(ctx->msg, SOUP_STATUS_OK);
    soup_message_set_response(ctx->msg, "application/json; charset=utf-8", SOUP_MEMORY_COPY,
                              status->str, status->len);
    soup_server_unpause_message(ctx->server, ctx->msg);
    interface_notify_remove_callback((spop_notify_callback_ptr) web_idle_notify, ctx);
    g_free(ctx);

}

/* API requests handler */
static void web_api_handler(SoupServer* server, SoupMessage* msg,
                            const char* path, GHashTable* query,
                            SoupClientContext* client, gpointer user_data) {
    size_t i;

    if (msg->method != SOUP_METHOD_GET) {
        soup_message_set_status(msg, SOUP_STATUS_NOT_IMPLEMENTED);
        return;
    }

    /* Parse the path */
    const gchar* subpath = path+5; /* Strip "/api/" */
    gchar** cmd = g_strsplit(subpath, "/", -1);
    guint cmd_len = g_strv_length(cmd);
    if (cmd_len == 0) {
        /* No command */
        g_strfreev(cmd);
        soup_message_set_status(msg, SOUP_STATUS_NOT_FOUND);
        return;
    }

    /* Decode each part of the path */
    for (i=0; i < cmd_len; i++) {
        gchar* decoded = soup_uri_decode(cmd[i]);
        g_free(cmd[i]);
        cmd[i] = decoded;
    }

    /* Lookup the command */
    command_full_descriptor* cmd_desc = NULL;
    for (i=0; commands_descriptors[i].name != NULL; i++) {
        int nb_args = 0;
        while ((nb_args < MAX_CMD_ARGS) && (commands_descriptors[i].desc.args[nb_args] != CA_NONE))
            nb_args += 1;
        if ((strcmp(commands_descriptors[i].name, cmd[0]) == 0) && (nb_args == cmd_len-1)) {
            cmd_desc = &(commands_descriptors[i]);
            break;
        }
    }
    if (!cmd_desc) {
        /* Unknown command */
        g_strfreev(cmd);
        soup_message_set_status(msg, SOUP_STATUS_NOT_FOUND);
        return;
    }

    g_debug("web: found command %s with %d parameter(s)", cmd_desc->name, cmd_len-1);

    /* Add headers to prevent browser from caching answers */
    soup_message_headers_append(msg->response_headers,
                                "Expires", "Mon, 26 Jul 1997 05:00:00 GMT");
    /* soup_message_headers_append(msg->response_headers, */
    /*                             "Last-Modified", "Mon, 26 Jul 1997 05:00:00 GMT"); */
    soup_message_headers_append(msg->response_headers,
                                "Cache-Control", "no-cache, must-revalidate");
    soup_message_headers_append(msg->response_headers, "Pragma", "no-cache");

    /* Run the command if possible */
    web_context* ctx = NULL;
    switch(cmd_desc->type) {
    case CT_FUNC:
        /* Run the command asynchronously */
        ctx = g_new0(web_context, 1);
        ctx->server = server;
        ctx->msg = msg;
        soup_server_pause_message(server, msg);
        command_run((command_finalize_func) web_command_finalize, ctx,
                    &(cmd_desc->desc), cmd_len, cmd);
        break;

    case CT_IDLE:
        /* Register the notification callback */
        ctx = g_new0(web_context, 1);
        ctx->server = server;
        ctx->msg = msg;
        soup_server_pause_message(server, msg);
        if (!interface_notify_add_callback((spop_notify_callback_ptr) web_idle_notify, ctx))
            g_error("Could not add a web idle callback.");
        break;

    default:
        soup_message_set_status(msg, SOUP_STATUS_FORBIDDEN);
    }
    g_strfreev(cmd);
}

/* Static file requests handler */
static void web_static_handler(SoupServer* server, SoupMessage* msg,
                               const char* path, GHashTable* query,
                               SoupClientContext* client, gpointer static_root) {

    /* Only respond to GET and HEAD */
    if (msg->method != SOUP_METHOD_GET && msg->method != SOUP_METHOD_HEAD) {
        soup_message_set_status(msg, SOUP_STATUS_NOT_IMPLEMENTED);
        return;
    }

    /* Build the full path */
    gchar* decoded_path = soup_uri_decode(path);
    if (strcmp(decoded_path, "/") == 0) {
        /* "Redirect" / to /index.html */
        g_free(decoded_path);
        decoded_path = g_strdup("/index.html");
    }
    gchar* full_path = g_build_filename((gchar*) static_root, decoded_path, NULL);
    g_free(decoded_path);
    gchar* full_real_path = realpath(full_path, NULL);
    if (!full_real_path) {
        g_debug("web: realpath error (%s): %s", full_path, g_strerror(errno));
        g_free(full_path);
        soup_message_set_status(msg, SOUP_STATUS_NOT_FOUND);
        return;
    }
    g_free(full_path);

    /* Make sure it's a regular file in the static root */
    if (!g_str_has_prefix(full_real_path, (gchar*) static_root) || !g_file_test(full_real_path, G_FILE_TEST_IS_REGULAR)) {
        free(full_real_path);
        soup_message_set_status(msg, SOUP_STATUS_NOT_FOUND);
        return;
    }

    /* Try to guess the content type */
    gchar* content_type;
    if (g_str_has_suffix(full_real_path, ".css"))
        content_type = "text/css";
    else if (g_str_has_suffix(full_real_path, ".js"))
        content_type = "application/javascript";
    else if (g_str_has_suffix(full_real_path, ".html"))
        content_type = "text/html";
    else
        content_type = "application/octet-stream";

    /* Now serve the file! */
    /* FIXME: this should be asynchronous and should not load the entire file in memory... */
    GError* err = NULL;
    gchar* body = NULL;
    gsize length;
    if (!g_file_get_contents(full_real_path, &body, &length, &err)) {
        g_warning("web: error while reading %s: %s", full_real_path, err->message);
        free(full_real_path);
        soup_message_set_status(msg, SOUP_STATUS_INTERNAL_SERVER_ERROR);
        return;
    }
    free(full_real_path);

    soup_message_set_status(msg, SOUP_STATUS_OK);
    soup_message_set_response(msg, content_type, SOUP_MEMORY_TAKE, body, length);

    return;
}

/* Request logger */
static void web_logger(SoupServer* server, SoupMessage* msg, SoupClientContext* client, gpointer user_data) {
    SoupURI* uri = soup_message_get_uri(msg);
    if (uri) {
        gchar* path = soup_uri_to_string(uri, TRUE);
        g_info("web: [%s] %s %s %u (%ld)", soup_client_context_get_host(client),
               msg->method, path,
               msg->status_code, soup_message_headers_get_content_length(msg->response_headers));
        g_free(path);
    }
}

/* Plugin initialization */
G_MODULE_EXPORT void spop_web_init() {
    /* Get IP/port to listen on */
    gchar* default_ip = g_strdup(WEB_DEFAULT_IP);
    gchar* web_ip = config_get_string_opt_group("web", "ip", default_ip);
    guint web_port = config_get_int_opt_group("web", "port", WEB_DEFAULT_PORT);

    /* Get the root for static files */
    gchar* default_static_root = g_strdup(WEB_STATIC_ROOT);
    gchar* static_root = config_get_string_opt_group("web", "root", default_static_root);
    g_free(default_static_root);

    /* Ensure the static root ends with a / */
    if (!g_str_has_suffix(static_root, "/")) {
        char* new_static_root = g_strdup_printf("%s/", static_root);
        g_free(static_root);
        static_root = new_static_root;
    }

    SoupAddress* addr = soup_address_new(web_ip, web_port);
    g_free(default_ip);
    if (soup_address_resolve_sync(addr, NULL) != SOUP_STATUS_OK)
        g_error("Could not resolve hostname for the web server");

    /* Init the server and add URL handlers */
    SoupServer* server = soup_server_new("interface", addr,
                                         "port", web_port,
                                         "server-header", "spop/" SPOP_VERSION " ",
                                         "raw-paths", TRUE,
                                         NULL);
    if (!server)
        g_error("Could not initialize web server");

    soup_server_add_handler(server, "/api", web_api_handler, NULL, NULL);
    soup_server_add_handler(server, "/", web_static_handler, static_root, NULL);
    g_signal_connect(server, "request-finished", G_CALLBACK(web_logger), NULL);

    /* Start the server */
    soup_server_run_async(server);
    g_info("web: Listening on %s:%d", soup_address_get_physical(addr), web_port);
}

G_MODULE_EXPORT void spop_web_close() {
}
