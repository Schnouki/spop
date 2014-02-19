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

#ifndef PLUGIN_H
#define PLUGIN_H

#include <libspotify/api.h>

typedef int (*audio_delivery_func_ptr)(const sp_audioformat*, const void*, int);
extern audio_delivery_func_ptr g_audio_delivery_func;

typedef void (*audio_buffer_stats_func_ptr)(sp_session*, sp_audio_buffer_stats*);
extern audio_buffer_stats_func_ptr g_audio_buffer_stats_func;

void plugins_init();
void plugins_close();

#endif
