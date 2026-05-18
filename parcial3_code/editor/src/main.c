#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

/* Wrapper para silenciar -Wunused-result en write() de archivos de muestra */
static void xwrite(int fd, const void *buf, size_t n) {
    ssize_t r = write(fd, buf, n); (void)r;
}

#include "buffer.h"
#include "compress.h"
#include "io.h"
#include "format.h"
#include "crypto.h"

/* ------------------------------------------------------------------ */
/* Estado global de la sesión                                           */
/* ------------------------------------------------------------------ */

static GapBuffer *g_buf      = NULL;   /* documento en memoria        */
static char       g_filepath[512] = ""; /* ruta del archivo actual   */

/* ------------------------------------------------------------------ */
/* Colores ANSI                                                         */
/* ------------------------------------------------------------------ */
#define C_RESET  "\033[0m"
#define C_BOLD   "\033[1m"
#define C_CYAN   "\033[36m"
#define C_GREEN  "\033[32m"
#define C_YELLOW "\033[33m"
#define C_RED    "\033[31m"

/* ------------------------------------------------------------------ */
/* Auxiliares                                                           */
/* ------------------------------------------------------------------ */

static void clear_screen(void) { printf("\033[2J\033[H"); }

static void print_header(void) {
    printf(C_CYAN C_BOLD);
    printf("╔══════════════════════════════════════════════════╗\n");
    printf("║     EDITOR DE TEXTO — Optimización de Bus I/O    ║\n");
    printf("║         Gap Buffer  |  RLE / Huffman / LZ77      ║\n");
    printf("╚══════════════════════════════════════════════════╝\n");
    printf(C_RESET);
    if (g_filepath[0])
        printf("  Archivo: " C_YELLOW "%s" C_RESET
               "  |  Tamaño: " C_GREEN "%zu bytes" C_RESET "\n\n",
               g_filepath, gap_buffer_length(g_buf));
    else
        printf("  Archivo: " C_RED "(ninguno cargado)" C_RESET "\n\n");
}

static void print_menu(void) {
    printf("  [1] Cargar archivo de texto\n");
    printf("  [2] Editar documento (Gap Buffer)\n");
    printf("  [3] Guardar con RLE\n");
    printf("  [4] Guardar con Huffman\n");
    printf("  [5] Guardar con LZ77\n");
    printf("  [6] Abrir archivo comprimido / cifrado\n");
    printf("  [7] Comparar los 3 algoritmos\n");
    printf("  [8] Comparar write() vs mmap()\n");
    printf("  [9] Crear archivo de prueba\n");
    printf("  " C_CYAN "[a]" C_RESET " Guardar con RLE + RC4\n");
    printf("  " C_CYAN "[b]" C_RESET " Guardar con Huffman + RC4\n");
    printf("  " C_CYAN "[c]" C_RESET " Guardar con LZ77 + RC4\n");
    printf("  [i] Info del Gap Buffer\n");
    printf("  [0] Salir\n\n");
    printf("  Opción: ");
}

/* ------------------------------------------------------------------ */
/* 1. Cargar archivo de texto plano                                     */
/* ------------------------------------------------------------------ */

static void cmd_load(void) {
    char path[512];
    printf("  Ruta del archivo: ");
    if (!fgets(path, sizeof(path), stdin)) return;
    path[strcspn(path, "\n")] = '\0';

    size_t   len  = 0;
    IOStats  st   = {0, 0, 0};
    uint8_t *data = io_read_fd(path, &len, &st);
    if (!data) { printf(C_RED "  Error leyendo el archivo.\n" C_RESET); return; }

    gap_buffer_free(g_buf);
    g_buf = gap_buffer_new();
    gap_buffer_insert_str(g_buf, (char *)data, len);
    snprintf(g_filepath, sizeof(g_filepath), "%s", path);
    free(data);

    printf(C_GREEN "  Cargado: %zu bytes en %.3f ms\n" C_RESET, len, st.elapsed_ms);
}

/* ------------------------------------------------------------------ */
/* 2. Editor de texto con Gap Buffer                                    */
/* ------------------------------------------------------------------ */

