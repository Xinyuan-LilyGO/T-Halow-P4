# Usage: cmake -DIN=<file> -DOUT=<file>.gz -P gzip_file.cmake
# Pure CMake so the build has no host gzip dependency.
file(ARCHIVE_CREATE OUTPUT "${OUT}" PATHS "${IN}" FORMAT raw COMPRESSION GZip COMPRESSION_LEVEL 9)
