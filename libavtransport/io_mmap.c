/*
 * Copyright © 2024, Lynne
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#define _GNU_SOURCE

#include "os_compat.h"

#include <stdio.h>
#include <string.h>
#include <uchar.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/uio.h>
#include <limits.h>
#include <sys/mman.h>

#include "io_common.h"
#include "io_utils.h"
#include "utils_internal.h"

/* A reasonable default */
#define MIN_ALLOC 1024*1024

struct AVTIOCtx {
    int fd;
    AVTBuffer *map;

    avt_pos rpos;
    avt_pos wpos;

    bool file_grew;
};

static void mmap_buffer_free(void *opaque, void *base_data, size_t size)
{
    munmap(base_data, size);
    close((int)((intptr_t)opaque));
}

static COLD int mmap_close(AVTIOCtx **_io)
{
    AVTIOCtx *io = *_io;
    avt_buffer_unref(&io->map);
    if (io->file_grew)
        ftruncate(io->fd, io->wpos);
    close(io->fd);
    free(io);
    *_io = NULL;
    return 0;
}

static COLD int mmap_init_common(AVTContext *ctx, AVTIOCtx *io)
{
    int ret;

    size_t len = lseek(io->fd, SEEK_END, 0);
    if (!len) {
        len = MIN_ALLOC;
        if (fallocate(io->fd, 0, 0, len)) {
            ret = avt_handle_errno(io, "Error in fallocate(): %i %s\n");
            return ret;
        }
        io->file_grew = 1;
    }

    int fd_dup = dup(io->fd);
    if (fd_dup < 0)
        return avt_handle_errno(io, "Error in dup(): %i %s\n");

    void *data = mmap(NULL, len, PROT_READ | PROT_WRITE,
                      MAP_SHARED |
                      MAP_POPULATE |
                      MAP_NONBLOCK,
                      fd_dup, 0);

    if (!data) {
        ret = avt_handle_errno(io, "Error in mmap(): %i %s\n");
        close(fd_dup);
        return ret;
    }

    io->map = avt_buffer_create(data, len,
                                (void *)((intptr_t)fd_dup),
                                mmap_buffer_free);
    if (!io->map) {
        munmap(data, len);
        close(fd_dup);
        return AVT_ERROR(ENOMEM);
    }

    return 0;
}

static COLD int mmap_init(AVTContext *ctx, AVTIOCtx **_io, AVTAddress *addr)
{
    int ret;
    AVTIOCtx *io = calloc(1, sizeof(*io));
    if (!io)
        return AVT_ERROR(ENOMEM);

    io->fd = dup(addr->fd);
    if (io->fd < 0) {
        ret = avt_handle_errno(io, "Error duplicating fd: %i %s\n");
        free(io);
        return ret;
    }

    ret = mmap_init_common(ctx, io);
    if (ret < 0) {
        close(io->fd);
        free(io);
        return ret;
    }

    *_io = io;

    return 0;
}

static COLD int mmap_init_path(AVTContext *ctx, AVTIOCtx **_io, AVTAddress *addr)
{
    int ret;
    AVTIOCtx *io = calloc(1, sizeof(*io));
    if (!io)
        return AVT_ERROR(ENOMEM);

    io->fd = open(addr->path, O_CREAT | O_RDWR | O_CLOEXEC, 0666);
    if (io->fd < 0) {
        ret = avt_handle_errno(io, "Error opening: %i %s\n");
        free(io);
        return ret;
    }

    ret = mmap_init_common(ctx, io);
    if (ret < 0) {
        close(io->fd);
        free(io);
        return ret;
    }

    *_io = io;

    return 0;
}

static int mmap_grow(AVTIOCtx *io, size_t amount)
{
    int ret;
    size_t old_map_size;
    uint8_t *old_map = avt_buffer_get_data(io->map, &old_map_size);
    void *new_map = MAP_FAILED;

    amount = AVT_MAX(amount, MIN_ALLOC);
    size_t new_map_size = old_map_size + amount;

    /* Grow file */
    if (fallocate(io->fd, 0, 0, new_map_size)) {
        ret = avt_handle_errno(io, "Error in fallocate(): %i %s\n");
        return ret;
    }

    /* Trim the file at the end, stripping it of padding, if it was grown */
    io->file_grew = true;

#if 1 /* TODO: detect */
    /* First, attempt to remap the old mapping, and retaining its address.
     * If there's only a single reference to the map (ours), allow it to be
     * moved. */
    new_map = mremap(old_map, old_map_size, new_map_size,
                     avt_buffer_get_refcount(io->map) == 1 ? MREMAP_MAYMOVE :
                                                             0);

    /* Success */
    if (new_map != MAP_FAILED) {
        /* Update the existing buffer */
        avt_buffer_update(io->map, new_map, new_map_size);
        return 0;
    } else if (new_map == MAP_FAILED && errno != ENOMEM) {
        ret = avt_handle_errno(io, "Error in mremap(): %i %s\n");
        return ret;
    }
#endif

    /* Recreate the mapping */
    int fd_dup = dup(io->fd);
    if (fd_dup < 0)
        return avt_handle_errno(io, "Error in dup(): %i %s\n");

    if (fcntl(fd_dup, F_SETFD, FD_CLOEXEC) == -1) {
        ret = avt_handle_errno(io, "Error in fcntl(F_SETFD, FD_CLOEXEC): %i %s\n");
        close(fd_dup);
        return ret;
    }

    void *data = mmap(NULL, new_map_size, PROT_READ | PROT_WRITE,
                      MAP_SHARED |
                      MAP_POPULATE,
                      fd_dup, 0);
    if (data == MAP_FAILED) {
        ret = avt_handle_errno(io, "Error in mmap(): %i %s\n");
        close(fd_dup);
        return ret;
    }

    AVTBuffer *new_buf = avt_buffer_create(data, new_map_size,
                                           (void *)((intptr_t)fd_dup),
                                           mmap_buffer_free);
    if (!io->map) {
        munmap(data, new_map_size);
        close(fd_dup);
        return AVT_ERROR(ENOMEM);
    }

    avt_buffer_unref(&io->map);
    io->map = new_buf;

    return 0;
}

