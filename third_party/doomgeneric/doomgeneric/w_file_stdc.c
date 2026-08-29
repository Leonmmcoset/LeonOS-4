//
// Copyright(C) 1993-1996 Id Software, Inc.
// Copyright(C) 2005-2014 Simon Howard
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// DESCRIPTION:
//	WAD I/O functions.
//

#include <stdio.h>

#if defined(LEONOS_DOOM)
#include "doomgeneric.h"
#include <leonos/fs.h>
#endif

#include "m_misc.h"
#include "w_file.h"
#include "z_zone.h"

typedef struct
{
    wad_file_t wad;
    FILE *fstream;
} stdc_wad_file_t;

extern wad_file_class_t stdc_wad_file;

static void W_StdC_YieldAfterRead(void)
{
#if defined(LEONOS_DOOM)
    static unsigned int read_count;

    /* Renderer setup can issue thousands of short, non-sequential reads.
     * Give the window server and storage worker a scheduling point after a
     * small batch, rather than monopolising a core until all WAD lookups are
     * complete. */
    if (++read_count % 8U == 0U)
    {
        DG_SleepMs(1);
    }
#endif
}

static wad_file_t *W_StdC_OpenFile(char *path)
{
    stdc_wad_file_t *result;
    FILE *fstream;

    fstream = fopen(path, "rb");

    if (fstream == NULL)
    {
        return NULL;
    }

    // Create a new stdc_wad_file_t to hold the file handle.

    result = Z_Malloc(sizeof(stdc_wad_file_t), PU_STATIC, 0);
    result->wad.file_class = &stdc_wad_file;
    result->wad.mapped = NULL;
    result->wad.length = M_FileLength(fstream);
    result->fstream = fstream;

    return &result->wad;
}

/* The kernel's filesystem API may complete a large read in several slices.
 * stdio's fread() is allowed to return a short count, but a WAD reader must
 * never silently accept one: the caller would then parse uninitialised bytes
 * as a lump directory and report misleading renderer errors such as
 * "Missing patch".  Keep the retry/short-read handling at this boundary so
 * every WAD access (header, directory and lump data) has the same contract. */
static size_t W_StdC_ReadExact(FILE *stream, unsigned int offset,
                               void *buffer, size_t length)
{
    size_t done = 0;
    unsigned char *dst = (unsigned char *)buffer;
    while (done < length)
    {
        size_t request = length - done;
#if defined(LEONOS_DOOM)
        /* The LeonOS file path is sliced at 32 KiB. Keep a WAD read inside
         * one such window so an unaligned lump directory cannot straddle two
         * filesystem stream transactions. In particular, Freedoom's entry
         * 1615 starts four bytes into the following window. */
        size_t window_remaining = LEONOS_FS_READ_SLICE_BYTES -
                                  ((offset + done) % LEONOS_FS_READ_SLICE_BYTES);
        if (request > window_remaining)
        {
            request = window_remaining;
        }
#endif
        size_t got = fread(dst + done, 1, request, stream);
        if (got == 0)
        {
            break;
        }
        done += got;
    }
    return done;
}

static void W_StdC_CloseFile(wad_file_t *wad)
{
    stdc_wad_file_t *stdc_wad;

    stdc_wad = (stdc_wad_file_t *) wad;

    fclose(stdc_wad->fstream);
    Z_Free(stdc_wad);
}

// Read data from the specified position in the file into the 
// provided buffer.  Returns the number of bytes read.

size_t W_StdC_Read(wad_file_t *wad, unsigned int offset,
                   void *buffer, size_t buffer_len)
{
    stdc_wad_file_t *stdc_wad;
    size_t result;

    stdc_wad = (stdc_wad_file_t *) wad;

    if (!stdc_wad || !stdc_wad->fstream || (!buffer && buffer_len != 0))
    {
        return 0;
    }

    // Jump to the specified position in the file.
    if (fseek(stdc_wad->fstream, (long)offset, SEEK_SET) != 0)
    {
        printf("[doom] WAD seek failed offset=%u length=%u\n", offset,
               (unsigned int)buffer_len);
        return 0;
    }

    // Read the complete request or return the exact short count.
    result = W_StdC_ReadExact(stdc_wad->fstream, offset, buffer, buffer_len);
    if (result != buffer_len)
    {
        printf("[doom] WAD short read offset=%u requested=%u got=%u\n",
               offset, (unsigned int)buffer_len, (unsigned int)result);
    }
    W_StdC_YieldAfterRead();

    return result;
}


wad_file_class_t stdc_wad_file = 
{
    W_StdC_OpenFile,
    W_StdC_CloseFile,
    W_StdC_Read,
};