static void cmd_edit(void) {
    printf("\n" C_BOLD "  === MODO EDITOR ===" C_RESET "\n");
    printf("  Comandos: [a] agregar texto  [d] borrar último char\n");
    printf("            [l] mover cursor ←  [r] mover cursor →\n");
    printf("            [p] ver contenido   [q] salir del editor\n\n");

    char line[1024];
    while (1) {
        printf("  editor> ");
        if (!fgets(line, sizeof(line), stdin)) break;
        line[strcspn(line, "\n")] = '\0';

        if (line[0] == 'q') break;

        else if (line[0] == 'a') {
            const char *texto = line + 2;
            gap_buffer_insert_str(g_buf, texto, strlen(texto));
            gap_buffer_insert(g_buf, '\n');
            printf(C_GREEN "  Insertado.\n" C_RESET);
        }
        else if (line[0] == 'd') {
            gap_buffer_delete(g_buf);
            printf(C_GREEN "  Carácter eliminado.\n" C_RESET);
        }
        else if (line[0] == 'l') {
            gap_buffer_move_left(g_buf);
            printf("  Cursor en posición %zu\n", gap_buffer_cursor(g_buf));
        }
        else if (line[0] == 'r') {
            gap_buffer_move_right(g_buf);
            printf("  Cursor en posición %zu\n", gap_buffer_cursor(g_buf));
        }
        else if (line[0] == 'p') {
            char *content = gap_buffer_get_content(g_buf);
            printf("\n--- contenido (%zu bytes) ---\n", gap_buffer_length(g_buf));
            printf("%s", content);
            if (gap_buffer_length(g_buf) > 0 &&
                content[gap_buffer_length(g_buf)-1] != '\n') printf("\n");
            printf("----------------------------\n");
            free(content);
        }
        else {
            printf("  Comando desconocido.\n");
        }
    }
}

/* ------------------------------------------------------------------ */
/* 3-5 / a-c. Guardar con compresión (+ cifrado RC4 opcional)          */
/* Pipeline: contenido → COMPRIMIR → [CIFRAR] → disco                  */
/* El orden compresión→cifrado es obligatorio: cifrar primero aumenta   */
/* la entropía y hace que el compresor no encuentre patrones.           */
/* ------------------------------------------------------------------ */

