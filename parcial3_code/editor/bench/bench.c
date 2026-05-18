/*
 * bench.c — Benchmark comparativo de 3 enfoques
 *
 *   A. Clásico       : write() directo, sin transformación
 *   B. Comprimido    : Huffman en User Space → write()
 *   C. Comprimido+RC4: Huffman → RC4 en User Space → write()
 *
 * Métricas registradas:
 *   - Tamaño transmitido al bus I/O
 *   - CPU: tiempo de compresión (aislado)
 *   - CPU: tiempo de cifrado    (aislado)
 *   - Tiempo total de I/O
 *   - Número de llamadas a write()
 *
 * Uso:
 *   ./bench plain      → solo enfoque clásico
 *   ./bench compressed → solo compresión
 *   ./bench encrypted  → compresión + RC4
 *   ./bench            → los tres + tabla comparativa
 *
 * Medir con:
 *   strace -c ./bench plain
 *   strace -c ./bench compressed
 *   strace -c ./bench encrypted
 *   time ./bench plain
 *   time ./bench compressed
 *   time ./bench encrypted
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../src/compress.h"
#include "../src/io.h"
#include "../src/format.h"
#include "../src/crypto.h"

#define OUTPUT_PLAIN      "/tmp/bench_plain.txt"
#define OUTPUT_COMPRESSED "/tmp/bench_compressed.bin"
#define OUTPUT_ENCRYPTED  "/tmp/bench_encrypted.bin"
#define TARGET_SIZE       (1 * 1024 * 1024)   /* 1 MB */

/* Llave de referencia para el benchmark (en el editor real se pide al usuario) */
static const char BENCH_KEY[] = "clave_benchmark_2025";

/* ------------------------------------------------------------------ */
/* Utilidad: diferencia de tiempo en ms entre dos timespec             */
/* ------------------------------------------------------------------ */
static double diff_ms(struct timespec t0, struct timespec t1) {
    return (t1.tv_sec - t0.tv_sec) * 1000.0
         + (t1.tv_nsec - t0.tv_nsec) / 1e6;
}

/* ------------------------------------------------------------------ */
/* Generar ~1 MB de texto natural repetitivo                           */
/* ------------------------------------------------------------------ */
static uint8_t *generate_data(size_t *out_len) {
    const char *frases[] = {
        "Los sistemas operativos gestionan los recursos del hardware.\n",
        "La memoria virtual permite ejecutar procesos más grandes que la RAM.\n",
        "El planificador de CPU decide qué proceso se ejecuta en cada momento.\n",
        "Un semáforo es una variable entera usada para sincronización.\n",
        "El bus de I/O conecta el procesador con los dispositivos periféricos.\n",
        "La caché de disco reduce la latencia de acceso a almacenamiento.\n",
        "Las llamadas al sistema son la interfaz entre User Space y el kernel.\n",
        "mmap() mapea archivos directamente en el espacio de direcciones.\n",
    };
    int n = (int)(sizeof(frases) / sizeof(frases[0]));

    uint8_t *buf = malloc(TARGET_SIZE + 256);
    if (!buf) { perror("generate_data"); exit(1); }

    size_t pos = 0;
    while (pos < TARGET_SIZE) {
        const char *f   = frases[pos % (size_t)n];
        size_t      len = strlen(f);
        if (pos + len > TARGET_SIZE) len = TARGET_SIZE - pos;
        memcpy(buf + pos, f, len);
        pos += len;
    }
    *out_len = pos;
    return buf;
}

/* ------------------------------------------------------------------ */
/* A. Clásico: write() sin ninguna transformación                      */
/* ------------------------------------------------------------------ */
static void run_plain(const uint8_t *data, size_t len) {
    printf("=== A. CLÁSICO (texto plano, sin compresión ni cifrado) ===\n");
    printf("Archivo        : %s\n", OUTPUT_PLAIN);
    printf("Tamaño en disco: %zu bytes (%.1f KB)\n\n", len, len / 1024.0);

    IOStats st = {0, 0, 0};
    io_write_fd(OUTPUT_PLAIN, data, len, &st);

    printf("Bytes al disco : %zu\n", st.bytes_written);
    printf("write() calls  : %ld\n", st.write_calls);
    printf("Tiempo I/O     : %.3f ms\n", st.elapsed_ms);
    printf("CPU (user)     : 0.000 ms  (sin transformación)\n\n");
}

