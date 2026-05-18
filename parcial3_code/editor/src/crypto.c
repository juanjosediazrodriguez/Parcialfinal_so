#include "crypto.h"

#include <stdlib.h>
#include <stdio.h>

/* ================================================================== */
/* RC4 — Key Scheduling Algorithm (KSA)                                */
/* ================================================================== */

static void rc4_ksa(uint8_t S[256], const uint8_t *key, size_t key_len) {
    for (int i = 0; i < 256; i++) S[i] = (uint8_t)i;

    int j = 0;
    for (int i = 0; i < 256; i++) {
        j = (j + S[i] + key[i % key_len]) & 0xFF;
        uint8_t t = S[i]; S[i] = S[j]; S[j] = t;
    }
}

/* ================================================================== */
/* RC4 — Pseudo-Random Generation Algorithm (PRGA) + XOR              */
/* ================================================================== */

uint8_t *rc4_encrypt(const uint8_t *key, size_t key_len,
                     const uint8_t *in,  size_t in_len,
                     size_t *out_len) {
    *out_len = 0;
    if (!key || key_len == 0 || !in || in_len == 0) return NULL;

    uint8_t *out = malloc(in_len);
    if (!out) { perror("rc4_encrypt: malloc"); return NULL; }

    uint8_t S[256];
    rc4_ksa(S, key, key_len);

    int i = 0, j = 0;
    for (size_t k = 0; k < in_len; k++) {
        i = (i + 1) & 0xFF;
        j = (j + S[i]) & 0xFF;
        uint8_t t = S[i]; S[i] = S[j]; S[j] = t;
        out[k] = in[k] ^ S[(S[i] + S[j]) & 0xFF];
    }

    /* Borrar el estado interno del keystream de la pila */
    secure_erase(S, sizeof(S));

    *out_len = in_len;
    return out;
}

/* ================================================================== */
/* Borrado seguro — la escritura volatile no puede ser eliminada       */
/* por el optimizador del compilador (equivale a explicit_bzero)       */
/* ================================================================== */

void secure_erase(void *ptr, size_t len) {
    volatile uint8_t *p = (volatile uint8_t *)ptr;
    while (len--) *p++ = 0;
}
