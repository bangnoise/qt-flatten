qt-flatten
==========

Code to flatten QuickTime and MPEG-4 movie files with a command-line tool by way of example.

qt-flatten is functionally very similar to [qt-faststart](http://multimedia.cx/eggs/improving-qt-faststart/) but adds a few features such as support for compressed moov atoms and the ability to strip free space. Currently the code supports MacOS X, Linux and Windows.

A function to flatten a movie in-place by moving the moov atom into previously reserved free space is also included, which for large files works much faster than rewriting the entire file but requires you reserve the free space when creating the original file.

Build Requirements
------------------

*   A compiler with C99 support, such as Clang or gcc.
*   zlib and zlib.h. MacOS provides it, on Ubuntu install the zlib1g-dev package, on Windows with MinGW do *mingw-get install libz*.

Build
-----

    cc -o qt-flatten -lz main.c qt_flatten.c

or for GCC

    cc -o qt-flatten -std=gnu99 main.c qt_flatten.c -lz
