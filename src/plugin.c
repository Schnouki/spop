/*
 * Copyright (C) 2010, 2011, 2012, 2013 Thomas Jost
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

#include "spop.h"
#include "config.h"
#include "plugin.h"

audio_delivery_func_ptr g_audio_delivery_func = NULL;

static GList* g_plugins_close_functions = NULL;

static GModule* plugin_open(char* module_name, char** search_path, gsize size_search_path) {
    int i;
    gchar* module_path;
    GModule* module;

    for (i=0; i < size_search_path; i++) {
        module_path = g_module_build_path(search_path[i], module_name);
        module = g_module_open(module_path, G_MODULE_BIND_LAZY);
        g_free(module_path);
        if (module)
            return module;
    }
    return g_module_open(module_name, G_MODULE_BIND_LAZY);
}

void plugins_init() {
    GString* module_name = NULL;
    GModule* module;

    gchar* audio_output;

    char** search_path;
    gsize search_path_size;
    char** plugins;
    gsize plugins_size;
    void (*plugin_init)();
    void (*plugin_close)();

    int i;

    module_name = g_string_sized_new(80);
    if (!module_name)
        g_error("Can't allocate memory.");

    search_path = config_get_string_list("plugins_search_path", &search_path_size);

    /* Load audio plugin */
    audio_output = config_get_string("audio_output");
    g_string_printf(module_name, "libspop_audio_%s", audio_output);

    module = plugin_open(module_name->str, search_path, search_path_size);
    if (!module)
        g_error("Can't load %s audio plugin: %s", audio_output, g_module_error());

    if (!g_module_symbol(module, "audio_delivery", (void**) &g_audio_delivery_func))
        g_error("Can't find symbol in audio plugin: %s", g_module_error());

    /* Now load other plugins */
    plugins = config_get_string_list("plugins", &plugins_size);
    for (i=0; i < plugins_size; i++) {
        g_strstrip(plugins[i]);
        g_info("Loading plugin %s...", plugins[i]);

        /* Load the module and the symbols (spop_<module>_init and spop_<module>_close) */
        g_string_printf(module_name, "libspop_plugin_%s", plugins[i]);
        module = plugin_open(module_name->str, search_path, search_path_size);
        if (!module) {
            g_warning("Can't load plugin \"%s\": %s", plugins[i], g_module_error());
            continue;
        }

        g_string_printf(module_name, "spop_%s_init", plugins[i]);
        if (!g_module_symbol(module, module_name->str, (void**) &plugin_init)) {
            g_warning("Can't find symbol \"%s\" in module \"%s\": %s", module_name->str, plugins[i], g_module_error());
            continue;
        }

        g_string_printf(module_name, "spop_%s_close", plugins[i]);
        if (g_module_symbol(module, module_name->str, (void**) &plugin_close))
            g_plugins_close_functions = g_list_prepend(g_plugins_close_functions, plugin_close);
        else
            g_info("Module \"%s\" does not have a \"%s\" symbol: %s", plugins[i], module_name->str, g_module_error());

        /* Really init the plugin (hoping it will not blow up) */
        plugin_init();

        g_debug("Plugin %s loaded and initialized", plugins[i]);
    }
    g_string_free(module_name, TRUE);
    g_strfreev(plugins);
    g_strfreev(search_path);
}

void plugins_close() {
    GList* cur = g_plugins_close_functions;
    void (*func)();

    g_debug("Closing plugins...");
    while (cur) {
        func = cur->data;
        func();
        cur = cur->next;
    }
}
