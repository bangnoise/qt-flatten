//
//  qt_flatten.c
//
//  Created by Tom Butterworth on 08/05/2012.
//  Copyright (c) 2012 Tom Butterworth. All rights reserved.
//
//  Redistribution and use in source and binary forms, with or without
//  modification, are permitted provided that the following conditions are met:
//
//  * Redistributions of source code must retain the above copyright
//  notice, this list of conditions and the following disclaimer.
//
//  * Redistributions in binary form must reproduce the above copyright
//  notice, this list of conditions and the following disclaimer in the
//  documentation and/or other materials provided with the distribution.
//
//  * Neither the name of qt-flatten nor the name of its contributors
//  may be used to endorse or promote products derived from this software
//  without specific prior written permission.
//
//  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
//  ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
//  WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
//  DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY
//  DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
//  (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
//  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
//  ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
//  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
//  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include "qt_flatten.h"

#include <stdlib.h> // malloc, free
#include <stdint.h> // sized & signed types
#include <fcntl.h> // open
#include <sys/param.h> // MIN
#include <string.h> // memcpy
#include <sys/stat.h> // fstat
#include <zlib.h> // inflate, deflate

#define QTF_FCC_ftyp (0x66747970)
#define QTF_FCC_moov (0x6d6f6f76)
#define QTF_FCC_free (0x66726565)
#define QTF_FCC_skip (0x736B6970)
#define QTF_FCC_wide (0x77696465)
#define QTF_FCC_mdat (0x6d646174)
#define QTF_FCC_qt__ (0x71742020)
#define QTF_FCC_cmov (0x636d6f76)
#define QTF_FCC_dcom (0x64636f6d)
#define QTF_FCC_cmvd (0x636d7664)
#define QTF_FCC_zlib (0x7a6c6962)
#define QTF_FCC_trak (0x7472616b)
#define QTF_FCC_mdia (0x6d646961)
#define QTF_FCC_minf (0x6d696e66)
#define QTF_FCC_stbl (0x7374626c)
#define QTF_FCC_stco (0x7374636f)
#define QTF_FCC_co64 (0x636F3634)

#if defined(__APPLE__)
#include <libkern/OSByteOrder.h>
#define qtf_swap_big_to_host_int_32(x) OSSwapBigToHostInt32((x))
#define qtf_swap_big_to_host_int_64(x) OSSwapBigToHostInt64((x))
#define qtf_swap_host_to_big_int_32(x) OSSwapHostToBigInt32((x))
#define qtf_swap_host_to_big_int_64(x) OSSwapHostToBigInt64((x))
#elif defined(__linux__)
#include <endian.h>
#define qtf_swap_big_to_host_int_32(x) be32toh((x))
#define qtf_swap_big_to_host_int_64(x) be64toh((x))
#define qtf_swap_host_to_big_int_32(x) htobe32((x))
#define qtf_swap_host_to_big_int_64(x) htobe64((x))
#elif defined(__WIN32)
static __inline unsigned short qtf_swap_16(unsigned short x)
{
  return (x >> 8) | (x << 8);
}

static __inline unsigned int qtf_swap_32(unsigned int x)
{
  return (qtf_swap_16(x & 0xffff) << 16) | (qtf_swap_16(x >> 16));
}

static __inline unsigned long long qtf_swap_64 (unsigned long long x)
{
  return (((unsigned long long) qtf_swap_32(x & 0xffffffffull)) << 32) | (qtf_swap_32 (x >> 32));
}

#define qtf_swap_big_to_host_int_32(x) qtf_swap_32((x))
#define qtf_swap_big_to_host_int_64(x) qtf_swap_64((x))
#define qtf_swap_host_to_big_int_32(x) qtf_swap_32((x))
#define qtf_swap_host_to_big_int_64(x) qtf_swap_64((x))
#endif

#ifndef MIN
#define MIN(x,y) (((x) < (y)) ? (x) : (y))
#endif

#define QTF_COPY_BUFFER_SIZE (10240)

/*
 *  Compression/Decompression
 */

static size_t qtf_decompress_data(void *source_buffer, size_t source_buffer_length,
                                  void *decompressed_buffer, size_t decompressed_buffer_length)
{
    size_t size_out = 0;
    uLongf decompressed_length = decompressed_buffer_length;
    
    int result = uncompress(decompressed_buffer, &decompressed_length, source_buffer, source_buffer_length);
    
    if (result == Z_OK) size_out = decompressed_length;
    
    return size_out;
}

