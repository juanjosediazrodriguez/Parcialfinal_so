#ifndef BUFFER_H
#define BUFFER_H

#include <stddef.h>

#define GAP_INITIAL  4096   /* tamaño inicial del gap */
#define GAP_GROW     4096   /* incremento al crecer   */

/*
 * Gap Buffer: estructura clásica de editores de texto (Emacs, Vim).
 *
 *  [ texto antes del cursor ][ GAP ][ texto después del cursor ]
 *  0                    gap_start  gap_end                   total
 *
 * Insertar es O(1) amortizado: escribir en gap_start y avanzarlo.
 * Mover el cursor es O(d) donde d es la distancia.
 */
typedef struct {
    char   *data;       /* array completo del buffer */
    size_t  gap_start;  /* inicio del gap = posición del cursor */
    size_t  gap_end;    /* fin del gap (exclusivo)              */
    size_t  total;      /* capacidad total de data[]            */
} GapBuffer;

GapBuffer *gap_buffer_new(void);
void       gap_buffer_free(GapBuffer *gb);

void       gap_buffer_insert(GapBuffer *gb, char c);
void       gap_buffer_insert_str(GapBuffer *gb, const char *str, size_t len);
void       gap_buffer_delete(GapBuffer *gb);     /* backspace  */
void       gap_buffer_move_left(GapBuffer *gb);
void       gap_buffer_move_right(GapBuffer *gb);
void       gap_buffer_move_to(GapBuffer *gb, size_t pos);

size_t     gap_buffer_length(const GapBuffer *gb);
size_t     gap_buffer_cursor(const GapBuffer *gb);

/* Devuelve copia del contenido (sin el gap). El llamador debe liberar. */
char      *gap_buffer_get_content(const GapBuffer *gb);

void       gap_buffer_print_info(const GapBuffer *gb);

#endif /* BUFFER_H */