static void cmd_save(uint8_t algo, int encrypt) {
    if (gap_buffer_length(g_buf) == 0) {
        printf(C_RED "  Buffer vacío.\n" C_RESET); return;
    }

    char path[512];
    printf("  Ruta destino (.bin): ");
    if (!fgets(path, sizeof(path), stdin)) return;
    path[strcspn(path, "\n")] = '\0';

    /* Obtener contenido del Gap Buffer */
    char  *content  = gap_buffer_get_content(g_buf);
    size_t orig_len = gap_buffer_length(g_buf);

    /* Paso 1: COMPRIMIR */
    size_t   comp_len = 0;
    uint8_t *comp     = NULL;
    switch (algo) {
        case ALGO_RLE:     comp = rle_compress    ((uint8_t*)content, orig_len, &comp_len); break;
        case ALGO_HUFFMAN: comp = huffman_compress((uint8_t*)content, orig_len, &comp_len); break;
        case ALGO_LZ77:    comp = lz77_compress   ((uint8_t*)content, orig_len, &comp_len); break;
    }
    free(content);

    /* Checksum sobre datos comprimidos (antes de cifrar).
       Al descifrar podremos verificar que la llave fue correcta. */
    uint64_t chk = compute_checksum(comp, comp_len);

    /* Paso 2: CIFRAR (solo si se solicitó) */
    uint8_t *final_data = comp;
    size_t   final_len  = comp_len;

    if (encrypt) {
        char key[256];
        memset(key, 0, sizeof(key));
        printf("  Llave RC4 (no se mostrará): ");
        fflush(stdout);
        if (!fgets(key, sizeof(key), stdin)) { free(comp); return; }
        key[strcspn(key, "\n")] = '\0';

        if (strlen(key) == 0) {
            printf(C_RED "  Llave vacía — operación cancelada.\n" C_RESET);
            free(comp); return;
        }

        size_t enc_len = 0;
        uint8_t *enc = rc4_encrypt((uint8_t *)key, strlen(key),
                                   comp, comp_len, &enc_len);

        /* Borrar la llave de la RAM inmediatamente tras usarla */
        secure_erase(key, sizeof(key));

        if (!enc) { free(comp); return; }
        free(comp);

        final_data = enc;
        final_len  = enc_len;   /* RC4 no añade padding: enc_len == comp_len */
    }

    /* Construir payload = header + datos finales */
    size_t   payload_len = HEADER_SIZE + final_len;
    uint8_t *payload     = malloc(payload_len);
    if (!payload) { perror("cmd_save"); free(final_data); return; }

    FileHeader hdr;
    memset(&hdr, 0, sizeof(hdr));
    memcpy(hdr.magic, MAGIC_NUMBER, 9);
    hdr.version         = FORMAT_VERSION;
    hdr.algo            = algo;
    hdr.encrypted       = encrypt ? 1 : 0;
    hdr.original_size   = (uint32_t)orig_len;
    hdr.compressed_size = (uint32_t)comp_len;
    hdr.checksum        = chk;

    const char *base = strrchr(g_filepath, '/');
    base = base ? base + 1 : g_filepath;
    size_t blen = strlen(base);
    if (blen >= sizeof(hdr.filename)) blen = sizeof(hdr.filename) - 1;
    memset(hdr.filename, 0, sizeof(hdr.filename));
    memcpy(hdr.filename, base, blen);

    memcpy(payload,               &hdr,       HEADER_SIZE);
    memcpy(payload + HEADER_SIZE, final_data, final_len);
    free(final_data);

    /* Escribir al disco con POSIX (sin stdio) */
    IOStats st = {0, 0, 0};
    if (io_write_fd(path, payload, payload_len, &st) == 0) {
        const char *enc_tag = encrypt ? " + RC4" : "";
        printf(C_GREEN "  Guardado con %s%s\n" C_RESET, algo_name(algo), enc_tag);
        printf("  Original   : %zu bytes\n", orig_len);
        printf("  Comprimido : %zu bytes (%.1f%% ahorro)\n",
               comp_len, 100.0*(1.0-(double)comp_len/(double)orig_len));
        if (encrypt)
            printf("  Cifrado    : %zu bytes (RC4, sin padding)\n", final_len);
        printf("  Tiempo I/O : %.3f ms  |  write() calls: %ld\n",
               st.elapsed_ms, st.write_calls);
    }
    free(payload);
}

/* ------------------------------------------------------------------ */
/* 6. Abrir archivo comprimido (con descifrado RC4 si aplica)          */
/* Pipeline inverso: disco → [DESCIFRAR] → DESCOMPRIMIR → Gap Buffer   */
/* ------------------------------------------------------------------ */

