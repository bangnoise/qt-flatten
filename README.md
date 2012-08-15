qt-flatten
==========

Code to flatten QuickTime movie files with a command-line tool by way of example.

qt-flatten is functionally very similar to [qt-faststart](http://multimedia.cx/eggs/improving-qt-faststart/) but adds a few features such as support for compressed moov atoms and the ability to strip free space. Currently the code supports MacOS X and Linux.

A function to flatten a movie in-place by moving the moov atom into previously reserved free space is also included, which for large files works much faster than rewriting the entire file but requires you reserve the free space when creating the original file.

Build:

    cc -o qt-flatten -lz main.c qt_flatten.c

Requires a compiler with C99 support - with GCC use

    cc -o qt-flatten -std=gnu99 -lz main.c qt_flatten.c
    
Requires zlib and zlib.h - on Ubuntu install zlib1g-dev.
