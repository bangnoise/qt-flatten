//
//  main.c
//  qt-flatten
//
//  Created by Tom Butterworth on 14/08/2012.
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

#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>

#if defined(_WIN32)
#include <Windows.h>
#include <sys/stat.h>
#endif

#include "qt_flatten.h"

#define temp_file_suffix ".temp"

int main(int argc, const char * argv[])
{
    int return_value = EXIT_SUCCESS;
    
    bool allow_compressed_moov_atoms = false;
    
    // Process arguments
    int next_arg = 1;
    if (next_arg < argc)
    {
    	if (strcmp(argv[next_arg], "-c") == 0)
    	{
    		allow_compressed_moov_atoms = true;
    		next_arg++;
    	}
    }
    
    // Get input and output paths
    const char *input_file = NULL;
    const char *output_file = NULL;
    
    if (next_arg < argc)
    {
        input_file = argv[next_arg];
    }
    
    next_arg++;
    
    if (next_arg < argc)
    {
        output_file = argv[next_arg];
    }
    
    if (!input_file)
    {
        return_value = EXIT_FAILURE;
    }
    
    // If we had bad arguments, print our usage
    if (return_value != EXIT_SUCCESS)
    {
    	const char *prog_name;
#if defined(__APPLE__)
    	prog_name = getprogname();
#elif defined(__linux__)
    	extern const char *program_invocation_short_name;
    	prog_name = program_invocation_short_name;
#elif defined(_WIN32)
		TCHAR full_path[MAX_PATH];

		if(GetModuleFileName(NULL, full_path, MAX_PATH))
		{
			prog_name = strrchr(full_path, '\\') + 1;
		}
		else
		{
			prog_name = "COMMAND";
		}
#else
#error add a way to discover the program name on your platform here
#endif
        fprintf(stderr, "usage: %s [-c] INPUT [OUTPUT] \n", prog_name);
    }
    else
    {
        // If output_file is the same as input_file we treat it as if it isn't present
        if (output_file && strcmp(output_file, input_file) == 0)
        {
            output_file = NULL;
        }
        
        // If we are to replace the input, first try doing the flatten in-place
        if (output_file == NULL)
        {
            qtf_result result = qtf_flatten_movie_in_place(input_file, allow_compressed_moov_atoms);
            if (result == qtf_result_ok) return EXIT_SUCCESS;
            // Ignore any other error here, we'll take a stab with syc_flatten_movie()
        }
        
        // We can't flatten the file in-place, continue to flatten to a new file
        if (output_file == NULL)
        {
            output_file = input_file;
        }
        else
        {
            // We attempt to create a new file to check no such file already exists
            // (because rename() will brutally overwrite any existing file (except on Windows))
#if defined(_WIN32)
			int out_fd = _open(output_file, _O_WRONLY | _O_CREAT | _O_EXCL, _S_IREAD | _S_IWRITE);
			if (out_fd != -1)
			{
				_close(out_fd);
			}
#else
			int out_fd = open(output_file, O_WRONLY | O_CREAT | O_EXCL,
                              S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH); // RW owner, R group, R others
            if (out_fd != -1)
            {
                // We created the file, we can close it now
                close(out_fd);
            }
#endif
            else
            {
                fprintf(stderr, "Error: ");
                switch (errno) {
                    case EEXIST:
                        fprintf(stderr, "The output file already exists");
                        break;
                    default:
                        fprintf(stderr, "The output file could not be created");
                        break;
                }
                fprintf(stderr, ".\n");
                return_value = EXIT_FAILURE;
            }
        }
        
        if (return_value == EXIT_SUCCESS)
        {
            // We flatten to a temporary file which we then move - this accomodates replacing the original file
            unsigned long temp_file_path_buffer_length = strlen(output_file) + strlen(temp_file_suffix) + 1;
            
            char *temp_file_path = (char *)malloc(temp_file_path_buffer_length);
            
			if (temp_file_path == NULL)
			{
				fprintf(stderr, "Error: Not enough memory was available.\n");
				return_value = EXIT_FAILURE;
			}
			else
			{
				qtf_result result = qtf_result_ok;
				snprintf(temp_file_path, temp_file_path_buffer_length, "%s%s", output_file, temp_file_suffix);

				result = qtf_flatten_movie(input_file, temp_file_path, allow_compressed_moov_atoms);

				if (result != qtf_result_ok)
				{
					fprintf(stderr, "Error: ");
					switch (result) {
					case qtf_result_file_not_movie:
						fprintf(stderr, "The file was not recognised as a QuickTime movie");
						break;
					case qtf_result_file_read_error:
						fprintf(stderr, "The file could not be read");
						break;
					case qtf_result_file_too_complex:
						fprintf(stderr, "This type of movie file is not supported");
						break;
					case qtf_result_file_write_error:
						fprintf(stderr, "The file could not be written");
						break;
					case qtf_result_memory_error:
						fprintf(stderr, "Not enough memory was available");
						break;
					default:
						fprintf(stderr, "An unexpected error occurred");
						break;
					}
					fprintf(stderr, ".\n");
					return_value = EXIT_FAILURE;
				}

				if (return_value == EXIT_SUCCESS)
				{
#if defined(_WIN32)
					// On Windows, rename() fails if the file already exists
					remove(output_file);
#endif
					rename(temp_file_path, output_file);
				}

				free(temp_file_path);
			}
        }
    }

    return return_value;
}