static size_t qtf_compress_data(void *source_buffer, size_t source_buffer_length,
                                void *compressed_buffer, size_t compressed_buffer_length,
                                int compression_level)
{
    size_t size_out = 0;
    uLongf compressed_length = compressed_buffer_length;
    
    int result = compress2(compressed_buffer, &compressed_length, source_buffer, source_buffer_length, compression_level);
    
    if (result == Z_OK) size_out = compressed_length;
    
    return size_out;
}

/*
 *  qtf_edit_list
 *
 *  qtf_edit_list maintains a list of data insertions and removals and calculates the overall change for data at given offsets
 */

typedef struct qtf_edit_s **qtf_edit_list;

typedef struct qtf_edit_s
{
    off_t offset;
    ssize_t edit;
    struct qtf_edit_s *next;
} qtf_edit_s;


static qtf_edit_list qtf_edit_list_create()
{
    qtf_edit_list list = malloc(sizeof(qtf_edit_s *));
    if (list)
    {
        *list = NULL;
    }
    return list;
}

static void qtf_edit_list_destroy(qtf_edit_list list)
{
    if (list)
    {
        qtf_edit_s *edit = *list;
        while (edit) {
            qtf_edit_s *next = edit->next;
            free(edit);
            edit = next;
        }
        free(list);
    }
}

static void qtf_edit_list_add_edit(qtf_edit_list list, off_t offset, ssize_t edit)
{
    if (list)
    {
        qtf_edit_s *entry = malloc(sizeof(qtf_edit_s));
        if (entry)
        {
            entry->offset = offset;
            entry->edit = edit;
            entry->next = *list;
            *list = entry;
        }
    }
}

static ssize_t qtf_edit_list_get_offset_change(qtf_edit_list list, off_t offset)
{
    ssize_t change = 0;
    if (list)
    {
        qtf_edit_s *edit = *list;
        while (edit) {
            if (edit->offset <= offset)
            {
                change += edit->edit;
            }
            edit = edit->next;
        }
    }
    return change;
}

/*
 *  Utility
 */

/*
 qtf_read is like read except it returns an error if the expected number of bytes couldn't be read
 */
static qtf_result qtf_read(int fd, void *buffer, size_t length)
{
    off_t got = read(fd, buffer, length);
    
    if (got == -1)
    {
        return qtf_result_file_read_error;
    }
    else if (got != length)
    {
        return qtf_result_file_not_movie;
    }
    return qtf_result_ok;
}

/*
 qtf_write is like write except it returns an error if the expected number of bytes couldn't be written
 */
static qtf_result qtf_write(int fd, const void *buffer, size_t length)
{
    ssize_t written = write(fd, buffer, length);
    if (written == -1 || written != length)
    {
        return qtf_result_file_write_error;
    }
    return qtf_result_ok;
}

/*
 returns 0 on success or a qtf_result
 */
static qtf_result qtf_get_file_size(int fd, size_t *out_file_size)
{
    struct stat stat_info;
    
    int got = fstat(fd, &stat_info);
    if (got == -1)
    {
        return qtf_result_file_read_error;
    }
    
    *out_file_size = stat_info.st_size;
    
    return qtf_result_ok;
}

/*
 returns 0 on success or a qtf_result
 */
static qtf_result qtf_read_atom_header(int fd, void *dest_buffer, size_t dest_buffer_length, uint32_t *out_type, uint64_t *out_size, size_t *out_bytes_read)
{
    *out_bytes_read = 0;
    *out_size = 0;
    *out_type = 0;
    
    if (dest_buffer_length < 16)
    {
        return qtf_result_memory_error;
    }
    uint64_t size;
    uint32_t type;
    off_t got = read(fd, dest_buffer, 8);
    
    if (got == -1)
    {
        return qtf_result_file_read_error;
    }
    *out_bytes_read += got;
    
    if (got == 0)
    {
        return 0;
    }
    else if (got != 8)
    {
        return qtf_result_file_not_movie;
    }
    
    size = qtf_swap_big_to_host_int_32(*(uint32_t *)dest_buffer);
    type = qtf_swap_big_to_host_int_32(*(uint32_t *)(dest_buffer + 4));
    
    if (size == 0)
    {
        off_t offset = lseek(fd, 0, SEEK_CUR);
        if (offset == -1)
        {
            return qtf_result_file_read_error;
        }
        
        size_t file_size = 0;
        qtf_result result = qtf_get_file_size(fd, &file_size);
        if (result != qtf_result_ok) return result;
        
        size = file_size - offset + 8;
    }
    else if (size == 1)
    {
        qtf_result result = qtf_read(fd, dest_buffer + 8, 8);
        if (result != qtf_result_ok) return result;
        
        *out_bytes_read += 8;
        
        size = qtf_swap_big_to_host_int_64(*(uint64_t *)(dest_buffer + 8));
    }
    *out_size = size;
    *out_type = type;
    return qtf_result_ok;
}