static int mmap_max_pkt_len(AVTIOCtx *io, size_t *mtu)
{
    *mtu = SIZE_MAX;
    return 0;
}

static inline avt_pos mmap_seek(AVTIOCtx *io, avt_pos pos)
{
    if (pos > avt_buffer_get_data_len(io->map))
        return AVT_ERROR(ERANGE);
    return (io->rpos = pos);
}

static avt_pos mmap_write_pkt(AVTIOCtx *io, AVTPktd *p, int64_t timeout)
{
    size_t pl_len;
    uint8_t *pl_data = avt_buffer_get_data(&p->pl, &pl_len);

    size_t map_size;
    uint8_t *map_data = avt_buffer_get_data(io->map, &map_size);

    if ((io->wpos + p->hdr_len + pl_len) > map_size) {
        int ret = mmap_grow(io, p->hdr_len + pl_len);
        if (ret < 0)
            return ret;
    }

    map_data = avt_buffer_get_data(io->map, &map_size);
    memcpy(&map_data[io->wpos], p->hdr, p->hdr_len);
    memcpy(&map_data[io->wpos + p->hdr_len], pl_data, pl_len);

    avt_pos offset = io->wpos + p->hdr_len + pl_len;
    AVT_SWAP(io->wpos, offset);
    return offset;
}

static avt_pos mmap_write_vec(AVTIOCtx *io, AVTPktd *iov, uint32_t nb_iov,
                              int64_t timeout)
{
    uint8_t *pl_data;
    size_t pl_len, sum = 0;
    for (int i = 0; i < nb_iov; i++)
        sum += iov[i].hdr_len + avt_buffer_get_data_len(&iov[i].pl);

    size_t map_size;
    uint8_t *map_data = avt_buffer_get_data(io->map, &map_size);

    if ((io->wpos + sum) > map_size) {
        int ret = mmap_grow(io, sum);
        if (ret < 0)
            return ret;
    }

    avt_pos offset = io->wpos;
    map_data = avt_buffer_get_data(io->map, &map_size);
    for (int i = 0; i < nb_iov; i++) {
        pl_data = avt_buffer_get_data(&iov[i].pl, &pl_len);
        memcpy(&map_data[offset], iov[i].hdr, iov[i].hdr_len);
        memcpy(&map_data[offset + iov[i].hdr_len], pl_data, pl_len);
        offset += iov[i].hdr_len + pl_len;
    }

    AVT_SWAP(io->wpos, offset);
    return offset;
}

static avt_pos mmap_rewrite(AVTIOCtx *io, AVTPktd *p, avt_pos off,
                            int64_t timeout)
{
    size_t pl_len;
    uint8_t *pl_data = avt_buffer_get_data(&p->pl, &pl_len);

    size_t map_size;
    uint8_t *map_data = avt_buffer_get_data(io->map, &map_size);

    if ((off + p->hdr_len + pl_len) > map_size)
        return AVT_ERROR(ERANGE);

    memcpy(&map_data[off], p->hdr, p->hdr_len);
    memcpy(&map_data[off + p->hdr_len], pl_data, pl_len);

    return off;
}

static inline int64_t mmap_read(AVTIOCtx *io, AVTBuffer *dst,
                                size_t len, int64_t timeout,
                                enum AVTIOReadFlags flags)
{
    size_t map_size;
    uint8_t *map_data = avt_buffer_get_data(io->map, &map_size);

    /* TODO: cap map_size at wpos? */
    len = AVT_MIN(map_size - io->rpos, len);

    if (flags & AVT_IO_READ_MUTABLE) {
        dst = avt_buffer_get_data(dst, NULL);
        memcpy(dst, &map_data[io->rpos], len);
    } else {
        avt_buffer_quick_unref(dst);
        avt_buffer_quick_ref(dst, io->map, io->rpos, len);
    }

    len = io->rpos + len;
    AVT_SWAP(io->rpos, len);
    return len;
}

static int mmap_flush(AVTIOCtx *io, int64_t timeout)
{
    size_t map_size;
    uint8_t *map_data = avt_buffer_get_data(io->map, &map_size);

    int ret = msync(map_data, map_size, timeout == 0 ? MS_ASYNC : MS_SYNC);
    if (ret < 0)
        ret = avt_handle_errno(io, "Error flushing: %i %s\n");

    return ret;
}

const AVTIO avt_io_mmap = {
    .name = "mmap",
    .type = AVT_IO_FD,
    .init = mmap_init,
    .get_max_pkt_len = mmap_max_pkt_len,
    .read_input = mmap_read,
    .write_vec = mmap_write_vec,
    .write_pkt = mmap_write_pkt,
    .rewrite = mmap_rewrite,
    .seek = mmap_seek,
    .flush = mmap_flush,
    .close = mmap_close,
};

const AVTIO avt_io_mmap_path = {
    .name = "mmap_path",
    .type = AVT_IO_FILE,
    .init = mmap_init_path,
    .get_max_pkt_len = mmap_max_pkt_len,
    .read_input = mmap_read,
    .write_vec = mmap_write_vec,
    .write_pkt = mmap_write_pkt,
    .rewrite = mmap_rewrite,
    .seek = mmap_seek,
    .flush = mmap_flush,
    .close = mmap_close,
};
