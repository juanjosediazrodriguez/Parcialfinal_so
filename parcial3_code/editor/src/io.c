#include "io.h"

#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ------------------------------------------------------------------ */
/* Utilidad: tiempo en milisegundos con reloj monotónico               */
/* ------------------------------------------------------------------ */

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

/* ------------------------------------------------------------------ */
/* Escritura con open() + write()                                       */
/* Buffer alineado a PAGE_SIZE para reducir system calls               */
/* ------------------------------------------------------------------ */

int io_write_fd(const char *path, const uint8_t *data, size_t len, IOStats *st) {
    if (st) { st->elapsed_ms = 0; st->write_calls = 0; st->bytes_written = 0; }

    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) { perror("io_write_fd: open"); return -1; }

    double t0 = now_ms();

    const uint8_t *ptr      = data;
    size_t         remaining = len;

    while (remaining > 0) {
        /* Escribir en bloques de PAGE_SIZE — alineado al tamaño de página */
        size_t  chunk = remaining < IO_BUFFER_SIZE ? remaining : IO_BUFFER_SIZE;
        ssize_t written = write(fd, ptr, chunk);
        if (written < 0) { perror("io_write_fd: write"); close(fd); return -1; }
        ptr       += written;
        remaining -= (size_t)written;
        if (st) st->write_calls++;
    }

    close(fd);

    if (st) {
        st->elapsed_ms   = now_ms() - t0;
        st->bytes_written = len;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Lectura con open() + read()                                          */
/* ------------------------------------------------------------------ */

uint8_t *io_read_fd(const char *path, size_t *len, IOStats *st) {
    if (st) { st->elapsed_ms = 0; st->write_calls = 0; st->bytes_written = 0; }

    int fd = open(path, O_RDONLY);
    if (fd < 0) { perror("io_read_fd: open"); return NULL; }

    struct stat sb;
    if (fstat(fd, &sb) < 0) { perror("io_read_fd: fstat"); close(fd); return NULL; }

    size_t   fsize = (size_t)sb.st_size;
    uint8_t *buf   = malloc(fsize + 1);
    if (!buf) { perror("io_read_fd: malloc"); close(fd); return NULL; }

    double   t0        = now_ms();
    size_t   total_read = 0;
    uint8_t *ptr       = buf;

    while (total_read < fsize) {
        size_t  chunk = (fsize - total_read) < IO_BUFFER_SIZE
                        ? (fsize - total_read) : IO_BUFFER_SIZE;
        ssize_t r = read(fd, ptr, chunk);
        if (r <= 0) break;
        ptr        += r;
        total_read += (size_t)r;
    }
    buf[total_read] = '\0';
    close(fd);

    if (st) st->elapsed_ms = now_ms() - t0;
    *len = total_read;
    return buf;
}

/* ------------------------------------------------------------------ */
/* Escritura con mmap()                                                 */
/* Mapea el archivo directamente en memoria — evita copias extra       */
/* ------------------------------------------------------------------ */

int io_write_mmap(const char *path, const uint8_t *data, size_t len, IOStats *st) {
    if (st) { st->elapsed_ms = 0; st->write_calls = 0; st->bytes_written = 0; }
    if (len == 0) return 0;

    int fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) { perror("io_write_mmap: open"); return -1; }

    /* Establecer tamaño del archivo antes de mapear */
    if (ftruncate(fd, (off_t)len) < 0) {
        perror("io_write_mmap: ftruncate"); close(fd); return -1;
    }

    double t0 = now_ms();

    void *mapped = mmap(NULL, len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (mapped == MAP_FAILED) {
        perror("io_write_mmap: mmap"); close(fd); return -1;
    }

    memcpy(mapped, data, len);

    /* Sincronizar con el disco */
    if (msync(mapped, len, MS_SYNC) < 0) perror("io_write_mmap: msync");

    munmap(mapped, len);
    close(fd);

    if (st) {
        st->elapsed_ms    = now_ms() - t0;
        st->write_calls   = 1;         /* una sola llamada msync() */
        st->bytes_written = len;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Lectura con mmap()                                                   */
/* ------------------------------------------------------------------ */

uint8_t *io_read_mmap(const char *path, size_t *len, IOStats *st) {
    if (st) { st->elapsed_ms = 0; st->write_calls = 0; st->bytes_written = 0; }

    int fd = open(path, O_RDONLY);
    if (fd < 0) { perror("io_read_mmap: open"); return NULL; }

    struct stat sb;
    if (fstat(fd, &sb) < 0) { perror("io_read_mmap: fstat"); close(fd); return NULL; }

    size_t fsize = (size_t)sb.st_size;
    if (fsize == 0) { close(fd); *len = 0; return calloc(1, 1); }

    double t0 = now_ms();

    void *mapped = mmap(NULL, fsize, PROT_READ, MAP_PRIVATE, fd, 0);
    if (mapped == MAP_FAILED) {
        perror("io_read_mmap: mmap"); close(fd); return NULL;
    }

    uint8_t *buf = malloc(fsize + 1);
    if (!buf) { perror("io_read_mmap: malloc"); munmap(mapped, fsize); close(fd); return NULL; }

    memcpy(buf, mapped, fsize);
    buf[fsize] = '\0';

    munmap(mapped, fsize);
    close(fd);

    if (st) st->elapsed_ms = now_ms() - t0;
    *len = fsize;
    return buf;
}

/* ------------------------------------------------------------------ */
/* Imprimir estadísticas                                                */
/* ------------------------------------------------------------------ */

void io_print_stats(const char *label, const IOStats *st) {
    printf("  %-20s  tiempo: %8.3f ms  |  write() calls: %4ld  |  bytes: %zu\n",
           label, st->elapsed_ms, st->write_calls, st->bytes_written);
}

/* ------------------------------------------------------------------ */
/* Comparar write() vs mmap() sobre el mismo archivo                   */
/* ------------------------------------------------------------------ */

void io_compare_methods(const char *path, const uint8_t *data, size_t len) {
    IOStats st_fd   = {0, 0, 0};
    IOStats st_mmap = {0, 0, 0};

    char path_fd[512], path_mm[512];
    snprintf(path_fd, sizeof(path_fd), "%s.fd",   path);
    snprintf(path_mm, sizeof(path_mm), "%s.mmap", path);

    io_write_fd  (path_fd, data, len, &st_fd);
    io_write_mmap(path_mm, data, len, &st_mmap);

    printf("\n  === Comparación I/O (%zu bytes) ===\n\n", len);
    printf("  %-20s  %-12s  %-20s  %s\n",
           "Método", "Tiempo (ms)", "Llamadas write()", "Bytes al disco");
    printf("  %-20s  %-12.3f  %-20ld  %zu\n",
           "open/write (fd)", st_fd.elapsed_ms, st_fd.write_calls, st_fd.bytes_written);
    printf("  %-20s  %-12.3f  %-20ld  %zu\n",
           "mmap", st_mmap.elapsed_ms, st_mmap.write_calls, st_mmap.bytes_written);

    printf("\n  Reducción de write() calls con mmap: %.0f%%\n",
           st_fd.write_calls > 0
           ? 100.0*(1.0-(double)st_mmap.write_calls/(double)st_fd.write_calls)
           : 0.0);

    unlink(path_fd);
    unlink(path_mm);
    printf("\n");
}
