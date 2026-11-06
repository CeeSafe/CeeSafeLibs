/* files_shim.c: the POSIX file calls the CeeSafe `diskio` package needs and lib/std does not offer: positioned reads,
 * fsync, rename, mkdir -p, directory sync and exclusive creation.
 *
 * Paths come as (pointer, length) and are copied into a NUL-terminated buffer; a path longer than PATH_MAX or holding
 * a NUL is refused with -ENAMETOOLONG or -EINVAL. Errors are returned as negative errno values. Every call may block
 * on the disk, so async code makes them through Async.blocking.
 */
/* PATH_MAX and O_CLOEXEC: glibc declares them only for POSIX.1-2008 (macOS always does). */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* Prototypes, for -Wmissing-prototypes: the package compiles this file alone, without a header path. */
int64_t csf_open(const uint8_t *path, size_t len, int32_t mode);
void csf_close(int64_t fd);
int64_t csf_pread(int64_t fd, uint8_t *out, size_t cap, uint64_t offset);
int64_t csf_write_all(int64_t fd, const uint8_t *bytes, size_t len);
int32_t csf_sync(int64_t fd);
int64_t csf_size(int64_t fd);
int32_t csf_rename(const uint8_t *from, size_t from_len, const uint8_t *to, size_t to_len);
int32_t csf_remove(const uint8_t *path, size_t len);
int32_t csf_exists(const uint8_t *path, size_t len);
int32_t csf_make_dirs(const uint8_t *path, size_t len);
int64_t csf_modified(const uint8_t *path, size_t len);
int32_t csf_sync_dir(const uint8_t *path, size_t len);
int32_t csf_errno_not_found(void);
int32_t csf_errno_exists(void);
int32_t csf_errno_denied(void);
int32_t csf_errno_no_space(void);

#define CSF_READ 0
#define CSF_CREATE_NEW 1
#define CSF_CREATE 2
#define CSF_APPEND 3

static int path_of(const uint8_t *path, size_t len, char out[PATH_MAX]) {
    if (len >= PATH_MAX) return -ENAMETOOLONG;
    if (len == 0 || memchr(path, 0, len) != NULL) return -EINVAL;
    memcpy(out, path, len);
    out[len] = 0;
    return 0;
}

int64_t csf_open(const uint8_t *path, size_t len, int32_t mode) {
    char p[PATH_MAX];
    int r = path_of(path, len, p);
    if (r < 0) return r;
    int flags = O_CLOEXEC;
    switch (mode) {
    case CSF_READ: flags |= O_RDONLY; break;
    case CSF_CREATE_NEW: flags |= O_RDWR | O_CREAT | O_EXCL; break;
    case CSF_CREATE: flags |= O_RDWR | O_CREAT | O_TRUNC; break;
    case CSF_APPEND: flags |= O_WRONLY | O_CREAT | O_APPEND; break;
    default: return -EINVAL;
    }
    int fd;
    do {
        fd = open(p, flags, 0640);
    } while (fd < 0 && errno == EINTR);
    return fd < 0 ? -errno : fd;
}

void csf_close(int64_t fd) {
    if (fd >= 0) close((int)fd);
}

int64_t csf_pread(int64_t fd, uint8_t *out, size_t cap, uint64_t offset) {
    ssize_t n;
    do {
        n = pread((int)fd, out, cap, (off_t)offset);
    } while (n < 0 && errno == EINTR);
    return n < 0 ? -errno : n;
}

int64_t csf_write_all(int64_t fd, const uint8_t *bytes, size_t len) {
    while (len > 0) {
        ssize_t n = write((int)fd, bytes, len);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -errno;
        }
        bytes += n;
        len -= (size_t)n;
    }
    return 0;
}

int32_t csf_sync(int64_t fd) {
    int r;
    do {
        r = fsync((int)fd);
    } while (r < 0 && errno == EINTR);
    return r < 0 ? -errno : 0;
}

int64_t csf_size(int64_t fd) {
    struct stat st;
    if (fstat((int)fd, &st) < 0) return -errno;
    return st.st_size;
}

int32_t csf_rename(const uint8_t *from, size_t from_len, const uint8_t *to, size_t to_len) {
    char a[PATH_MAX], b[PATH_MAX];
    int r = path_of(from, from_len, a);
    if (r < 0) return r;
    r = path_of(to, to_len, b);
    if (r < 0) return r;
    return rename(a, b) < 0 ? -errno : 0;
}

int32_t csf_remove(const uint8_t *path, size_t len) {
    char p[PATH_MAX];
    int r = path_of(path, len, p);
    if (r < 0) return r;
    return unlink(p) < 0 ? -errno : 0;
}

/* 1 if the path exists (as anything), 0 if not, else -errno. */
int32_t csf_exists(const uint8_t *path, size_t len) {
    char p[PATH_MAX];
    int r = path_of(path, len, p);
    if (r < 0) return r;
    struct stat st;
    if (stat(p, &st) == 0) return 1;
    return errno == ENOENT ? 0 : -errno;
}

/* mkdir -p: creates every missing directory of the path. */
int32_t csf_make_dirs(const uint8_t *path, size_t len) {
    char p[PATH_MAX];
    int r = path_of(path, len, p);
    if (r < 0) return r;
    for (size_t i = 1; i <= len; i++) {
        if (i == len || p[i] == '/') {
            char saved = p[i];
            p[i] = 0;
            if (mkdir(p, 0750) < 0 && errno != EEXIST) {
                return -errno;
            }
            p[i] = saved;
        }
    }
    struct stat st;
    if (stat(p, &st) < 0) return -errno;
    return S_ISDIR(st.st_mode) ? 0 : -ENOTDIR;
}

/* The last modification time, in Unix seconds, or -errno. */
int64_t csf_modified(const uint8_t *path, size_t len) {
    char p[PATH_MAX];
    int r = path_of(path, len, p);
    if (r < 0) return r;
    struct stat st;
    if (stat(p, &st) < 0) return -errno;
    return (int64_t)st.st_mtime;
}

/* fsync of a directory, so that a rename or creation in it survives a crash. */
int32_t csf_sync_dir(const uint8_t *path, size_t len) {
    char p[PATH_MAX];
    int r = path_of(path, len, p);
    if (r < 0) return r;
    int fd = open(p, O_RDONLY | O_CLOEXEC
#ifdef O_DIRECTORY
                         | O_DIRECTORY
#endif
    );
    if (fd < 0) return -errno;
    r = fsync(fd);
    int e = errno;
    close(fd);
    return r < 0 ? -e : 0;
}

int32_t csf_errno_not_found(void) { return -ENOENT; }
int32_t csf_errno_exists(void) { return -EEXIST; }
int32_t csf_errno_denied(void) { return -EACCES; }
int32_t csf_errno_no_space(void) { return -ENOSPC; }
