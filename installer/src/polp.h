// PlayOnline update server client for the installer (polp.c).
#pragma once

#include <SDL3/SDL.h>
#include <stddef.h>
#include <stdint.h>

// Newest PlayOnline build installed: 20150901_X adds the "goodbye" picture that ends the Viewer.
#define POLP_CAP "20110726_A"

typedef struct polp_file {
    char *path;          // in the PlayOnline folder, '/' separated as the server lists it
    char *blob;          // <version>/Direct/<path>.slc
    long size, blob_size;
    char version[16];
} polp_file;

// The server's file list, each file at its newest version up to `cap` (POLP_CAP, or an older PlayOnline such as
// 20100601_1). Returns the count or -1 (error filled).
int polp_catalogue(const char *cap, polp_file **out, char *error, size_t error_size);
void polp_files_free(polp_file *files, int count);

// One file, decoded (malloc'd), or nullptr (error filled).
unsigned char *polp_download(const polp_file *f, size_t *len, char *error, size_t error_size);