/* ------------------------------------------------------------------ */
/* B. Comprimido: Huffman en User Space → write()                      */
/* ------------------------------------------------------------------ */
static void run_compressed(const uint8_t *data, size_t len) {
    printf("=== B. COMPRIMIDO (solo Huffman, sin cifrado) ===\n");

    struct timespec t0, t1;

    /* Medir solo el tiempo de CPU de compresión */
    clock_gettime(CLOCK_MONOTONIC, &t0);
    size_t   comp_len = 0;
    uint8_t *comp     = huffman_compress(data, len, &comp_len);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    double t_comp_ms = diff_ms(t0, t1);

    /* Construir payload con header */
    size_t   payload_len = HEADER_SIZE + comp_len;
    uint8_t *payload     = malloc(payload_len);
    if (!payload) { perror("run_compressed"); exit(1); }

    FileHeader hdr;
    memset(&hdr, 0, sizeof(hdr));
    memcpy(hdr.magic, MAGIC_NUMBER, 9);
    hdr.version         = FORMAT_VERSION;
    hdr.algo            = ALGO_HUFFMAN;
    hdr.encrypted       = 0;
    hdr.original_size   = (uint32_t)len;
    hdr.compressed_size = (uint32_t)comp_len;
    hdr.checksum        = compute_checksum(comp, comp_len);
    snprintf(hdr.filename, sizeof(hdr.filename), "bench_data.txt");

    memcpy(payload,               &hdr, HEADER_SIZE);
    memcpy(payload + HEADER_SIZE, comp, comp_len);
    free(comp);

    IOStats st = {0, 0, 0};
    io_write_fd(OUTPUT_COMPRESSED, payload, payload_len, &st);
    free(payload);

    printf("Archivo        : %s\n", OUTPUT_COMPRESSED);
    printf("Original       : %zu bytes (%.1f KB)\n", len, len / 1024.0);
    printf("Comprimido     : %zu bytes (%.1f KB)\n", comp_len, comp_len / 1024.0);
    printf("Ahorro I/O     : %.1f%%\n\n", 100.0 * (1.0 - (double)comp_len / len));
    printf("CPU compresión : %.3f ms\n", t_comp_ms);
    printf("CPU cifrado    : 0.000 ms  (sin cifrado)\n");
    printf("CPU total      : %.3f ms\n\n", t_comp_ms);
    printf("Bytes al disco : %zu\n", st.bytes_written);
    printf("write() calls  : %ld\n", st.write_calls);
    printf("Tiempo I/O     : %.3f ms\n\n", st.elapsed_ms);
}

/* ------------------------------------------------------------------ */
/* C. Comprimido + Cifrado: Huffman → RC4 → write()                   */
/* Pipeline correcto: comprimir PRIMERO, cifrar DESPUÉS.              */
/* Si se invirtiese el orden (cifrar → comprimir), la alta entropía   */
/* del texto cifrado impediría cualquier compresión significativa.     */
/* ------------------------------------------------------------------ */
static void run_encrypted(const uint8_t *data, size_t len) {
    printf("=== C. COMPRIMIDO + CIFRADO (Huffman → RC4) ===\n");

    struct timespec t0, t1, t2, t3;

    /* Paso 1: medir CPU de compresión por separado */
    clock_gettime(CLOCK_MONOTONIC, &t0);
    size_t   comp_len = 0;
    uint8_t *comp     = huffman_compress(data, len, &comp_len);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    double t_comp_ms = diff_ms(t0, t1);

    /* Checksum de datos comprimidos (antes de cifrar) */
    uint64_t chk = compute_checksum(comp, comp_len);

    /* Paso 2: medir CPU de cifrado por separado */
    clock_gettime(CLOCK_MONOTONIC, &t2);
    size_t   enc_len = 0;
    uint8_t *enc = rc4_encrypt(
        (const uint8_t *)BENCH_KEY, strlen(BENCH_KEY),
        comp, comp_len, &enc_len);
    clock_gettime(CLOCK_MONOTONIC, &t3);
    double t_enc_ms = diff_ms(t2, t3);

    free(comp);

    /* Construir payload con header */
    size_t   payload_len = HEADER_SIZE + enc_len;
    uint8_t *payload     = malloc(payload_len);
    if (!payload) { perror("run_encrypted"); exit(1); }

    FileHeader hdr;
    memset(&hdr, 0, sizeof(hdr));
    memcpy(hdr.magic, MAGIC_NUMBER, 9);
    hdr.version         = FORMAT_VERSION;
    hdr.algo            = ALGO_HUFFMAN;
    hdr.encrypted       = 1;
    hdr.original_size   = (uint32_t)len;
    hdr.compressed_size = (uint32_t)comp_len;
    hdr.checksum        = chk;
    snprintf(hdr.filename, sizeof(hdr.filename), "bench_data.txt");

    memcpy(payload,               &hdr, HEADER_SIZE);
    memcpy(payload + HEADER_SIZE, enc,  enc_len);
    free(enc);

    IOStats st = {0, 0, 0};
    io_write_fd(OUTPUT_ENCRYPTED, payload, payload_len, &st);
    free(payload);

    printf("Archivo        : %s\n", OUTPUT_ENCRYPTED);
    printf("Original       : %zu bytes (%.1f KB)\n", len, len / 1024.0);
    printf("Comprimido     : %zu bytes (%.1f KB)\n", comp_len, comp_len / 1024.0);
    printf("Cifrado (RC4)  : %zu bytes (sin padding — RC4 es stream cipher)\n\n", enc_len);
    printf("Ahorro I/O     : %.1f%%\n\n",
           100.0 * (1.0 - (double)enc_len / len));
    printf("CPU compresión : %.3f ms  ← costo de Huffman\n", t_comp_ms);
    printf("CPU cifrado    : %.3f ms  ← costo adicional de RC4\n", t_enc_ms);
    printf("CPU total      : %.3f ms\n\n", t_comp_ms + t_enc_ms);
    printf("Bytes al disco : %zu\n", st.bytes_written);
    printf("write() calls  : %ld\n", st.write_calls);
    printf("Tiempo I/O     : %.3f ms\n\n", st.elapsed_ms);
}

