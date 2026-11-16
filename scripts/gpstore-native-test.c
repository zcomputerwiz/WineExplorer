/*
 * Command-line driver for src/dlls/wexgpstore/native.c, for scripts/test_gpstore_native.sh
 *
 * Copyright 2026 WineExplorer contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
 */

/*
 *   gpstore-native-test PREFIX open
 *   gpstore-native-test PREFIX read KIND OUTFILE     OUTFILE gets the request's bytes
 *   gpstore-native-test PREFIX write KIND INFILE     INFILE empty: removes the store file
 *   gpstore-native-test PREFIX done KIND [ERROR]
 *   gpstore-native-test PREFIX clean
 * Prints "result=N" (enum gps_result) and, for read, "size=N".
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/dlls/wexgpstore/native.h"

static unsigned char *slurp( const char *path, size_t *size )
{
    FILE *f = fopen( path, "rb" );
    unsigned char *data;
    long length;

    if (!f) return NULL;
    fseek( f, 0, SEEK_END );
    length = ftell( f );
    fseek( f, 0, SEEK_SET );
    data = malloc( length ? length : 1 );
    if (!data || (length && fread( data, 1, length, f ) != (size_t)length)) { fclose( f ); return NULL; }
    fclose( f );
    *size = length;
    return data;
}

int main( int argc, char **argv )
{
    struct gps_store store;
    int result;

    if (argc < 3) return 2;
    result = gps_open( argv[1], &store );
    if (result) { printf( "result=%d\n", result ); return 1; }
    if (!strcmp( argv[2], "open" )) printf( "result=0\n" );
    else if (!strcmp( argv[2], "clean" )) { gps_clean( &store ); printf( "result=0\n" ); }
    else if (argc >= 4)
    {
        enum gps_kind kind = atoi( argv[3] );

        if (!strcmp( argv[2], "read" ) && argc >= 5)
        {
            unsigned char *data;
            size_t size;
            FILE *out;

            result = gps_spool_read( &store, kind, &data, &size );
            if (!result && (out = fopen( argv[4], "wb" )))
            {
                if (size) fwrite( data, 1, size, out );
                fclose( out );
            }
            printf( "result=%d size=%zu\n", result, size );
            free( data );
        }
        else if (!strcmp( argv[2], "write" ) && argc >= 5)
        {
            size_t size = 0;
            unsigned char *data = slurp( argv[4], &size );

            if (!data) { printf( "result=-1\n" ); return 1; }
            result = gps_store_write( &store, kind, size ? data : NULL, size );
            printf( "result=%d\n", result );
            free( data );
        }
        else if (!strcmp( argv[2], "done" ))
        {
            result = gps_spool_done( &store, kind, argc >= 5 ? argv[4] : NULL );
            printf( "result=%d\n", result );
        }
        else return 2;
    }
    else return 2;
    gps_close( &store );
    return 0;
}