// set as many try_ flags as you want, they will be tried sequentially until one works in the given buffer size
// returns the size of the compressed atom on success, or 0 on failure
static size_t qtf_compress_movie_atom(void *atom_buffer, size_t atom_buffer_length,
                                      void *compressed_atom_buffer, size_t compressed_atom_buffer_length,
                                      bool try_fast, bool try_default, bool try_best)
{
    // leave space for the compressed movie atoms (40 bytes)
    size_t compressed_data_max_length = compressed_atom_buffer_length - 40;
    void *compressed_data = compressed_atom_buffer + 40;
    size_t compressed_data_length = 0;
    if (try_fast)
    {
        compressed_data_length = qtf_compress_data(atom_buffer, atom_buffer_length, compressed_data, compressed_data_max_length, Z_BEST_SPEED);
    }
    if (try_default && compressed_data_length == 0)
    {
        compressed_data_length = qtf_compress_data(atom_buffer, atom_buffer_length, compressed_data, compressed_data_max_length, Z_DEFAULT_COMPRESSION);
    }
    if (try_best || compressed_data_length == 0)
    {
        compressed_data_length = qtf_compress_data(atom_buffer, atom_buffer_length, compressed_data, compressed_data_max_length, Z_BEST_COMPRESSION);
    }
    if (compressed_data_length != 0)
    {
        // write the compressed movie atom header before the compressed data
        *(uint32_t *)compressed_atom_buffer = qtf_swap_host_to_big_int_32(compressed_data_length + 40);
        *(uint32_t *)(compressed_atom_buffer + 4) = qtf_swap_host_to_big_int_32(QTF_FCC_moov);
        *(uint32_t *)(compressed_atom_buffer + 8) = qtf_swap_host_to_big_int_32(compressed_data_length + 32);
        *(uint32_t *)(compressed_atom_buffer + 12) = qtf_swap_host_to_big_int_32(QTF_FCC_cmov);
        *(uint32_t *)(compressed_atom_buffer + 16) = qtf_swap_host_to_big_int_32(12);
        *(uint32_t *)(compressed_atom_buffer + 20) = qtf_swap_host_to_big_int_32(QTF_FCC_dcom);
        *(uint32_t *)(compressed_atom_buffer + 24) = qtf_swap_host_to_big_int_32(QTF_FCC_zlib);
        *(uint32_t *)(compressed_atom_buffer + 28) = qtf_swap_host_to_big_int_32(compressed_data_length + 12);
        *(uint32_t *)(compressed_atom_buffer + 32) = qtf_swap_host_to_big_int_32(QTF_FCC_cmvd);
        *(uint32_t *)(compressed_atom_buffer + 36) = qtf_swap_host_to_big_int_32(atom_buffer_length);
        
        compressed_data_length += 40;
    }
    return compressed_data_length;
}

static qtf_result qtf_offsets_apply_list(void *moov_atom, size_t moov_atom_size, qtf_edit_list edit_list)
{
    qtf_result result = qtf_result_ok;
    for (int i = 8; i < moov_atom_size; ) {
        uint32_t size = qtf_swap_big_to_host_int_32(*(uint32_t *)(moov_atom + i));
        uint32_t type = qtf_swap_big_to_host_int_32(*(uint32_t *)(moov_atom + i + 4));
        if (size > (moov_atom_size - i))
        {
            result = qtf_result_file_not_movie;
            break;
        }
        if (type == QTF_FCC_stco)
        {
            uint32_t entry_count = qtf_swap_big_to_host_int_32(*(uint32_t *)(moov_atom + i + 12));
            if (entry_count * 4 > size - 16)
            {
                result = qtf_result_file_not_movie;
                break;
            }
            for (int j = 0; j < entry_count; j++) {
                uint32_t *entry = moov_atom + i + 16 + (j * 4);
                uint32_t current_offset = qtf_swap_big_to_host_int_32(*entry);
                current_offset += qtf_edit_list_get_offset_change(edit_list, current_offset);
                *entry = qtf_swap_host_to_big_int_32(current_offset);
            }
        }
        else if (type == QTF_FCC_co64)
        {
            uint32_t entry_count = qtf_swap_big_to_host_int_32(*(uint32_t *)(moov_atom + i + 12));
            if (entry_count * 8 > size - 16)
            {
                result = qtf_result_file_not_movie;
                break;
            }
            for (int j = 0; j < entry_count; j++) {
                uint64_t *entry = moov_atom + i + 16 + (j * 8);
                uint64_t current_offset = qtf_swap_big_to_host_int_64(*entry);
                current_offset += qtf_edit_list_get_offset_change(edit_list, current_offset);
                *entry = qtf_swap_host_to_big_int_64(current_offset);
            }
        }
        switch (type) {
            case QTF_FCC_trak:
            case QTF_FCC_mdia:
            case QTF_FCC_minf:
            case QTF_FCC_stbl:
                // enter it
                i += 8;
                break;
            default:
                // skip it
                i += size;
                break;
        }
    }
    return result;
}

