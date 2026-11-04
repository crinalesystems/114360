// A small FTP client for the installer (ftp.c): one control connection, passive mode, binary transfers streamed
// through a callback. Written against what the Xbox 360's FTP servers (Aurora, DashLaunch) answer: LIST in the
// Unix "ls -l" form, MKD, STOR, RETR, RNFR/RNTO, DELE, RMD. Paths are absolute, '/' separated ("/Hdd1/114360").
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct ftp ftp;

typedef struct ftp_entry {
    char *name;
    uint64_t size;
    bool dir;
} ftp_entry;

// Connect to host:port and sign in (TYPE I). nullptr on failure, with `error` filled.
ftp *ftp_open(const char *host, int port, const char *user, const char *password, char *error, size_t error_size);
void ftp_close(ftp *f);

// Sign in again on a new control connection after a failure (the console's server drops connections now and
// then). False with `error` filled.
bool ftp_reopen(ftp *f, char *error, size_t error_size);

// The entries of `folder`: count (0 when it is empty or does not exist), or -1 on a connection failure.
int ftp_list(ftp *f, const char *folder, ftp_entry **out, char *error, size_t error_size);
void ftp_entries_free(ftp_entry *entries, int count);

// Make `folder` (one level; an "already there" refusal counts as success). False on a connection failure.
bool ftp_mkdir(ftp *f, const char *folder, char *error, size_t error_size);

// Store `size` bytes at `path`, pulled from `read` (which fills `buf` with exactly `want` bytes or fails).
typedef bool (*ftp_reader)(void *ud, void *buf, size_t want);
bool ftp_store(ftp *f, const char *path, uint64_t size, ftp_reader read, void *ud, char *error, size_t error_size);

// A whole (small) file, malloc'd, or nullptr (`missing` set when the server refused it, e.g. no such file).
unsigned char *ftp_fetch(ftp *f, const char *path, size_t *len, bool *missing, char *error, size_t error_size);

bool ftp_rename(ftp *f, const char *from, const char *to, char *error, size_t error_size);
bool ftp_delete(ftp *f, const char *path, char *error, size_t error_size);
bool ftp_rmdir(ftp *f, const char *path, char *error, size_t error_size);
