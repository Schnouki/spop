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

#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>

#include "spop.h"
#include "config.h"
#include "plugin.h"

audio_delivery_func_ptr g_audio_delivery_func = NULL;

void plugins_init() {
    void* lib_audio;
    void* lib_interface;
    const char* audio_output;
    const char* interface;
    void (*interface_init)();
    char lib_name[80];
    char* error;

    /* Load audio plugin */
    audio_output = config_get_string("audio_output");
    snprintf(lib_name, sizeof(lib_name), "libspop_%s.so", audio_output);

    lib_audio = dlopen(lib_name, RTLD_LAZY);
    if (!lib_audio) {
        fprintf(stderr, "Can't load audio plugin %s: %s\n", lib_name, dlerror());
        exit(1);
    }
    dlerror(); /* Clear any existing error */
    g_audio_delivery_func = dlsym(lib_audio, "audio_delivery");
    if ((error = dlerror()) != NULL) {
        fprintf(stderr, "Can't find symbol in audio plugin: %s\n", error);
        exit(1);
    }

    /* Load interface plugin */
    interface = config_get_string("interface");
    snprintf(lib_name, sizeof(lib_name), "libspop_interface_%s.so", interface);

    lib_interface = dlopen(lib_name, RTLD_LAZY);
    if (!lib_interface) {
        fprintf(stderr, "Can't load interface plugin %s: %s\n", lib_name, dlerror());
        exit(1);
    }
    dlerror(); /* Clear any existing error */
    interface_init = dlsym(lib_interface, "interface_init");
    if ((error = dlerror()) != NULL) {
        fprintf(stderr, "Can't find symbol in interface plugin: %s\n", error);
        exit(1);
    }
    interface_init();    
}