static qtf_result qtf_offsets_modify(void *moov_atom, size_t moov_atom_size, ssize_t change)
{
    // fake a qtf_edit_list with one edit at offset 0
    qtf_edit_s edit = {0, change, NULL};
    qtf_edit_s *edit_ptr = &edit;
    
    return qtf_offsets_apply_list(moov_atom, moov_atom_size, &edit_ptr);
}

/*
 *  Public Functions
 */

qtf_result qtf_flatten_movie(const char *src_path, const char *dst_path, bool allow_compressed_moov_atom)
{
    // open source
#if defined(_WIN32)
    int fd_source = _open(src_path, _O_RDONLY | _O_BINARY);
#else
    int fd_source = open(src_path, O_RDONLY);
#endif
    if (fd_source == -1)
    {
        return qtf_result_file_read_error;
    }
    
    // an error, to return when we finish
    int result = 0;
    // the atoms we will directly deal with
    void *atom_ftyp = NULL;
    size_t atom_ftyp_size = 0;
    void *atom_moov = NULL;
    size_t atom_moov_size = 0;
    
    bool atom_mdat_present = false;
    
    qtf_edit_list edit_list = qtf_edit_list_create();
    if (edit_list == NULL) result = qtf_result_memory_error;
    
    off_t offset = 0;
    
    // copy the ftyp atom if present and the moov atom, get other information we need to ignore free space in the file
    while (result == qtf_result_ok) {
        uint32_t atom_header[4];
        uint64_t size = 0;
        uint32_t type = 0;
        size_t bytes_read;
        result = qtf_read_atom_header(fd_source, atom_header, sizeof(atom_header), &type, &size, &bytes_read);
        
        if (result != 0 || bytes_read == 0) break;
        
        switch (type) {
            case QTF_FCC_ftyp:
                // QTFF Chapter 1, The File Type Compatibility Atom
                // TODO: could drop 0x0 compatibility atoms (save 3 bytes from QT-built files, woo)
                if (atom_ftyp_size != 0 || offset != 0)
                {
                    result = qtf_result_file_not_movie; // there must be only one ftyp atom, and it must be the first atom
                }
                else
                {
                    if (bytes_read != 8 || size < 20) result = qtf_result_file_not_movie; // minimally useful size 20 bytes: atom header(8) + major brand(4) + version(4) + 1 compatible brand(4)
                    
                    if (result == qtf_result_ok)
                    {
                        atom_ftyp_size = size;
                        atom_ftyp = malloc(atom_ftyp_size);
                        if (atom_ftyp == NULL)
                        {
                            result = qtf_result_memory_error;
                        }
                    }
                    if (result == qtf_result_ok)
                    {
                        // copy what we already read
                        memcpy(atom_ftyp, atom_header, bytes_read);
                        // read the rest
                        result = qtf_read(fd_source, atom_ftyp + bytes_read, atom_ftyp_size - bytes_read);
                        if (result == qtf_result_ok)
                        {
                            bytes_read += atom_ftyp_size - bytes_read;
                        }
                    }
                    if (result == qtf_result_ok)
                    {
                        // Check for compatibility
                        int brand_count = (atom_ftyp_size - 16) / 4;
                        bool has_qt = false;
                        for (int i = 0; i < brand_count; i++) {
                            uint32_t brand = qtf_swap_big_to_host_int_32(*(uint32_t *)(atom_ftyp + 16 + (i * 4)));
                            if (brand == QTF_FCC_qt__)
                            {
                                has_qt = true;
                                break;
                            }
                        }
                        if (!has_qt) result = qtf_result_file_not_movie;
                    }
                }
                break;
            case QTF_FCC_moov:
                // remove the atom from the file in its current location, we will add it again later
                qtf_edit_list_add_edit(edit_list, offset, -(ssize_t)size);
                // there should only be one of these, we discard any others
                if (atom_moov_size == 0)
                {
                    atom_moov_size = size;
                    atom_moov = malloc(atom_moov_size);
                    size_t contents_bytes_read = 0;
                    uint64_t contents_size = 0;
                    uint32_t contents_type = 0;
                    
                    uint32_t decompressed_size = 0;
                    off_t compressed_data_start = 0;
                    
                    if (atom_moov == NULL)
                    {
                        result = qtf_result_memory_error;
                    }
                    if (result == qtf_result_ok)
                    {
                        // copy what we already read
                        memcpy(atom_moov, atom_header, bytes_read);
                        
                        // read the first atom header inside the moov atom straight into the moov atom in memory
                        result = qtf_read_atom_header(fd_source, atom_moov + bytes_read, atom_moov_size - bytes_read, &contents_type, &contents_size, &contents_bytes_read);
                        bytes_read += contents_bytes_read;
                    }
                    if (result == qtf_result_ok)
                    {
                        // check if the atom is compressed
                        // QTFF Chapter 2, Compressed Movie Resources
                        if (contents_type == QTF_FCC_cmov)
                        {                            
                            // read the next atom header inside the cmov atom
                            result = qtf_read_atom_header(fd_source, atom_moov + bytes_read, atom_moov_size - bytes_read, &contents_type, &contents_size, &contents_bytes_read);
                            bytes_read += contents_bytes_read;
                            // check it's a valid dcom atom
                            if (result == qtf_result_ok && (contents_type != QTF_FCC_dcom || (contents_size - contents_bytes_read) != 4)) result = qtf_result_file_not_movie;
                            if (result == qtf_result_ok)
                            {
                                // read the 4 byte compression type from the dcom atom
                                result = qtf_read(fd_source, atom_moov + bytes_read, 4);
                                if (result == qtf_result_ok)
                                {
                                    // check for zlib compression
                                    uint32_t compression = qtf_swap_big_to_host_int_32(*(uint32_t *)(atom_moov + bytes_read));
                                    if (compression != QTF_FCC_zlib)
                                    {
                                        result = qtf_result_file_too_complex;
                                    }
                                    bytes_read += 4;
                                }
                            }
                            if (result == qtf_result_ok)
                            {
                                // read the cmvd atom header
                                result = qtf_read_atom_header(fd_source, atom_moov + bytes_read, atom_moov_size - bytes_read, &contents_type, &contents_size, &contents_bytes_read);
                                bytes_read += contents_bytes_read;
                                // check it's a valid cmvd atom
                                if (result == qtf_result_ok && (contents_type != QTF_FCC_cmvd || (contents_size - contents_bytes_read) < 4)) result = qtf_result_file_not_movie;
                                if (result == qtf_result_ok)
                                {
                                    // read the 4 byte decompressed size
                                    result = qtf_read(fd_source, atom_moov + bytes_read, 4);
                                    if (result == qtf_result_ok)
                                    {
                                        decompressed_size = qtf_swap_big_to_host_int_32(*(uint32_t *)(atom_moov + bytes_read));
                                        if (decompressed_size == 0) result = qtf_result_file_not_movie;
                                        bytes_read += 4;
                                        compressed_data_start = bytes_read;
                                    }
                                }
                            }
                        }
                    }
                    
                    if (result == qtf_result_ok)
                    {
                        // read the rest of the atom
                        result = qtf_read(fd_source, atom_moov + bytes_read, atom_moov_size - bytes_read);
                        if (result == qtf_result_ok)
                        {
                            bytes_read += atom_moov_size - bytes_read;
                        }
                    }
                    if (result == qtf_result_ok && decompressed_size != 0) // ie the moov atom is compressed
                    {
                        void *atom_moov_decompressed = malloc(decompressed_size);
                        if (atom_moov_decompressed == NULL) result = qtf_result_memory_error;
                        if (result == qtf_result_ok)
                        {
                            size_t actuallly_decompressed = qtf_decompress_data(atom_moov + compressed_data_start, atom_moov_size - compressed_data_start,
                                                                                atom_moov_decompressed, decompressed_size);
                            
                            if (actuallly_decompressed != decompressed_size) result = qtf_result_file_not_movie;
                            else
                            {
                                free(atom_moov);
                                atom_moov = atom_moov_decompressed;
                                atom_moov_decompressed = NULL;
                                atom_moov_size = decompressed_size;
                            }
                        }
                        free(atom_moov_decompressed); // if all went well this will be NULL by now
                    }
                }
                break;
            case QTF_FCC_free:
            case QTF_FCC_skip:
            case QTF_FCC_wide:
                qtf_edit_list_add_edit(edit_list, offset, -(ssize_t)size);
                break;
            case QTF_FCC_mdat:
                atom_mdat_present = true;
                break;
            default:
                break;
        }
        
        offset = lseek(fd_source, size - bytes_read, SEEK_CUR);
        if (offset == -1)
        {
            result = qtf_result_file_read_error;
        }
    }
    // check we can do something with this file
    if (result == qtf_result_ok && (atom_mdat_present == false || atom_moov_size == 0))
    {
        result = qtf_result_file_too_complex;
    }
    
    if (allow_compressed_moov_atom)
    {
        void *atom_moov_compressed = NULL;
        size_t atom_moov_compressed_size = 0;
        size_t atom_moov_compressed_expected_size = 0;
        size_t atom_moov_compressed_actual_size = 0;
        
        if (atom_moov_size < 20) result = qtf_result_file_not_movie;
        
        if (result == qtf_result_ok)
        {
            if (result == qtf_result_ok)
            {
                atom_moov_compressed_size = atom_moov_size;
                atom_moov_compressed = malloc(atom_moov_compressed_size);
                if (atom_moov_compressed == NULL)
                {
                    result = qtf_result_memory_error;
                }
            }
            if (result == qtf_result_ok)
            {
                // We have to estimate a compressed size, modify the sample data offsets for that estimated size, then compress
                // the modified atom and see if we met our target. If not, repeat the process, allowing a little more space until
                // we succeed or arrive at the original atom size
                size_t increments = ((atom_moov_size / 16) + (16 - 1)) & ~(16 - 1);
                atom_moov_compressed_expected_size = increments * 3;
                off_t total_offset_change = atom_moov_compressed_expected_size;
                bool can_store_atoms = false;
                
                // add an edit for our estimated size
                qtf_edit_list_add_edit(edit_list, atom_ftyp_size, atom_moov_compressed_expected_size);
                // apply all the edits to date
                result = qtf_offsets_apply_list(atom_moov, atom_moov_size, edit_list);
                                
                do {
                    // This is skipped the first pass, then expands the space we reserve on subsequent passes
                    if (result == qtf_result_ok && atom_moov_compressed_actual_size > (atom_moov_compressed_expected_size - 8))
                    {
                        atom_moov_compressed_expected_size += increments;
                        total_offset_change += increments;
                        
                        result = qtf_offsets_modify(atom_moov, atom_moov_size, increments);
                    }
                    
                    if (result == qtf_result_ok)
                    {
                        atom_moov_compressed_actual_size = qtf_compress_movie_atom(atom_moov, atom_moov_size,
                                                                                   atom_moov_compressed, atom_moov_compressed_size,
                                                                                   false, true, false);
                    }
                    if (result == qtf_result_ok
                        &&
                        (atom_moov_compressed_actual_size == atom_moov_compressed_expected_size
                         || atom_moov_compressed_actual_size < (atom_moov_compressed_expected_size - 8))
                        )
                    {
                        // atom_moov_compressed_actual_size will be zero if we couldn't compress it in the buffer space
                        // or if an error occurred in compression, so we use the uncompressed atom in those cases
                        if (atom_moov_compressed_actual_size != 0)
                        {
                            // we substitute the existing atom_moov with the an "atom" which is usually two atoms:
                            // the compressed moov atom plus a free atom for the extra space we estimated when
                            // calculating the offset
                            free(atom_moov);
                            atom_moov = atom_moov_compressed;
                            atom_moov_size = atom_moov_compressed_expected_size; // The total size we'll write to the file
                            atom_moov_compressed = NULL;
                            size_t free_size = atom_moov_compressed_expected_size - atom_moov_compressed_actual_size;
                            if (free_size > 0 && free_size < 8)
                            {
                                result = qtf_result_memory_error; // this shouldn't happen but it's fatal so check
                            }
                            else
                            {
                                uint32_t *size = atom_moov + atom_moov_compressed_actual_size;
                                uint32_t *type = atom_moov + atom_moov_compressed_actual_size + 4;
                                *size = qtf_swap_host_to_big_int_32(free_size);
                                *type = qtf_swap_host_to_big_int_32(QTF_FCC_free);
                            }
                            
                        }
                        else
                        {
                            // we failed to compress the atom, set the offsets for the uncompressed atom size
                            result = qtf_offsets_modify(atom_moov, atom_moov_size, (ssize_t)atom_moov_size - (ssize_t)total_offset_change);
                        }
                        can_store_atoms = true;
                    }
                } while (result == qtf_result_ok && can_store_atoms == false);
            }
        }
        // we've swapped it into atom_moov by now if things went well, and atom_moov_compressed will be NULL
        free(atom_moov_compressed);
    }
    else
    {
        if (result == qtf_result_ok)
        {
            // add the movie back in its new position
            qtf_edit_list_add_edit(edit_list, atom_ftyp_size, atom_moov_size);
            // update the moov atom with the new offsets
            result = qtf_offsets_apply_list(atom_moov, atom_moov_size, edit_list);
        }
    }
    // We're finished with the edit_list now
    qtf_edit_list_destroy(edit_list);
    edit_list = NULL;
    
    int fd_dest = 0;
    
    if (result == qtf_result_ok)
    {
#if defined(_WIN32)
        fd_dest = _open(dst_path, _O_WRONLY | _O_CREAT | _O_EXCL | _O_BINARY, _S_IREAD | _S_IWRITE);
#else
        fd_dest = open(dst_path, O_WRONLY | O_CREAT | O_EXCL,
                       S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH); // RW owner, R group, R others
#endif

        if (fd_dest == -1)
        {
            result = qtf_result_file_write_error;
        }
    }
    if (result == qtf_result_ok)
    {
        // Write the ftyp atom if there was one
        if (atom_ftyp != NULL)
        {
            result = qtf_write(fd_dest, atom_ftyp, atom_ftyp_size);
        }
        // Write the moov atom
        if (result == qtf_result_ok)
        {
            result = qtf_write(fd_dest, atom_moov, atom_moov_size);
        }
        if (result == qtf_result_ok)
        {
            // skip over the ftyp atom if present
            off_t source_offset = lseek(fd_source, atom_ftyp_size, SEEK_SET);
            if (source_offset == -1)
            {
                result = qtf_result_file_read_error;
            }
            // Copy everything except the moov atom(s) and any free skip or wide atoms
            char buffer[QTF_COPY_BUFFER_SIZE];
            
            while (result == qtf_result_ok) {
                
                uint32_t type;
                uint64_t size;
                size_t bytes_read;
                result = qtf_read_atom_header(fd_source, buffer, QTF_COPY_BUFFER_SIZE, &type, &size, &bytes_read);
                                
                if (result != 0 || bytes_read == 0) break;
                
                bool skip;
                
                switch (type) {
                    case QTF_FCC_moov:
                    case QTF_FCC_free:
                    case QTF_FCC_skip:
                    case QTF_FCC_wide:
                        skip = true;
                        break;
                    default:
                        skip = false;
                        break;
                }
                if (skip)
                {
                    // Skip these atoms
                    off_t offset = lseek(fd_source, size - bytes_read, SEEK_CUR);
                    if (offset == -1)
                    {
                        result = qtf_result_file_read_error;
                    }
                    else
                    {
                        bytes_read += (size - bytes_read);
                    }
                }
                else
                {
                    // Write all other atoms to the new file
                    result = qtf_write(fd_dest, buffer, bytes_read);
                    if (result == qtf_result_ok)
                    {
                        size -= bytes_read;
                        
                        while (result == qtf_result_ok && size > 0)
                        {
                            size_t to_copy = MIN(size, QTF_COPY_BUFFER_SIZE);
                            result = qtf_read(fd_source, buffer, to_copy);
                            bytes_read += to_copy;
                            
                            if (result == qtf_result_ok)
                            {
                                result = qtf_write(fd_dest, buffer, to_copy);
                                if (result == qtf_result_ok)
                                {
                                    size -= to_copy;
                                }
                            }
                        }
                    }
                } // skip
                source_offset += bytes_read;
            } // while
        }
    }
    free(atom_moov);
    free(atom_ftyp);
    if (fd_source) close(fd_source);
    if (fd_dest) close(fd_dest);
    return result;
}

