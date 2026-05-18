#ifndef FORMAT_H
#define FORMAT_H

#include <stdint.h>
#include <stddef.h>

#define MAGIC_NUMBER    "EDITORCMP"
#define HEADER_SIZE     64
#define FORMAT_VERSION  1

/* Identificadores de algoritmo */
#define ALGO_RLE        0
#define ALGO_HUFFMAN    1
#define ALGO_LZ77       2

/*
 * Cabecera binaria del archivo comprimido.
 * __attribute__((packed)) elimina el padding del compilador.
 * Tamaño exacto: 9+1+1+1+4+4+4+8+32 = 64 bytes.
 */
typedef struct __attribute__((packed)) {
    char     magic[9];          /* "EDITORCMP"                     9 bytes */
    uint8_t  version;           /* versión del formato             1 byte  */
    uint8_t  algo;              /* ALGO_RLE/HUFFMAN/LZ77           1 byte  */
    uint8_t  encrypted;         /* 1 = cifrado RC4, 0 = sin cifrar 1 byte  */
    uint8_t  reserved[4];       /* reservado                       4 bytes */
    uint32_t original_size;     /* tamaño original                 4 bytes */
    uint32_t compressed_size;   /* tamaño comprimido (pre-cifrado) 4 bytes */
    uint64_t checksum;          /* FNV-1a de datos comprimidos     8 bytes */
    char     filename[32];      /* nombre del archivo             32 bytes */
} FileHeader;                   /*                             = 64 bytes  */

/* Verificación en tiempo de compilación — compatible C89/C99/C11 */
typedef char _check_FileHeader_size[(sizeof(FileHeader) == HEADER_SIZE) ? 1 : -1];

uint64_t compute_checksum(const uint8_t *data, size_t len);
const char *algo_name(uint8_t algo);

#endif /* FORMAT_H */
