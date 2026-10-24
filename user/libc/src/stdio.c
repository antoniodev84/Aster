#include <os.h>

struct FILE {
    int fd, mode, error, eof, last, ungot, owned_buffer;
    unsigned flags;
    unsigned char *buffer;
    size_t capacity, read_position, read_end, write_position;
    struct FILE *next;
};
static unsigned char input_buffer[BUFSIZ], output_buffer[BUFSIZ];
static struct FILE
    input = {.fd = 0, .mode = _IOFBF, .ungot = -1, .buffer = input_buffer, .capacity = BUFSIZ},
    output = {.fd = 1, .mode = _IOLBF, .ungot = -1, .buffer = output_buffer, .capacity = BUFSIZ},
    error_output = {.fd = 2, .mode = _IONBF, .ungot = -1};
FILE *stdin = &input, *stdout = &output, *stderr = &error_output;
static FILE *streams;
static int write_all(int fd, const void *buffer, size_t count) {
    const char *p = buffer;
    while (count) {
        ssize_t n = write(fd, p, count);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        if (!n) {
            errno = EIO;
            return -1;
        }
        p += n;
        count -= n;
    }
    return 0;
}
static int flush_one(FILE *f) {
    if (f->last == 2 && f->write_position) {
        if (write_all(f->fd, f->buffer, f->write_position) < 0) {
            f->error = 1;
            return EOF;
        }
        f->write_position = 0;
    }
    if (f->last == 1) {
        off_t unread = f->read_end - f->read_position + (f->ungot >= 0);
        if (unread && lseek(f->fd, -unread, SEEK_CUR) < 0) {
            if (errno != ESPIPE) {
                f->error = 1;
                return EOF;
            }
        }
        f->read_position = f->read_end = 0;
        f->ungot = -1;
    }
    return 0;
}
int fflush(FILE *f) {
    if (f)
        return flush_one(f);
    int result = 0;
    if (flush_one(stdout) < 0 || flush_one(stderr) < 0)
        result = EOF;
    for (FILE *p = streams; p; p = p->next)
        if (flush_one(p) < 0)
            result = EOF;
    return result;
}
static int mode_flags(const char *mode) {
    int flags;
    if (*mode == 'r')
        flags = O_RDONLY;
    else if (*mode == 'w')
        flags = O_WRONLY | O_CREAT | O_TRUNC;
    else if (*mode == 'a')
        flags = O_WRONLY | O_CREAT | O_APPEND;
    else {
        errno = EINVAL;
        return -1;
    }
    for (const char *p = mode + 1; *p; p++) {
        if (*p == '+')
            flags = (flags & ~3) | O_RDWR;
        else if (*p == 'e')
            flags |= O_CLOEXEC;
        else if (*p == 'x')
            flags |= O_EXCL;
        else if (*p != 'b') {
            errno = EINVAL;
            return -1;
        }
    }
    return flags;
}
FILE *fdopen(int fd, const char *mode) {
    int flags = mode_flags(mode);
    if (flags < 0)
        return NULL;
    int original = fcntl(fd, F_GETFL);
    if (original < 0)
        return NULL;
    if (((flags & 3) != O_RDONLY && (original & 3) == O_RDONLY) ||
        ((flags & 3) != O_WRONLY && (original & 3) == O_WRONLY)) {
        errno = EINVAL;
        return NULL;
    }
    if ((flags & O_APPEND) && !(original & O_APPEND) &&
        fcntl(fd, F_SETFL, (long)(original | O_APPEND)) < 0)
        return NULL;
    FILE *f = calloc(1, sizeof(*f));
    if (!f)
        return NULL;
    f->buffer = malloc(BUFSIZ);
    if (!f->buffer) {
        free(f);
        return NULL;
    }
    f->fd = fd;
    f->flags = flags;
    f->mode = _IOFBF;
    f->capacity = BUFSIZ;
    f->owned_buffer = 1;
    f->ungot = -1;
    f->next = streams;
    streams = f;
    return f;
}
FILE *fopen(const char *path, const char *mode) {
    int flags = mode_flags(mode);
    if (flags < 0)
        return NULL;
    int fd = open(path, flags, 0666);
    if (fd < 0)
        return NULL;
    FILE *f = fdopen(fd, mode);
    if (!f)
        close(fd);
    return f;
}
FILE *freopen(const char *path, const char *mode, FILE *f) {
    int flags = mode_flags(mode);
    if (flags < 0)
        return NULL;
    fflush(f);
    int fd = path ? open(path, flags, 0666) : f->fd;
    if (fd < 0)
        return NULL;
    if (path)
        close(f->fd);
    f->fd = fd;
    f->flags = flags;
    f->last = f->error = f->eof = 0;
    f->ungot = -1;
    f->read_position = f->read_end = f->write_position = 0;
    return f;
}
int fclose(FILE *f) {
    if (!f) {
        errno = EINVAL;
        return EOF;
    }
    int result = fflush(f);
    if (close(f->fd) < 0)
        result = EOF;
    if (f->owned_buffer)
        free(f->buffer);
    if (f == stdin || f == stdout || f == stderr) {
        f->fd = -1;
        f->buffer = NULL;
        f->capacity = 0;
        return result;
    }
    FILE **link = &streams;
    while (*link && *link != f)
        link = &(*link)->next;
    if (*link)
        *link = f->next;
    free(f);
    return result;
}
int fileno(FILE *f) {
    return f->fd;
}
int feof(FILE *f) {
    return f->eof;
}
int ferror(FILE *f) {
    return f->error;
}
void clearerr(FILE *f) {
    f->error = f->eof = 0;
}
int setvbuf(FILE *f, char *buffer, int mode, size_t size) {
    if (mode < _IOFBF || mode > _IONBF) {
        errno = EINVAL;
        return -1;
    }
    if (fflush(f) < 0)
        return -1;
    unsigned char *replacement = (unsigned char *)buffer;
    bool own = false;
    if (mode != _IONBF && !replacement) {
        if (!size)
            size = BUFSIZ;
        replacement = malloc(size);
        if (!replacement)
            return -1;
        own = true;
    }
    if (mode != _IONBF && !size) {
        errno = EINVAL;
        return -1;
    }
    if (f->owned_buffer)
        free(f->buffer);
    f->buffer = replacement;
    f->capacity = mode == _IONBF ? 0 : size;
    f->owned_buffer = own;
    f->mode = mode;
    return 0;
}
void setbuf(FILE *f, char *b) {
    setvbuf(f, b, b ? _IOFBF : _IONBF, b ? BUFSIZ : 0);
}
int fgetc(FILE *f) {
    if (f->last == 2 && fflush(f) < 0)
        return EOF;
    f->last = 1;
    if (f->ungot >= 0) {
        int c = f->ungot;
        f->ungot = -1;
        return c;
    }
    if (f->read_position < f->read_end)
        return f->buffer[f->read_position++];
    unsigned char c;
    size_t size = f->mode == _IONBF ? 1 : f->capacity;
    unsigned char *buffer = f->mode == _IONBF ? &c : f->buffer;
    ssize_t n = read(f->fd, buffer, size);
    if (n <= 0) {
        if (n < 0)
            f->error = 1;
        else
            f->eof = 1;
        return EOF;
    }
    if (f->mode == _IONBF)
        return c;
    f->read_position = 1;
    f->read_end = n;
    return f->buffer[0];
}
int fputc(int c, FILE *f) {
    if (f->last == 1 && fflush(f) < 0)
        return EOF;
    f->last = 2;
    unsigned char byte = c;
    if (f->mode == _IONBF) {
        if (write_all(f->fd, &byte, 1) < 0) {
            f->error = 1;
            return EOF;
        }
        return byte;
    }
    if (f->write_position == f->capacity && fflush(f) < 0)
        return EOF;
    f->buffer[f->write_position++] = byte;
    if ((f->mode == _IOLBF && byte == '\n') || f->write_position == f->capacity)
        if (fflush(f) < 0)
            return EOF;
    return byte;
}
size_t fread(void *restrict b, size_t size, size_t count, FILE *restrict f) {
    if (size && count > SIZE_MAX / size) {
        errno = EOVERFLOW;
        f->error = 1;
        return 0;
    }
    if (!size)
        return 0;
    size_t bytes = size * count, done = 0;
    while (done < bytes) {
        int c = fgetc(f);
        if (c == EOF)
            break;
        ((unsigned char *)b)[done++] = c;
    }
    return done / size;
}
size_t fwrite(const void *restrict b, size_t size, size_t count, FILE *restrict f) {
    if (size && count > SIZE_MAX / size) {
        errno = EOVERFLOW;
        f->error = 1;
        return 0;
    }
    if (!size)
        return 0;
    size_t bytes = size * count, done = 0;
    while (done < bytes) {
        if (fputc(((const unsigned char *)b)[done], f) == EOF)
            break;
        done++;
    }
    return done / size;
}
int getc(FILE *f) {
    return fgetc(f);
}
int getchar(void) {
    return fgetc(stdin);
}
int putc(int c, FILE *f) {
    return fputc(c, f);
}
int putchar(int c) {
    return fputc(c, stdout);
}
int ungetc(int c, FILE *f) {
    if (c == EOF || f->ungot >= 0)
        return EOF;
    f->ungot = (unsigned char)c;
    f->eof = 0;
    return f->ungot;
}
char *fgets(char *restrict b, int n, FILE *restrict f) {
    if (n <= 0) {
        errno = EINVAL;
        return NULL;
    }
    int i = 0;
    while (i < n - 1) {
        int c = fgetc(f);
        if (c == EOF)
            break;
        b[i++] = c;
        if (c == '\n')
            break;
    }
    b[i] = 0;
    return i || n == 1 ? b : NULL;
}
int fputs(const char *restrict s, FILE *restrict f) {
    size_t n = strlen(s);
    return fwrite(s, 1, n, f) == n ? 0 : EOF;
}
int puts(const char *s) {
    return fputs(s, stdout) < 0 || fputc('\n', stdout) < 0 ? EOF : 0;
}
ssize_t getdelim(char **restrict b, size_t *restrict capacity, int delimiter, FILE *restrict f) {
    if (!b || !capacity) {
        errno = EINVAL;
        return -1;
    }
    size_t n = 0;
    for (;;) {
        int c = fgetc(f);
        if (c == EOF) {
            if (!n)
                return -1;
            break;
        }
        if (!*b || n + 2 > *capacity) {
            size_t size = *capacity ? *capacity * 2 : 128;
            if (size < n + 2 || size > INT64_MAX) {
                errno = ENOMEM;
                return -1;
            }
            char *next = realloc(*b, size);
            if (!next)
                return -1;
            *b = next;
            *capacity = size;
        }
        (*b)[n++] = c;
        if (c == (unsigned char)delimiter)
            break;
    }
    (*b)[n] = 0;
    return n;
}
ssize_t getline(char **restrict b, size_t *restrict n, FILE *restrict f) {
    return getdelim(b, n, '\n', f);
}
int fseeko(FILE *f, off_t offset, int whence) {
    if (fflush(f) < 0)
        return -1;
    if (lseek(f->fd, offset, whence) < 0)
        return -1;
    f->eof = 0;
    f->last = 0;
    return 0;
}
int fseek(FILE *f, long offset, int whence) {
    return fseeko(f, offset, whence);
}
off_t ftello(FILE *f) {
    off_t offset = lseek(f->fd, 0, SEEK_CUR);
    if (offset < 0)
        return -1;
    if (f->last == 1)
        offset -= f->read_end - f->read_position + (f->ungot >= 0);
    else if (f->last == 2)
        offset += f->write_position;
    return offset;
}
long ftell(FILE *f) {
    return ftello(f);
}
void rewind(FILE *f) {
    fseek(f, 0, SEEK_SET);
    clearerr(f);
}
int fgetpos(FILE *f, fpos_t *p) {
    off_t n = ftello(f);
    if (n < 0)
        return -1;
    *p = n;
    return 0;
}
int fsetpos(FILE *f, const fpos_t *p) {
    return fseeko(f, *p, SEEK_SET);
}
int remove(const char *p) {
    struct stat s;
    if (lstat(p, &s) < 0)
        return -1;
    return S_ISDIR(s.st_mode) ? rmdir(p) : unlink(p);
}
int mkstemp(char *template) {
    size_t n = strlen(template);
    if (n < 6 || strcmp(template + n - 6, "XXXXXX")) {
        errno = EINVAL;
        return -1;
    }
    static uint64_t serial;
    for (unsigned attempt = 0; attempt < 1024; attempt++) {
        uint64_t v = ((uint64_t)getpid() << 20) ^ serial++;
        for (unsigned i = 0; i < 6; i++) {
            template[n - 1 - i] = "0123456789abcdefghijklmnopqrstuvwxyz"[v % 36];
            v /= 36;
        }
        int fd = open(template, O_RDWR | O_CREAT | O_EXCL, 0600);
        if (fd >= 0)
            return fd;
        if (errno != EEXIST)
            return -1;
    }
    errno = EEXIST;
    return -1;
}
FILE *tmpfile(void) {
    char path[] = "/tmp/fileXXXXXX";
    int fd = mkstemp(path);
    if (fd < 0)
        return NULL;
    unlink(path);
    FILE *f = fdopen(fd, "w+");
    if (!f)
        close(fd);
    return f;
}