qtf_result qtf_flatten_movie_in_place(const char *src_path, bool allow_compressed_moov_atom)
{
    qtf_result result = qtf_result_ok;
#if defined(_WIN32)
    int fd = _open(src_path, _O_RDWR | _O_BINARY);
#else
    int fd = open(src_path, O_RDWR);
#endif

    if (fd == -1)
    {
        return qtf_result_file_read_error;
    }
    
    if (fd > 0)
    {
        size_t file_length = 0;
        result = qtf_get_file_size(fd, &file_length);
        if (result != qtf_result_ok) return result;
        
        off_t free_start = 0;
        off_t moov_start = 0;
        size_t free_size = 0;
        size_t moov_size = 0;
        off_t offset = 0;
        while (result == qtf_result_ok && (moov_size == 0 || free_size == 0))
        {
            uint32_t atom_header[4];
            uint64_t size = 0;
            uint32_t type = 0;
            size_t bytes_read;
            
            result = qtf_read_atom_header(fd, atom_header, sizeof(atom_header), &type, &size, &bytes_read);
            
            if (result != qtf_result_ok || bytes_read == 0) break;
            
            if (type == QTF_FCC_free && free_size == 0) // use only the first free atom
            {
                free_start = offset;
                free_size = size;
            }
            else if (type == QTF_FCC_wide && offset == (free_start + free_size))
            {
                // we can swallow up a wide atom if it immediately follows the free atom
                free_size += size;
            }
            else if (type == QTF_FCC_moov)
            {
                moov_start = offset;
                moov_size = size;
            }
            offset += size;
            lseek(fd, size - bytes_read, SEEK_CUR);
        }
        
        // If there is a free atom before the moov atom
        if (result == qtf_result_ok
            && (free_start < moov_start)
            && (free_size > 8)
            && (moov_size > 8))
        {
            bool moov_was_at_end = ((moov_start + moov_size) == file_length) ? true : false;
            void *moov = malloc(moov_size);
            if (!moov)
            {
                result = qtf_result_memory_error;
            }
            if (result == qtf_result_ok)
            {
                size_t got = 0;
                got = lseek(fd, moov_start, SEEK_SET);
                if (got == -1) result = qtf_result_file_read_error;
                if (result == qtf_result_ok)
                {
                    result = qtf_read(fd, moov, moov_size);
                }
                if (result == qtf_result_ok && allow_compressed_moov_atom && free_size < (moov_size + 8) && (free_size != moov_size) && (free_size > 40))
                {
                    void *compressed = malloc(free_size);
                    if (compressed)
                    {
                        size_t compressed_size = qtf_compress_movie_atom(moov,
                                                                         moov_size,
                                                                         compressed,
                                                                         free_size,
                                                                         true, true, true); // use the fastest method that will fit
                        if (compressed_size != 0)
                        {
                            // swap our compressed movie atom for the original
                            free(moov);
                            moov = compressed;
                            moov_size = compressed_size;
                        }
                        else
                        {
                            free(compressed);
                        }
                    }
                }
                // If the moov atom can either replace the free atom entirely
                // or there is space to insert the moov atom and a new free atom (minimum 8 bytes)
                if ((free_size >= (moov_size + 8)) || (free_size == moov_size))
                {
                    if (result == qtf_result_ok)
                    {
                        got = lseek(fd, free_start, SEEK_SET);
                        if (got == -1) result = qtf_result_file_read_error;
                    }
                    if (result == qtf_result_ok)
                    {
                        result = qtf_write(fd, moov, moov_size);
                    }
                    // add a new smaller free after the moov if necessary
                    if (result == qtf_result_ok && moov_size < free_size)
                    {
                        uint32_t new_free_size = qtf_swap_host_to_big_int_32(free_size - moov_size);
                        uint32_t new_free_type = qtf_swap_host_to_big_int_32(QTF_FCC_free);
                        result = qtf_write(fd, &new_free_size, 4);
                        if (result == qtf_result_ok)
                        {
                            result = qtf_write(fd, &new_free_type, 4);
                        }
                    }
                    if (result == qtf_result_ok)
                    {
                        // If the old moov atom was at the end of the file, truncate the file
                        // otherwise turn the atom into a free atom
                        if (moov_was_at_end)
                        {
                            ftruncate(fd, moov_start);
                        }
                        else
                        {                            
                            got = lseek(fd, moov_start + 4, SEEK_SET);
                            if (got == -1) result = qtf_result_file_read_error;
                            if (result == qtf_result_ok)
                            {
                                uint32_t new_free_type = qtf_swap_host_to_big_int_32(QTF_FCC_free);
                                result = qtf_write(fd, &new_free_type, 4);
                            }
                        }
                    }
                }
                else
                {
                    result = qtf_result_file_no_free_space;
                }
                free(moov);
            } // end if (moov)
        }
        else
        {
            result = qtf_result_file_no_free_space;
        }
        close(fd);
    }
    return result;
}
