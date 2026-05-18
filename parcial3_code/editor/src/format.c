#include "format.h"
#include <string.h>

/* FNV-1a de 64 bits — rápido y con buena distribución */
uint64_t compute_checksum(const uint8_t *data, size_t len) {
    uint64_t hash = 0xcbf29ce484222325ULL;
    for (size_t i = 0; i < len; i++) {
        hash ^= data[i];
        hash *= 0x00000100000001b3ULL;
    }
    return hash;
}

const char *algo_name(uint8_t algo) {
    switch (algo) {
        case ALGO_RLE:     return "RLE";
        case ALGO_HUFFMAN: return "Huffman";
        case ALGO_LZ77:    return "LZ77";
        default:           return "Desconocido";
    }
}