static void cmd_open_compressed(void) {
    char path[512];
    printf("  Ruta del archivo comprimido: ");
    if (!fgets(path, sizeof(path), stdin)) return;
    path[strcspn(path, "\n")] = '\0';

    size_t   raw_len = 0;
    IOStats  st      = {0, 0, 0};
    uint8_t *raw     = io_read_fd(path, &raw_len, &st);
    if (!raw) { printf(C_RED "  Error leyendo el archivo.\n" C_RESET); return; }

    if (raw_len < HEADER_SIZE) {
        printf(C_RED "  Archivo muy pequeño o corrupto.\n" C_RESET);
        free(raw); return;
    }

    FileHeader hdr;
    memcpy(&hdr, raw, HEADER_SIZE);

    if (memcmp(hdr.magic, MAGIC_NUMBER, 9) != 0) {
        printf(C_RED "  Magic number inválido — no es un archivo de este editor.\n" C_RESET);
        free(raw); return;
    }

    uint8_t *disk_data = raw + HEADER_SIZE;
    size_t   disk_len  = raw_len - HEADER_SIZE;

    /* Paso 1: DESCIFRAR si el archivo está cifrado */
    uint8_t *comp_data     = disk_data;
    size_t   comp_len      = disk_len;
    uint8_t *decrypted_buf = NULL;

    if (hdr.encrypted) {
        char key[256];
        memset(key, 0, sizeof(key));
        printf("  Llave RC4: ");
        fflush(stdout);
        if (!fgets(key, sizeof(key), stdin)) { free(raw); return; }
        key[strcspn(key, "\n")] = '\0';

        size_t dec_len = 0;
        decrypted_buf = rc4_decrypt((uint8_t *)key, strlen(key),
                                    disk_data, disk_len, &dec_len);

        /* Borrar la llave de la RAM inmediatamente */
        secure_erase(key, sizeof(key));

        if (!decrypted_buf) { free(raw); return; }

        comp_data = decrypted_buf;
        comp_len  = dec_len;
    }

    /* Verificar checksum sobre datos comprimidos (post-descifrado).
       Si la llave fue incorrecta el checksum no coincidirá. */
    uint64_t chk = compute_checksum(comp_data, comp_len);
    if (chk != hdr.checksum) {
        printf(C_RED "  Checksum inválido%s\n" C_RESET,
               hdr.encrypted ? " — llave incorrecta o datos corruptos."
                              : " — datos corruptos.");
        free(decrypted_buf);
        free(raw);
        return;
    }

    /* Paso 2: DESCOMPRIMIR */
    size_t   orig_len = 0;
    uint8_t *orig     = NULL;
    switch (hdr.algo) {
        case ALGO_RLE:     orig = rle_decompress    (comp_data, comp_len, &orig_len); break;
        case ALGO_HUFFMAN: orig = huffman_decompress(comp_data, comp_len, &orig_len); break;
        case ALGO_LZ77:    orig = lz77_decompress   (comp_data, comp_len, &orig_len); break;
        default:
            printf(C_RED "  Algoritmo desconocido.\n" C_RESET);
            free(decrypted_buf); free(raw); return;
    }

    free(decrypted_buf);
    free(raw);

    /* Cargar en el Gap Buffer */
    gap_buffer_free(g_buf);
    g_buf = gap_buffer_new();
    gap_buffer_insert_str(g_buf, (char *)orig, orig_len);
    snprintf(g_filepath, sizeof(g_filepath), "%s", path);
    free(orig);

    const char *enc_tag = hdr.encrypted ? " + RC4" : "";
    printf(C_GREEN "  Descomprimido con %s%s: %u → %zu bytes\n" C_RESET,
           algo_name(hdr.algo), enc_tag, hdr.original_size, orig_len);
    printf("  Archivo original: %s\n", hdr.filename);
}

/* ------------------------------------------------------------------ */
/* 9. Crear archivo de prueba                                           */
/* ------------------------------------------------------------------ */

