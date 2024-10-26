#include "stream.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>

void stream_open_memory(stream* stream, const void* data, size_t len) {
    stream->ty = STREAM_MEMORY;
    stream->inner.data = (buffer){.data = data, .len = len, .pos = 0};
    return;
}

int stream_open_file(stream* stream, const char* filename) {
    stream->ty = STREAM_FILE;
    stream->inner.file = fopen(filename, "rb");
    if (!stream->inner.file) {
        int result = errno;
        errno = 0;
        return result;
    }
    return 0;
}

int stream_close(stream* stream) {
    if (stream->ty == STREAM_MEMORY) {
        return 0;
    }
    if (stream->ty == STREAM_FILE) {
        return fclose(stream->inner.file);
    }
    assert(0);
}

int stream_pos(stream* stream, long* pos) {
    int result = 0;
    switch (stream->ty) {
        case STREAM_MEMORY:
            *pos = stream->inner.data.pos;
            return 0;
        case STREAM_FILE:
            *pos = ftell(stream->inner.file);
            if (*pos == -1) {
                result = errno;
                errno = 0;
                return result;
            }
            return 0;
    }
    assert(0);
}

int stream_read(stream* stream, unsigned char* out) {
    buffer* buf = NULL;
    int result = 0;
    switch (stream->ty) {
        case STREAM_MEMORY:
            buf = &stream->inner.data;
            if (buf->pos == buf->len) {
                return EOF;
            }
            *out = buf->data[buf->pos];
            buf->pos++;
            return 0;
        case STREAM_FILE:
            result = fgetc(stream->inner.file);
            if (result == EOF) {
                return EOF;
            }
            *out = (unsigned char)result;
            return 0;
    }
    assert(0);
}

int stream_seek(stream* stream, size_t relative) {
    buffer* buf = NULL;
    size_t next;
    switch (stream->ty) {
        case STREAM_MEMORY:
            buf = &stream->inner.data;
            next = buf->pos + relative;
            if (next < 0 || next >= buf->len) {
                return -1;
            }
            buf->pos = next;
            return 0;
        case STREAM_FILE:
            return fseek(stream->inner.file, relative, SEEK_CUR);
    }
    assert(0);
}

int stream_read_utf8_continuation(stream* st, unsigned char* out, size_t n) {
    unsigned char current;
    int err;
    for (size_t i = 0; i < n; i++) {
        err = stream_read(st, &current);
        if (err != 0) {
            return err;
        }
        if (0x80 != (0xc0 & current)) {
            // not continuation byte
            return ERR_INVALID_UTF8;
        }
        out[i] = current;
    }
    return 0;
}

// out needs to be at least 4 bytes long
// returns amount of bytes read
int stream_next_utf8_cp(stream* st, unsigned char* out, size_t* len) {
    unsigned char current;
    *len = 0;
    int err = stream_read(st, &current);
    if (err != 0) {
        return err;
    }
    out[0] = current;
    if (0xf0 == (0xf8 & current)) {
        err = stream_read_utf8_continuation(st, out + 1, 3);
        if (err != 0) {
            return err;
        }
        if ((0 == (0x07 & out[0])) && (0 == (0x30 & out[1]))) {
            // overlong encoding
            return ERR_INVALID_UTF8;
        }
        *len = 4;
        return 0;
    }
    if (0xe0 == (0xf0 & current)) {
        err = stream_read_utf8_continuation(st, out + 1, 2);
        if (err != 0) {
            return err;
        }
        if ((0 == (0x07 & out[0])) && (0 == (0x30 & out[1]))) {
            // overlong encoding
            return ERR_INVALID_UTF8;
        }
        *len = 3;
        return 0;
    }
    if (0xc0 == (0xe0 & current)) {
        err = stream_read_utf8_continuation(st, out + 1, 1);
        if (err != 0) {
            return err;
        }
        if (0 == (0x1e & out[0])) {
            // overlong encoding
            return ERR_INVALID_UTF8;
        }
        *len = 2;
        return 0;
    }
    if (0x00 == (0x80 & current)) {
        *len = 1;
        return 0;
    }
    // continuation byte at start position
    return ERR_INVALID_UTF8;
}
