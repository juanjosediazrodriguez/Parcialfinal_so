# Matriz de Diseño del Pipeline I/O

## Diagrama de Flujo — Escritura (Guardar)

```
┌─────────────────────────────────────────────────────────────────┐
│                        USER SPACE                               │
│                                                                 │
│  ┌──────────┐    ┌─────────────────────────────────────────┐   │
│  │  Usuario │    │           Gap Buffer (RAM)               │   │
│  │  escribe │───▶│  [texto antes][  GAP  ][texto después]  │   │
│  │  texto   │    │   O(1) amortizado para inserción         │   │
│  └──────────┘    └────────────────┬────────────────────────┘   │
│                                   │ gap_buffer_get_content()    │
│                                   ▼                             │
│                  ┌────────────────────────────────────────┐    │
│                  │       Algoritmo de Compresión           │    │
│                  │                                         │    │
│                  │   ┌─────────┐ ┌─────────┐ ┌────────┐  │    │
│                  │   │   RLE   │ │ Huffman │ │  LZ77  │  │    │
│                  │   │ O(n)    │ │O(n log k)│ │ O(n·W) │  │    │
│                  │   └─────────┘ └─────────┘ └────────┘  │    │
│                  │                                         │    │
│                  │  Input:  texto plano  (N bytes)         │    │
│                  │  Output: datos comprimidos (M bytes)    │    │
│                  │          donde M < N en la mayoría      │    │
│                  └────────────────┬───────────────────────┘    │
│                                   │                             │
│                  ┌────────────────▼───────────────────────┐    │
│                  │         Header Binario (64 bytes)       │    │
│                  │   __attribute__((packed))               │    │
│                  │                                         │    │
│                  │  [magic:9][ver:1][algo:1][res:5]        │    │
│                  │  [orig_size:4][comp_size:4]             │    │
│                  │  [checksum:8][filename:32]              │    │
│                  │  ──────────────────────────             │    │
│                  │  Total exacto: 64 bytes                 │    │
│                  └────────────────┬───────────────────────┘    │
│                                   │ payload = header + datos    │
│                                   ▼                             │
│                  ┌────────────────────────────────────────┐    │
│                  │          Capa de I/O POSIX              │    │
│                  │                                         │    │
│                  │  open(path, O_WRONLY|O_CREAT, 0644)     │    │
│                  │                                         │    │
│                  │  Buffer alineado: PAGE_SIZE = 4096 B    │    │
│                  │  ┌────┐┌────┐┌────┐       ┌────┐       │    │
│                  │  │4KB ││4KB ││4KB │  ...  │<4KB│       │    │
│                  │  └──┬─┘└──┬─┘└──┬─┘       └──┬─┘       │    │
│                  │     │     │     │             │         │    │
│                  │   write() write() write()  write()      │    │
│                  └─────┬─────┬─────┬─────────────┬────────┘    │
│                        │     │     │             │             │
└────────────────────────┼─────┼─────┼─────────────┼────────────┘
                         │     │     │             │
         ════════════════╪═════╪═════╪═════════════╪════ syscall boundary
                         │     │     │             │
┌────────────────────────┼─────┼─────┼─────────────┼────────────┐
│                KERNEL SPACE  │     │             │            │
│                         │     │     │             │            │
│                  ┌──────▼─────▼─────▼─────────────▼──────┐    │
│                  │         VFS (Virtual File System)       │    │
│                  │   Capa de abstracción del SO            │    │
│                  └────────────────┬───────────────────────┘    │
│                                   │                             │
│                  ┌────────────────▼───────────────────────┐    │
│                  │          Page Cache (buffer del SO)     │    │
│                  │   Páginas de 4KB en RAM gestionadas     │    │
│                  │   por el kernel                         │    │
│                  └────────────────┬───────────────────────┘    │
│                                   │ flush / writeback           │
└───────────────────────────────────┼────────────────────────────┘
                                    │
                    ┌───────────────▼──────────────┐
                    │         DISCO FÍSICO          │
                    │   Archivo .bin comprimido     │
                    │   (M bytes en lugar de N)     │
                    └──────────────────────────────┘
```

---

## Diagrama de Flujo — Lectura (Abrir archivo comprimido)

```
┌──────────────────────────────────────────────────────┐
│                    DISCO FÍSICO                       │
│            Archivo .bin (M bytes)                     │
└─────────────────────┬────────────────────────────────┘
                      │
         ═════════════╪═══ syscall boundary
                      │
┌─────────────────────▼────────────────────────────────┐
│                  KERNEL SPACE                         │
│    open() + fstat() → tamaño del archivo              │
│    read() en bloques de PAGE_SIZE (4096 bytes)        │
└─────────────────────┬────────────────────────────────┘
                      │
┌─────────────────────▼────────────────────────────────┐
│                  USER SPACE                           │
│                                                       │
│  1. Leer y validar FileHeader (64 bytes)              │
│     ├── Verificar magic: "EDITORCMP"                  │
│     ├── Verificar checksum FNV-1a                     │
│     └── Identificar algoritmo (RLE/Huffman/LZ77)      │
│                                                       │
│  2. Descomprimir payload                              │
│     ├── rle_decompress()                              │
│     ├── huffman_decompress()  ← reconstruye árbol     │
│     └── lz77_decompress()    ← recorre ventana        │
│                                                       │
│  3. Cargar en Gap Buffer                              │
│     gap_buffer_insert_str(gb, content, len)           │
│                                                       │
└──────────────────────────────────────────────────────┘
```

---

## Comparación: write() vs mmap()

```
  ENFOQUE write()                    ENFOQUE mmap()
  ──────────────────────────         ──────────────────────────
  User Space                         User Space
  ┌──────────┐                       ┌──────────┐
  │  buffer  │──write()──▶ kernel    │  buffer  │
  │  4096 B  │  (syscall)            └──────────┘
  └──────────┘                            │ memcpy()
       │ (repetir N/4096 veces)           │ (sin syscall)
       ▼                                  ▼
  Kernel Space                       Espacio de addr. mapeado
  ┌──────────┐                       ┌─────────────────────┐
  │  VFS     │                       │  mmap() region      │
  │  cache   │                       │  (archivo en RAM)   │
  └──────────┘                       └──────────┬──────────┘
       │                                         │ msync() (1 llamada)
       ▼                                         ▼
    Disco                                     Disco

  N/4096 syscalls                    1 syscall (msync)
  Más context switches               Menos overhead de kernel
  Mejor para escrituras pequeñas     Mejor para archivos grandes
```

---

## Justificación del tamaño de buffer: PAGE_SIZE = 4096 bytes

El sistema operativo gestiona la memoria en páginas de **4 KB**.
Usar un buffer de exactamente ese tamaño garantiza que:

1. Cada `write()` llena exactamente **una página** en la Page Cache del kernel.
2. No hay desperdicio por padding ni fragmentación interna.
3. La alineación coincide con la granularidad de `mmap()`.
4. Se minimizan los **context switches** al reducir la frecuencia de syscalls.

```
  Datos de 1 MB con buffer de 4 KB → 256 write() calls
  Datos de 1 MB con buffer de 1 B  → 1,048,576 write() calls  (×4096 peor)
  Datos de 1 MB con buffer de 8 KB → 128 write() calls        (×0.5 mejor, pero fuera de página)
```
