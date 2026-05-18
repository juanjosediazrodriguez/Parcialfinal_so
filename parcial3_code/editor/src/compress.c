#include "compress.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ================================================================== */
/* RLE — Run-Length Encoding                                           */
/* Formato: [count][byte] ... count siempre 1-255                      */
/* ================================================================== */

uint8_t *rle_compress(const uint8_t *in, size_t in_len, size_t *out_len) {
    if (in_len == 0) { *out_len = 0; return NULL; }

    uint8_t *out = malloc(in_len * 2);
    if (!out) { perror("rle_compress"); exit(1); }

    size_t o = 0, i = 0;
    while (i < in_len) {
        uint8_t byte  = in[i];
        size_t  count = 1;
        while (i + count < in_len && in[i + count] == byte && count < 255)
            count++;
        out[o++] = (uint8_t)count;
        out[o++] = byte;
        i += count;
    }

    *out_len = o;
    uint8_t *r = realloc(out, o);
    return r ? r : out;
}

uint8_t *rle_decompress(const uint8_t *in, size_t in_len, size_t *out_len) {
    if (in_len == 0) { *out_len = 0; return NULL; }

    size_t   cap = in_len * 4;
    uint8_t *out = malloc(cap);
    if (!out) { perror("rle_decompress"); exit(1); }

    size_t o = 0;
    for (size_t i = 0; i + 1 < in_len; i += 2) {
        size_t  count = in[i];
        uint8_t byte  = in[i + 1];
        if (o + count > cap) {
            cap = (o + count) * 2;
            uint8_t *tmp = realloc(out, cap);
            if (!tmp) { perror("rle_decompress realloc"); exit(1); }
            out = tmp;
        }
        memset(out + o, byte, count);
        o += count;
    }

    *out_len = o;
    uint8_t *r = realloc(out, o ? o : 1);
    return r ? r : out;
}

/* ================================================================== */
/* Huffman Coding                                                       */
/* Formato: [uint32 orig_size][uint32[256] frecuencias][bitstream]     */
/* ================================================================== */

#define HUFF_SYMBOLS   256
#define HUFF_MAX_NODES (HUFF_SYMBOLS * 2)

typedef struct {
    uint32_t freq;
    int      left;    /* -1 si es hoja */
    int      right;   /* -1 si es hoja */
    uint16_t symbol;
} HuffNode;

typedef struct {
    uint32_t bits;
    uint8_t  len;
} HuffCode;

/* Min-heap basado en array de índices */
typedef struct {
    int       heap[HUFF_MAX_NODES];
    int       size;
    HuffNode *nodes;
} MinHeap;

static void heap_push(MinHeap *h, int idx) {
    int i = h->size++;
    h->heap[i] = idx;
    while (i > 0) {
        int p = (i - 1) / 2;
        if (h->nodes[h->heap[p]].freq <= h->nodes[h->heap[i]].freq) break;
        int tmp = h->heap[p]; h->heap[p] = h->heap[i]; h->heap[i] = tmp;
        i = p;
    }
}

static int heap_pop(MinHeap *h) {
    int top    = h->heap[0];
    h->heap[0] = h->heap[--h->size];
    int i = 0;
    while (1) {
        int l = 2*i+1, r = 2*i+2, s = i;
        if (l < h->size && h->nodes[h->heap[l]].freq < h->nodes[h->heap[s]].freq) s = l;
        if (r < h->size && h->nodes[h->heap[r]].freq < h->nodes[h->heap[s]].freq) s = r;
        if (s == i) break;
        int tmp = h->heap[i]; h->heap[i] = h->heap[s]; h->heap[s] = tmp;
        i = s;
    }
    return top;
}

static void gen_codes(HuffNode *nodes, int idx, uint32_t code,
                      uint8_t depth, HuffCode *tbl) {
    if (nodes[idx].left == -1) {
        tbl[nodes[idx].symbol].bits = code;
        tbl[nodes[idx].symbol].len  = depth ? depth : 1;
        return;
    }
    if (nodes[idx].left  != -1)
        gen_codes(nodes, nodes[idx].left,  (code<<1)|0, depth+1, tbl);
    if (nodes[idx].right != -1)
        gen_codes(nodes, nodes[idx].right, (code<<1)|1, depth+1, tbl);
}

/* Escritor de bits */
typedef struct {
    uint8_t *data;
    size_t   cap;
    size_t   byte_pos;
    int      bit_pos;
} BitWriter;

static void bw_init(BitWriter *bw) {
    bw->cap = 4096; bw->byte_pos = 0; bw->bit_pos = 0;
    bw->data = calloc(bw->cap, 1);
}

static void bw_write(BitWriter *bw, uint32_t val, int n) {
    for (int i = n - 1; i >= 0; i--) {
        if (bw->byte_pos >= bw->cap - 1) {
            bw->cap *= 2;
            bw->data = realloc(bw->data, bw->cap);
            memset(bw->data + bw->cap/2, 0, bw->cap/2);
        }
        bw->data[bw->byte_pos] |= (uint8_t)(((val >> i) & 1) << (7 - bw->bit_pos));
        if (++bw->bit_pos == 8) { bw->bit_pos = 0; bw->byte_pos++; }
    }
}

