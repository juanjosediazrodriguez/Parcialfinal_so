# Gestión de Memoria en C

## 1. Diseño de Estructuras (`struct`)

### 1.1 FileHeader — `__attribute__((packed))`

```c
typedef struct __attribute__((packed)) {
    char     magic[9];           /*  9 bytes */
    uint8_t  version;            /*  1 byte  */
    uint8_t  algo;               /*  1 byte  */
    uint8_t  reserved[5];        /*  5 bytes */
    uint32_t original_size;      /*  4 bytes */
    uint32_t compressed_size;    /*  4 bytes */
    uint64_t checksum;           /*  8 bytes */
    char     filename[32];       /* 32 bytes */
} FileHeader;                    /* = 64 bytes EXACTOS */
```

**¿Por qué `__attribute__((packed))`?**

Sin `packed`, el compilador agrega **padding** para alinear campos al
tamaño de su tipo:

```
SIN packed (layout con padding):
  [magic:9][pad:3][version:1][algo:1][pad:2][reserved:5][pad:3]
  [original_size:4][compressed_size:4][checksum:8][filename:32]
  → Tamaño real: ~80 bytes (diferente en cada compilador/arquitectura)

CON __attribute__((packed)):
  [magic:9][version:1][algo:1][reserved:5][original_size:4]
  [compressed_size:4][checksum:8][filename:32]
  → Tamaño garantizado: 64 bytes en cualquier plataforma
```

Esto es **crítico** para un formato de archivo binario: si dos programas
compilados diferente leen el mismo archivo, deben interpretar los bytes
en las mismas posiciones.

**Verificación en tiempo de compilación:**
```c
typedef char _check_FileHeader_size[(sizeof(FileHeader) == 64) ? 1 : -1];
/* Si sizeof != 64, el compilador reporta: array de tamaño -1 → error */
```

---

### 1.2 GapBuffer — Edición eficiente de texto

```c
typedef struct {
    char   *data;       /* array dinámico en el heap  */
    size_t  gap_start;  /* inicio del gap = cursor    */
    size_t  gap_end;    /* fin del gap (exclusivo)    */
    size_t  total;      /* capacidad total de data[]  */
} GapBuffer;
```

**Layout en memoria:**

```
data[] en el heap:
┌──────────────────┬────────────────┬──────────────────────┐
│  texto antes del │      GAP       │  texto después del   │
│     cursor       │  (espacio libre│      cursor          │
│                  │   para insertar│                      │
└──────────────────┴────────────────┴──────────────────────┘
0             gap_start         gap_end                 total

Longitud del texto = total - (gap_end - gap_start)
```

**¿Por qué no usar `char *` simple o `std::string`?**

| Operación        | Array simple | Lista enlazada | Gap Buffer    |
|------------------|-------------|----------------|---------------|
| Insertar en medio| O(n)        | O(1) + O(n) pos| O(1) amort.   |
| Borrar en medio  | O(n)        | O(1) + O(n) pos| O(1) amort.   |
| Acceso aleatorio | O(1)        | O(n)           | O(1)          |
| Localidad cache  | Excelente   | Mala           | Excelente     |

El Gap Buffer es la estructura que usan **Emacs y la mayoría de editores
de texto** en producción por esta razón.

**Política de crecimiento:**
```c
#define GAP_INITIAL  4096   /* alineado a PAGE_SIZE */
#define GAP_GROW     4096   /* mismo incremento     */
```
Cuando el gap se agota (`gap_start == gap_end`), se llama a `realloc()`
para añadir `GAP_GROW` bytes. Crecer en múltiplos de PAGE_SIZE asegura
que el kernel asigne páginas completas sin fragmentación.

---

### 1.3 HuffNode — Árbol de Huffman en array

```c
typedef struct {
    uint32_t freq;     /* frecuencia del símbolo o subárbol */
    int      left;     /* índice en nodes[], -1 si es hoja  */
    int      right;    /* índice en nodes[], -1 si es hoja  */
    uint16_t symbol;   /* byte (0-255) si es hoja           */
} HuffNode;

HuffNode nodes[HUFF_MAX_NODES];  /* HUFF_MAX_NODES = 511 */
```

**¿Por qué un array en lugar de nodos dinámicos (`malloc` por nodo)?**

```
DINÁMICO (malloc por nodo):
  511 llamadas a malloc()          → 511 system calls overhead
  511 llamadas a free()            → fragmentación del heap
  Punteros por toda la memoria     → mala localidad de caché

ARRAY ESTÁTICO EN STACK/BSS:
  1 sola reserva (automática)      → 0 overhead de allocación
  0 llamadas a free()              → sin fragmentación
  Acceso secuencial                → cache-friendly
  Índices int16_t en lugar de ptr  → 2 bytes vs 8 bytes (×4 menos memoria)
```