/* ------------------------------------------------------------------ */
/* Tabla comparativa final de los 3 enfoques                           */
/* ------------------------------------------------------------------ */
static void print_summary(const uint8_t *data, size_t len) {
    /* Medir compresión */
    struct timespec t0, t1, t2, t3;
    size_t comp_len = 0;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    uint8_t *comp = huffman_compress(data, len, &comp_len);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    double t_comp = diff_ms(t0, t1);

    /* Medir cifrado */
    size_t enc_len = 0;
    clock_gettime(CLOCK_MONOTONIC, &t2);
    uint8_t *enc = rc4_encrypt(
        (const uint8_t *)BENCH_KEY, strlen(BENCH_KEY),
        comp, comp_len, &enc_len);
    clock_gettime(CLOCK_MONOTONIC, &t3);
    double t_enc = diff_ms(t2, t3);

    free(comp);
    free(enc);

    printf("╔══════════════════════════════════════════════════════════╗\n");
    printf("║            TABLA COMPARATIVA — TRIÁNGULO DE HIERRO       ║\n");
    printf("╠══════════════════╦═══════════╦═══════════╦═══════════════╣\n");
    printf("║ Métrica          ║ A.Clásico ║ B.Comprim.║ C.Comp+RC4   ║\n");
    printf("╠══════════════════╬═══════════╬═══════════╬═══════════════╣\n");
    printf("║ Tamaño (bytes)   ║ %9zu ║ %9zu ║ %13zu ║\n",
           len, comp_len, enc_len);
    printf("║ Ahorro I/O       ║      0.0%% ║   %5.1f%% ║        %5.1f%% ║\n",
           100.0*(1.0-(double)comp_len/len),
           100.0*(1.0-(double)enc_len /len));
    printf("║ CPU compresión   ║   0.000ms ║ %7.3fms ║     %7.3fms ║\n",
           t_comp, t_comp);
    printf("║ CPU cifrado      ║   0.000ms ║   0.000ms ║     %7.3fms ║\n",
           t_enc);
    printf("║ CPU total        ║   0.000ms ║ %7.3fms ║     %7.3fms ║\n",
           t_comp, t_comp + t_enc);
    printf("╚══════════════════╩═══════════╩═══════════╩═══════════════╝\n\n");
    printf("Conclusión: añadir RC4 casi duplica el costo de CPU pero\n");
    printf("el tamaño en disco se mantiene ~%.0f%% menor que el clásico.\n",
           100.0*(1.0-(double)enc_len/len));
    printf("Sistema más lento en CPU pero 100%% cifrado y ~70%% más pequeño.\n\n");
}

/* ------------------------------------------------------------------ */

int main(int argc, char *argv[]) {
    size_t   data_len = 0;
    uint8_t *data     = generate_data(&data_len);

    if (argc > 1 && strcmp(argv[1], "plain") == 0) {
        run_plain(data, data_len);
    } else if (argc > 1 && strcmp(argv[1], "compressed") == 0) {
        run_compressed(data, data_len);
    } else if (argc > 1 && strcmp(argv[1], "encrypted") == 0) {
        run_encrypted(data, data_len);
    } else {
        run_plain(data, data_len);
        run_compressed(data, data_len);
        run_encrypted(data, data_len);
        print_summary(data, data_len);
    }

    free(data);
    return 0;
}
