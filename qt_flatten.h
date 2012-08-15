//
//  qt_flatten.h
//
//  Created by Tom Butterworth on 08/05/2012.
//  Copyright (c) 2012 Tom Butterworth. All rights reserved.
//

#ifndef qt_flatten_h
#define qt_flatten_h

#include <stdbool.h>

typedef enum qtf_result {
    qtf_result_ok = 0,
    qtf_result_file_no_free_space = 1, // there is not enough free space in the file
    qtf_result_file_too_complex = 2, // a valid file is beyond our abilities for some reason
    qtf_result_file_not_movie = 3, // file is not a valid movie
    qtf_result_file_read_error = 4, // file system error
    qtf_result_file_write_error = 5, // file system error
    qtf_result_memory_error = 6 // couldn't allocate sufficient memory
} qtf_result;

/**
 Attempts to flatten a QuickTime movie file in-place by moving the moov atom from the end of the file
 into free space at the start of the file. This requires the original file be created with a suitably-sized
 free atom at the start:
 
    [ftyp (optional)][free][wide (optional)][mdat][moov]
 
 This function is likely only going to be useful if you have programatically created the movie file yourself.
 
 If allow_compressed_moov_atom is true the moov atom may be compressed if doing so is necessary to fit it in
 the available free space.
 
 Returns syc_result_ok on success, syc_result_file_no_free_space if there isn't a sufficiently large free atom
 preceding the movie data, or an error.
 */
qtf_result qtf_flatten_movie_in_place(const char *src_path, bool allow_compressed_moov_atom);

/**
 Writes a flattened version of the QuickTime movie file at src_path to dst_path.
 
 If allow_compressed_moov_atom is true the moov atom will be compressed.
 
 Returns syc_result_ok on success, or an error.
 */
qtf_result qtf_flatten_movie(const char *src_path, const char *dst_path, bool allow_compressed_moov_atom);

#endif
