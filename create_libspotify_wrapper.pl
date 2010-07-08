#!/usr/bin/perl -w

use File::Basename;

if ($#ARGV < 0) {
    print STDERR "Syntax: $0 /path/to/libspotify/api.h\n";
    exit 1;
}

local $dst_h = dirname($0)."/src/libspotify.h";
local $dst_c = dirname($0)."/src/libspotify_wrapper.c";
local $src = $ARGV[0];

open(DST_H, ">", $dst_h) or die $!;
open(DST_C, ">", $dst_c) or die $!;
open(SRC, "<", $src) or die $!;

# Begin with header
my $copyright_notice = <<CP_END;
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

/* Note: this file was auto-generated, do not modify it by hand. */
CP_END

print DST_H <<HDR_H_END;
$copyright_notice
#ifndef SPOTIFY_API_H
#define SPOTIFY_API_H

#include <libspotify/api.h>

HDR_H_END

print DST_C <<HDR_C_END;
$copyright_notice
#include <glib.h>
#include <libspotify/api.h>

static GStaticMutex g_sp_mutex = G_STATIC_MUTEX_INIT;

HDR_C_END

# Now parse the source file
foreach (<SRC>) {
    next unless /^SP_LIBEXPORT\((.+)\) (sp_[a-z_]+)\((.*)\);$/;

    # Return type, function name, args
    my ($rtype, $fname, $fargs) = ($1, $2, $3);

    # Args for invocation
    my @args;
    for (split /, /, $fargs) {
        # Remove type and star(s)
        s/.* \**([a-z_])?+/$1/;
        # Remove brackets
        s/\[.*\]//;

        push(@args, $_);
    }
    my $iargs = join ", ", @args;

    # Replacement function and macro
    if ($rtype eq "void") {
        print DST_C <<FUNC_END;
$rtype __safe_$fname($fargs) {
    g_static_mutex_lock(&g_sp_mutex);
    $fname($iargs);
    g_static_mutex_unlock(&g_sp_mutex);
}

FUNC_END

        print DST_H <<HDR_END;
$rtype __safe_$fname($fargs);
#define $fname($iargs) __safe_$fname($iargs)

HDR_END
    }
    else {
        print DST_C <<FUNC_END;
$rtype __safe_$fname($fargs) {
    $rtype __ret;
    g_static_mutex_lock(&g_sp_mutex);
    __ret = $fname($iargs);
    g_static_mutex_unlock(&g_sp_mutex);
    return __ret;
}

FUNC_END

        print DST_H <<HDR_END;
$rtype __safe_$fname($fargs);
#define $fname($iargs) __safe_$fname($iargs)

HDR_END
    }
}

# Footer
print DST_H <<FTR_END;

#endif
FTR_END