El árbol completo para 256 símbolos tiene a lo sumo **511 nodos**
(256 hojas + 255 nodos internos), por lo que el tamaño máximo es conocido
y fijo en tiempo de compilación.

---

## 2. Optimización para Evitar Padding Innecesario

### Regla general: ordenar campos de mayor a menor tamaño

```c
/* MAL — padding implícito del compilador */
struct Malo {
    char    a;       /* 1 byte  + 7 bytes padding */
    uint64_t b;      /* 8 bytes                   */
    char    c;       /* 1 byte  + 7 bytes padding */
};
/* sizeof(Malo) = 24 bytes */

/* BIEN — sin padding */
struct Bueno {
    uint64_t b;      /* 8 bytes */
    char     a;      /* 1 byte  */
    char     c;      /* 1 byte  */
                     /* 6 bytes padding al final — solo uno */
};
/* sizeof(Bueno) = 16 bytes */
```

### En este proyecto

El `FileHeader` usa `__attribute__((packed))` para **eliminar todo padding**,
garantizando el layout exacto en el archivo binario.

El `GapBuffer` no necesita `packed` porque no se escribe directamente
a disco — es una estructura interna de runtime donde el padding no afecta
la corrección ni la interoperabilidad.

El `HuffNode` usa `int` para `left`/`right` (índices de 4 bytes)
en lugar de punteros (8 bytes en x86_64), reduciendo el tamaño del array
de nodos en un 25%.

---

## 3. Estrategias para Evitar Fugas de Memoria

### 3.1 Propiedad clara de cada buffer

Cada función que retorna un puntero a memoria dinámica documenta
que **el llamador es responsable de liberarlo**:

```c
/* El llamador DEBE llamar free() sobre el puntero retornado */
char    *gap_buffer_get_content(const GapBuffer *gb);
uint8_t *rle_compress(const uint8_t *in, size_t in_len, size_t *out_len);
uint8_t *huffman_compress(...);
uint8_t *lz77_compress(...);
uint8_t *io_read_fd(...);
uint8_t *io_read_mmap(...);
```

### 3.2 Patrón malloc → use → free en cada función

```c
/* Ejemplo en cmd_save() de main.c */
char  *content = gap_buffer_get_content(g_buf);   /* malloc interno */
/* ... usar content ... */
free(content);                                      /* liberar aquí  */

uint8_t *comp = huffman_compress(...);              /* malloc interno */
/* ... construir payload ... */
free(comp);                                         /* liberar aquí  */

uint8_t *payload = malloc(payload_len);
/* ... escribir al disco ... */
free(payload);                                      /* liberar aquí  */
```

### 3.3 Liberación en el destructor del Gap Buffer

```c
void gap_buffer_free(GapBuffer *gb) {
    if (!gb) return;
    free(gb->data);   /* liberar el array de contenido */
    free(gb);         /* liberar la estructura misma   */
}
```

Se llama **una sola vez** al final de `main()`:
```c
gap_buffer_free(g_buf);
```

### 3.4 Verificación con Valgrind

```bash
make valgrind
```

Resultado obtenido:
```
HEAP SUMMARY:
    in use at exit: 0 bytes in 0 blocks
  total heap usage: 8 allocs, 8 frees, 31,731 bytes allocated

All heap blocks were freed -- no leaks are possible
ERROR SUMMARY: 0 errors from 0 contexts
```

**Interpretación:**
- `8 allocs` y `8 frees` — perfecta simetría, sin fugas.
- `0 errors` — sin accesos inválidos, sin lecturas de memoria sin inicializar.

---

## 4. Resumen de gestión de memoria por módulo

| Módulo       | Estructura        | Allocación     | Liberación          |
|--------------|-------------------|----------------|---------------------|
| `buffer.c`   | `GapBuffer`       | `malloc`×2     | `gap_buffer_free()` |
| `buffer.c`   | `data[]` (grow)   | `realloc`      | dentro de `free()`  |
| `compress.c` | buffers de salida | `malloc/realloc`| llamador (`free()`) |
| `compress.c` | `BitWriter.data`  | `calloc`       | dentro de `huffman_compress()` |
| `compress.c` | `HuffNode nodes[]`| stack (array)  | automática          |
| `io.c`       | buffer de lectura | `malloc`       | llamador (`free()`) |
| `format.c`   | —                 | ninguna        | —                   |