static size_t bw_size(const BitWriter *bw) {
    return bw->byte_pos + (bw->bit_pos > 0 ? 1 : 0);
}

static int huff_build_tree(const uint32_t *freq, HuffNode *nodes, int *node_count) {
    *node_count = 0;
    MinHeap heap = { .size = 0, .nodes = nodes };

    for (int s = 0; s < HUFF_SYMBOLS; s++) {
        if (freq[s] > 0) {
            nodes[*node_count] = (HuffNode){ freq[s], -1, -1, (uint16_t)s };
            heap_push(&heap, (*node_count)++);
        }
    }
    if (heap.size == 0) return -1;

    /* Caso especial: un solo símbolo único */
    if (heap.size == 1) {
        int only = heap_pop(&heap);
        nodes[*node_count] = (HuffNode){ nodes[only].freq, only, -1, 0 };
        heap_push(&heap, (*node_count)++);
    }

    while (heap.size > 1) {
        int a = heap_pop(&heap), b = heap_pop(&heap);
        nodes[*node_count] = (HuffNode){ nodes[a].freq + nodes[b].freq, a, b, 0 };
        heap_push(&heap, (*node_count)++);
    }
    return heap_pop(&heap);
}

uint8_t *huffman_compress(const uint8_t *in, size_t in_len, size_t *out_len) {
    if (in_len == 0) { *out_len = 0; return NULL; }

    uint32_t freq[HUFF_SYMBOLS] = {0};
    for (size_t i = 0; i < in_len; i++) freq[in[i]]++;

    HuffNode nodes[HUFF_MAX_NODES];
    int  node_count = 0;
    int  root       = huff_build_tree(freq, nodes, &node_count);

    HuffCode tbl[HUFF_SYMBOLS] = {0};
    gen_codes(nodes, root, 0, 0, tbl);

    BitWriter bw;
    bw_init(&bw);
    for (size_t i = 0; i < in_len; i++)
        bw_write(&bw, tbl[in[i]].bits, tbl[in[i]].len);

    size_t bits_len = bw_size(&bw);
    size_t total    = sizeof(uint32_t) + HUFF_SYMBOLS*sizeof(uint32_t) + bits_len;
    uint8_t *out    = malloc(total);
    if (!out) { perror("huffman_compress"); exit(1); }

    uint32_t orig32 = (uint32_t)in_len;
    memcpy(out,                                              &orig32, sizeof(uint32_t));
    memcpy(out + sizeof(uint32_t),                          freq,    HUFF_SYMBOLS*sizeof(uint32_t));
    memcpy(out + sizeof(uint32_t) + HUFF_SYMBOLS*sizeof(uint32_t), bw.data, bits_len);

    free(bw.data);
    *out_len = total;
    return out;
}

uint8_t *huffman_decompress(const uint8_t *in, size_t in_len, size_t *out_len) {
    if (in_len == 0) { *out_len = 0; return NULL; }

    uint32_t orig_size;
    memcpy(&orig_size, in, sizeof(uint32_t));

    uint32_t freq[HUFF_SYMBOLS];
    memcpy(freq, in + sizeof(uint32_t), HUFF_SYMBOLS*sizeof(uint32_t));

    const uint8_t *bits = in  + sizeof(uint32_t) + HUFF_SYMBOLS*sizeof(uint32_t);
    size_t bits_len     = in_len - sizeof(uint32_t) - HUFF_SYMBOLS*sizeof(uint32_t);

    HuffNode nodes[HUFF_MAX_NODES];
    int  node_count = 0;
    int  root       = huff_build_tree(freq, nodes, &node_count);

    uint8_t *out = malloc(orig_size + 1);
    if (!out) { perror("huffman_decompress"); exit(1); }

    /* Caso: raíz es hoja (un solo símbolo) */
    if (nodes[root].left == -1) {
        memset(out, nodes[root].symbol, orig_size);
        *out_len = orig_size;
        return out;
    }

    size_t decoded = 0;
    int    cur     = root;
    for (size_t bi = 0; bi < bits_len && decoded < orig_size; bi++) {
        for (int b = 7; b >= 0 && decoded < orig_size; b--) {
            int go   = (bits[bi] >> b) & 1;
            int next = go ? nodes[cur].right : nodes[cur].left;
            if (next == -1) { cur = root; continue; }  /* bit de relleno */
            cur = next;
            if (nodes[cur].left == -1) {
                out[decoded++] = (uint8_t)nodes[cur].symbol;
                cur = root;
            }
        }
    }

    *out_len = decoded;
    return out;
}

/* ================================================================== */
/* LZ77 — estilo LZSS con flag bytes                                   */
/*                                                                     */
/* Formato: [uint32 orig_size] [bloques...]                            */
/* Bloque : [flag_byte] [item0..item7]                                 */
/*   bit=0 → literal  (1 byte)                                        */
/*   bit=1 → match    (2 bytes: 4 bits len-3 | 12 bits offset)        */
/* ================================================================== */

