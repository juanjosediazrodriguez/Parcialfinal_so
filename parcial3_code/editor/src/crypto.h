#ifndef CRYPTO_H
#define CRYPTO_H

#include <stdint.h>
#include <stddef.h>

/*
 * RC4 — cifrado de flujo simétrico.
 * Retorna un buffer malloc'd con los datos cifrados/descifrados.
 * El llamador es responsable de liberar el buffer.
 */
uint8_t *rc4_encrypt(const uint8_t *key, size_t key_len,
                     const uint8_t *in,  size_t in_len,
                     size_t *out_len);

/* RC4 es simétrico: cifrar y descifrar son la misma operación */
#define rc4_decrypt rc4_encrypt

/*
 * Borrado seguro de memoria — equivalente a explicit_bzero.
 * El calificador volatile impide que el compilador elimine la
 * escritura por optimización, evitando que la llave quede en RAM.
 */
void secure_erase(void *ptr, size_t len);

#endif /* CRYPTO_H */
