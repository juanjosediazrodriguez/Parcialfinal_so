#include "buffer.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ------------------------------------------------------------------ */
/* Internas                                                             */
/* ------------------------------------------------------------------ */

static void gap_grow(GapBuffer *gb) {
    size_t gap_size = gb->gap_end - gb->gap_start;
    if (gap_size > 0) return;   /* gap todavía tiene espacio */

    size_t new_total = gb->total + GAP_GROW;
    char  *new_data  = realloc(gb->data, new_total);
    if (!new_data) {
        perror("gap_grow: realloc");
        exit(1);
    }

    /* Mover el texto que está después del gap para abrir espacio */
    size_t after_len = gb->total - gb->gap_end;
    memmove(new_data + gb->gap_end + GAP_GROW,
            new_data + gb->gap_end,
            after_len);

    gb->data    = new_data;
    gb->gap_end = gb->gap_start + GAP_GROW;
    gb->total   = new_total;
}

/* ------------------------------------------------------------------ */
/* API pública                                                          */
/* ------------------------------------------------------------------ */

GapBuffer *gap_buffer_new(void) {
    GapBuffer *gb = malloc(sizeof(GapBuffer));
    if (!gb) { perror("gap_buffer_new"); exit(1); }

    gb->data      = malloc(GAP_INITIAL);
    if (!gb->data) { perror("gap_buffer_new data"); exit(1); }

    gb->gap_start = 0;
    gb->gap_end   = GAP_INITIAL;
    gb->total     = GAP_INITIAL;
    return gb;
}

void gap_buffer_free(GapBuffer *gb) {
    if (!gb) return;
    free(gb->data);
    free(gb);
}

void gap_buffer_insert(GapBuffer *gb, char c) {
    gap_grow(gb);
    gb->data[gb->gap_start++] = c;
}

void gap_buffer_insert_str(GapBuffer *gb, const char *str, size_t len) {
    for (size_t i = 0; i < len; i++)
        gap_buffer_insert(gb, str[i]);
}

/* Backspace: elimina el carácter antes del cursor */
void gap_buffer_delete(GapBuffer *gb) {
    if (gb->gap_start == 0) return;
    gb->gap_start--;
}

void gap_buffer_move_left(GapBuffer *gb) {
    if (gb->gap_start == 0) return;
    /* Mueve el carácter previo al gap hacia el lado derecho del gap */
    gb->gap_end--;
    gb->data[gb->gap_end] = gb->data[gb->gap_start - 1];
    gb->gap_start--;
}

void gap_buffer_move_right(GapBuffer *gb) {
    if (gb->gap_end == gb->total) return;
    /* Mueve el carácter siguiente al gap hacia el lado izquierdo */
    gb->data[gb->gap_start] = gb->data[gb->gap_end];
    gb->gap_start++;
    gb->gap_end++;
}

void gap_buffer_move_to(GapBuffer *gb, size_t pos) {
    size_t cur = gb->gap_start;
    while (cur < pos) { gap_buffer_move_right(gb); cur++; }
    while (cur > pos) { gap_buffer_move_left(gb);  cur--; }
}

size_t gap_buffer_length(const GapBuffer *gb) {
    return gb->total - (gb->gap_end - gb->gap_start);
}

size_t gap_buffer_cursor(const GapBuffer *gb) {
    return gb->gap_start;
}

char *gap_buffer_get_content(const GapBuffer *gb) {
    size_t len    = gap_buffer_length(gb);
    char  *result = malloc(len + 1);
    if (!result) { perror("gap_buffer_get_content"); exit(1); }

    /* Copiar la parte antes del gap */
    memcpy(result, gb->data, gb->gap_start);

    /* Copiar la parte después del gap */
    size_t after_len = gb->total - gb->gap_end;
    memcpy(result + gb->gap_start, gb->data + gb->gap_end, after_len);

    result[len] = '\0';
    return result;
}

void gap_buffer_print_info(const GapBuffer *gb) {
    printf("\n=== Gap Buffer Info ===\n");
    printf("  Capacidad total : %zu bytes\n", gb->total);
    printf("  Texto (longitud): %zu bytes\n", gap_buffer_length(gb));
    printf("  Cursor          : posición %zu\n", gb->gap_start);
    printf("  Gap             : [%zu, %zu) = %zu bytes libres\n",
           gb->gap_start, gb->gap_end, gb->gap_end - gb->gap_start);
    printf("========================\n\n");
}