static void lz77_find_match(const uint8_t *data, size_t pos, size_t len,
                             size_t *best_off, size_t *best_len) {
    *best_off = 0; *best_len = 0;
    size_t win  = (pos > LZ77_WINDOW) ? pos - LZ77_WINDOW : 0;
    size_t look = len - pos;
    if (look > LZ77_MAX_MATCH) look = LZ77_MAX_MATCH;

    for (size_t i = win; i < pos; i++) {
        size_t ml = 0;
        while (ml < look && data[i + ml] == data[pos + ml]) ml++;
        if (ml >= (size_t)LZ77_MIN_MATCH && ml > *best_len) {
            *best_len = ml;
            *best_off = pos - i;
            if (ml == LZ77_MAX_MATCH) break;
        }
    }
}

uint8_t *lz77_compress(const uint8_t *in, size_t in_len, size_t *out_len) {
    if (in_len == 0) { *out_len = 0; return NULL; }

    size_t   cap = sizeof(uint32_t) + in_len * 3 + 16;
    uint8_t *out = malloc(cap);
    if (!out) { perror("lz77_compress"); exit(1); }

    uint32_t orig32 = (uint32_t)in_len;
    memcpy(out, &orig32, sizeof(uint32_t));
    size_t o = sizeof(uint32_t);
    size_t i = 0;

    while (i < in_len) {
        size_t  flag_pos = o++;
        uint8_t flag     = 0;

        for (int bit = 0; bit < 8 && i < in_len; bit++) {
            size_t best_off = 0, best_len = 0;
            lz77_find_match(in, i, in_len, &best_off, &best_len);

            if (best_len >= (size_t)LZ77_MIN_MATCH && best_off > 0) {
                /* Match: 4 bits (len-3) | 12 bits (offset) = 2 bytes */
                uint16_t enc = (uint16_t)(((best_len - LZ77_MIN_MATCH) << 12) | best_off);
                out[o++] = (enc >> 8) & 0xFF;
                out[o++] =  enc       & 0xFF;
                flag    |= (uint8_t)(1 << bit);
                i += best_len;
            } else {
                out[o++] = in[i++];
            }
        }
        out[flag_pos] = flag;
    }

    *out_len = o;
    uint8_t *r = realloc(out, o);
    return r ? r : out;
}

uint8_t *lz77_decompress(const uint8_t *in, size_t in_len, size_t *out_len) {
    if (in_len == 0) { *out_len = 0; return NULL; }

    uint32_t orig_size;
    memcpy(&orig_size, in, sizeof(uint32_t));
    size_t i = sizeof(uint32_t);

    uint8_t *out = malloc(orig_size + 1);
    if (!out) { perror("lz77_decompress"); exit(1); }

    size_t o = 0;
    while (i < in_len && o < orig_size) {
        uint8_t flag = in[i++];

        for (int bit = 0; bit < 8 && i < in_len && o < orig_size; bit++) {
            if (flag & (1 << bit)) {
                if (i + 1 >= in_len) break;
                uint16_t enc = ((uint16_t)in[i] << 8) | in[i+1];
                i += 2;
                size_t mlen = (enc >> 12) + LZ77_MIN_MATCH;
                size_t moff = enc & 0x0FFF;
                if (moff == 0 || moff > o) continue;  /* token inválido */
                size_t src = o - moff;
                /* Copia byte a byte: soporta solapamiento (patrones repetidos) */
                for (size_t k = 0; k < mlen && o < orig_size; k++)
                    out[o++] = out[src + k];
            } else {
                out[o++] = in[i++];
            }
        }
    }

    *out_len = o;
    return out;
}

/* ================================================================== */
/* Tabla comparativa de los 3 algoritmos                               */
/* ================================================================== */

void print_comparison(const uint8_t *data, size_t len) {
    if (len == 0) { printf("  (archivo vacío)\n"); return; }

    size_t rle_sz = 0, huff_sz = 0, lz_sz = 0;
    uint8_t *r = rle_compress(data, len, &rle_sz);     free(r);
    uint8_t *h = huffman_compress(data, len, &huff_sz); free(h);
    uint8_t *l = lz77_compress(data, len, &lz_sz);     free(l);

    printf("\n  +------------------+----------+----------+---------+\n");
    printf("  | Algoritmo        | Original | Comprim. |  Ahorro |\n");
    printf("  +------------------+----------+----------+---------+\n");
    printf("  | RLE              | %8zu | %8zu | %6.1f%% |\n",
           len, rle_sz,  100.0*(1.0-(double)rle_sz /(double)len));
    printf("  | Huffman          | %8zu | %8zu | %6.1f%% |\n",
           len, huff_sz, 100.0*(1.0-(double)huff_sz/(double)len));
    printf("  | LZ77             | %8zu | %8zu | %6.1f%% |\n",
           len, lz_sz,   100.0*(1.0-(double)lz_sz  /(double)len));
    printf("  +------------------+----------+----------+---------+\n\n");
}
