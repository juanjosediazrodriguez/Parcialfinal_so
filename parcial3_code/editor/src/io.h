#ifndef IO_H
#define IO_H

#include <stdint.h>
#include <stddef.h>

#define PAGE_SIZE      4096   /* tamaño de página del SO — alineación óptima */
#define IO_BUFFER_SIZE PAGE_SIZE

/*
 * Estadísticas de una operación I/O.
 * Permite comparar empíricamente write() vs mmap().
 */
typedef struct {
    double elapsed_ms;      /* tiempo total en milisegundos          */
    long   write_calls;     /* llamadas a write() o msync() usadas   */
    size_t bytes_written;   /* bytes enviados al disco               */
} IOStats;

/* Escritura con descriptores de archivo (open + write, buffer de PAGE_SIZE) */
int      io_write_fd  (const char *path, const uint8_t *data, size_t len, IOStats *st);

/* Lectura con descriptores de archivo (open + read) */
uint8_t *io_read_fd   (const char *path, size_t *len, IOStats *st);

/* Escritura con mapeo en memoria (mmap + memcpy + msync) */
int      io_write_mmap(const char *path, const uint8_t *data, size_t len, IOStats *st);

/* Lectura con mapeo en memoria (mmap + memcpy) */
uint8_t *io_read_mmap (const char *path, size_t *len, IOStats *st);

void     io_print_stats      (const char *label, const IOStats *st);
void     io_compare_methods  (const char *path,  const uint8_t *data, size_t len);

#endif /* IO_H */