static void cmd_create_sample(void) {
    printf("\n  Tipo de archivo:\n");
    printf("  [1] Texto repetitivo (ideal para RLE)\n");
    printf("  [2] Texto natural (ideal para Huffman)\n");
    printf("  [3] Código fuente C (ideal para LZ77)\n");
    printf("  Opción: ");

    char opt[8];
    if (!fgets(opt, sizeof(opt), stdin)) return;

    const char *path = "samples/fuente/prueba.txt";
    int fd_out = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);  /* POSIX */
    if (fd_out < 0) {
        /* Intentar con ruta relativa simple */
        path = "prueba.txt";
        fd_out = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd_out < 0) { perror("cmd_create_sample: open"); return; }
    }

    char line[256];
    int  written_total = 0;

    if (opt[0] == '1') {
        for (int i = 0; i < 200; i++) {
            int n = snprintf(line, sizeof(line),
                             "AAAAAAAAABBBBBBBBCCCCCCCDDDDDDD %d\n", i);
            xwrite(fd_out, line, (size_t)n);
            written_total += n;
        }
    } else if (opt[0] == '2') {
        const char *frases[] = {
            "En un lugar de la Mancha, de cuyo nombre no quiero acordarme,\n",
            "no ha mucho tiempo que vivía un hidalgo de los de lanza en astillero.\n",
            "Los sistemas operativos gestionan los recursos del hardware.\n",
            "La memoria virtual permite ejecutar procesos más grandes que la RAM.\n",
            "El planificador de CPU decide qué proceso se ejecuta en cada momento.\n",
            "Un semáforo es una variable entera usada para sincronización.\n",
        };
        int n_frases = (int)(sizeof(frases) / sizeof(frases[0]));
        for (int i = 0; i < 60; i++) {
            const char *f = frases[i % n_frases];
            xwrite(fd_out, f, strlen(f));
            written_total += (int)strlen(f);
        }
    } else {
        const char *codigo[] = {
            "#include <stdio.h>\n",
            "#include <stdlib.h>\n",
            "int main(int argc, char *argv[]) {\n",
            "    for (int i = 0; i < argc; i++) {\n",
            "        printf(\"arg[%d] = %s\\n\", i, argv[i]);\n",
            "    }\n",
            "    return 0;\n",
            "}\n",
        };
        int n = (int)(sizeof(codigo) / sizeof(codigo[0]));
        for (int i = 0; i < 80; i++) {
            const char *l = codigo[i % n];
            xwrite(fd_out, l, strlen(l));
            written_total += (int)strlen(l);
        }
    }
    close(fd_out);

    /* Cargar en el buffer */
    size_t   len  = 0;
    IOStats  st   = {0, 0, 0};
    uint8_t *data = io_read_fd(path, &len, &st);
    if (data) {
        gap_buffer_free(g_buf);
        g_buf = gap_buffer_new();
        gap_buffer_insert_str(g_buf, (char *)data, len);
        snprintf(g_filepath, sizeof(g_filepath), "%s", path);
        free(data);
        printf(C_GREEN "  Archivo creado: %s (%d bytes)\n" C_RESET, path, written_total);
    }
}

/* ------------------------------------------------------------------ */
/* Punto de entrada                                                     */
/* ------------------------------------------------------------------ */

int main(void) {
    g_buf = gap_buffer_new();

    char opt[8];
    while (1) {
        clear_screen();
        print_header();
        print_menu();

        if (!fgets(opt, sizeof(opt), stdin)) break;
        opt[strcspn(opt, "\n")] = '\0';

        printf("\n");

        if      (strcmp(opt, "0") == 0) break;
        else if (strcmp(opt, "1") == 0) cmd_load();
        else if (strcmp(opt, "2") == 0) cmd_edit();
        else if (strcmp(opt, "3") == 0) cmd_save(ALGO_RLE,     0);
        else if (strcmp(opt, "4") == 0) cmd_save(ALGO_HUFFMAN, 0);
        else if (strcmp(opt, "5") == 0) cmd_save(ALGO_LZ77,    0);
        else if (strcmp(opt, "6") == 0) cmd_open_compressed();
        else if (strcmp(opt, "a") == 0) cmd_save(ALGO_RLE,     1);
        else if (strcmp(opt, "b") == 0) cmd_save(ALGO_HUFFMAN, 1);
        else if (strcmp(opt, "c") == 0) cmd_save(ALGO_LZ77,    1);
        else if (strcmp(opt, "7") == 0) {
            if (gap_buffer_length(g_buf) == 0) {
                printf(C_RED "  Buffer vacío.\n" C_RESET);
            } else {
                char *c = gap_buffer_get_content(g_buf);
                print_comparison((uint8_t *)c, gap_buffer_length(g_buf));
                free(c);
            }
        }
        else if (strcmp(opt, "8") == 0) {
            if (gap_buffer_length(g_buf) == 0) {
                printf(C_RED "  Buffer vacío.\n" C_RESET);
            } else {
                char *c = gap_buffer_get_content(g_buf);
                io_compare_methods("/tmp/editor_bench",
                                   (uint8_t *)c, gap_buffer_length(g_buf));
                free(c);
            }
        }
        else if (strcmp(opt, "9") == 0) cmd_create_sample();
        else if (strcmp(opt, "i") == 0) gap_buffer_print_info(g_buf);
        else printf(C_RED "  Opción inválida.\n" C_RESET);

        printf("\n  Presiona Enter para continuar...");
        if (!fgets(opt, sizeof(opt), stdin)) break;
    }

    gap_buffer_free(g_buf);
    printf("\n  Hasta luego.\n\n");
    return 0;
}
