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
#include <gmodule.h>

#include "spop.h"
#include "config.h"
#include "plugin.h"

audio_delivery_func_ptr g_audio_delivery_func = NULL;

void plugins_init() {
    GString* module_name = NULL;
    GModule* module;

    gchar* audio_output;

    module_name = g_string_sized_new(80);
    if (!module_name)
        g_error("Can't allocate memory.");

    /* Load audio plugin */
    audio_output = config_get_string("audio_output");
    g_string_printf(module_name, "libspop_audio_%s", audio_output);

    module = g_module_open(module_name->str, G_MODULE_BIND_LAZY);
    if (!module)
        g_error("Can't load %s audio plugin: %s", audio_output, g_module_error());

    if (!g_module_symbol(module, "audio_delivery", (void**) &g_audio_delivery_func))
        g_error("Can't find symbol in audio plugin: %s", g_module_error());
}
