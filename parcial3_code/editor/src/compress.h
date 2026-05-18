#ifndef COMPRESS_H
#define COMPRESS_H

#include <stdint.h>
#include <stddef.h>

/* ---- RLE ---------------------------------------------------------- */
uint8_t *rle_compress  (const uint8_t *in, size_t in_len, size_t *out_len);
uint8_t *rle_decompress(const uint8_t *in, size_t in_len, size_t *out_len);

/* ---- Huffman ------------------------------------------------------- */
uint8_t *huffman_compress  (const uint8_t *in, size_t in_len, size_t *out_len);
uint8_t *huffman_decompress(const uint8_t *in, size_t in_len, size_t *out_len);

/* ---- LZ77 (estilo LZSS) ------------------------------------------- */
#define LZ77_WINDOW   4096   /* ventana de búsqueda — coincide con tamaño de página */
#define LZ77_MAX_MATCH  18   /* máximo de bytes por coincidencia (4 bits → 0-15 + 3) */
#define LZ77_MIN_MATCH   3   /* mínimo útil para emitir una referencia              */

uint8_t *lz77_compress  (const uint8_t *in, size_t in_len, size_t *out_len);
uint8_t *lz77_decompress(const uint8_t *in, size_t in_len, size_t *out_len);

/* ---- Utilidades ---------------------------------------------------- */
void print_comparison(const uint8_t *data, size_t len);

#endif /* COMPRESS_H */
