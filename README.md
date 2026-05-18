# Editor de Texto — Reto Final: El Triángulo de Hierro

> **Parcial Final — Sistemas Operativos** | C11 · Linux · WSL2  
> Compresión + Cifrado RC4 · Gestión segura de llaves · POSIX I/O · Gap Buffer · Profiling con `strace`

## Equipo

- Juan Jose Diaz
- Jose Miguel Sanchez
- Samuel Granados

---

## Descripción

Editor de texto en C nativo para Linux que implementa un **pipeline de memoria en dos etapas** antes de escribir al disco:

```
Contenido → [COMPRIMIR] → [CIFRAR con RC4] → Disco
```

El orden de las transformaciones es **crítico e irremplazable**: cifrar primero genera ruido de alta entropía, haciendo imposible la compresión posterior. Comprimir primero aprovecha los patrones del texto original y luego el cifrado protege los datos en reposo sin destruir el ahorro de espacio.

---

## Tabla de Contenidos

1. [Estructura del proyecto](#estructura-del-proyecto)
2. [Compilar y ejecutar](#compilar-y-ejecutar)
3. [Uso del editor](#uso-del-editor)
4. [Arquitectura del pipeline](#arquitectura-del-pipeline)
5. [Criptografía RC4 y seguridad de llaves](#criptografía-rc4-y-seguridad-de-llaves)
6. [Formato de archivo binario](#formato-de-archivo-binario)
7. [Módulos del sistema](#módulos-del-sistema)
8. [Benchmark y resultados](#benchmark-y-resultados)
9. [Gestión de memoria](#gestión-de-memoria)
10. [Preguntas de sustentación](#preguntas-de-sustentación)

---

## Estructura del proyecto

```
editor/
├── Makefile
├── src/
│   ├── main.c        ← TUI principal, pipeline guardar/abrir
│   ├── buffer.h/c    ← Gap Buffer — estructura de edición O(1)
│   ├── compress.h/c  ← RLE · Huffman · LZ77
│   ├── crypto.h/c    ← RC4 + secure_erase (cifrado simétrico)
│   ├── io.h/c        ← POSIX I/O: open/write (4 KB) y mmap
│   └── format.h/c    ← FileHeader binario de 64 bytes (packed)
├── bench/
│   └── bench.c       ← Benchmark A/B/C con timers aislados por etapa
├── samples/
│   └── fuente/prueba.txt
└── doc/
    ├── pipeline_io.md
    ├── gestion_memoria.md
    └── profiling/
        ├── reporte_profiling.md
        ├── strace_plain.txt
        ├── strace_compressed.txt
        ├── strace_encrypted.txt
        ├── time_plain.txt
        ├── time_compressed.txt
        └── time_encrypted.txt
```

---

## Compilar y ejecutar

**Requisitos:** `gcc`, `make`, `strace`, `valgrind` (Linux / WSL2)

```bash
# Instalar herramientas (Ubuntu/Debian/WSL)
sudo apt-get install -y gcc make strace valgrind
```

```bash
make          # compilar editor
make run      # compilar y ejecutar el editor interactivo
make bench    # compilar el benchmark
make profile  # generar evidencia completa: strace + time para A, B y C
make valgrind # verificar fugas de memoria
make debug    # compilar con AddressSanitizer
make clean    # limpiar binarios
```

---

## Uso del editor

Al ejecutar `./bin/editor` aparece el menú:

```
  [1] Cargar archivo de texto
  [2] Editar documento (Gap Buffer)
  [3] Guardar con RLE
  [4] Guardar con Huffman
  [5] Guardar con LZ77
  [6] Abrir archivo comprimido / cifrado
  [7] Comparar los 3 algoritmos
  [8] Comparar write() vs mmap()
  [9] Crear archivo de prueba
  [a] Guardar con RLE + RC4
  [b] Guardar con Huffman + RC4
  [c] Guardar con LZ77 + RC4
  [i] Info del Gap Buffer
  [0] Salir
```

### Flujo — solo compresión

```
[9] Crear archivo de prueba   → elegir tipo (1, 2 o 3)
[7] Comparar algoritmos       → ver tabla de ratios
[4] Guardar con Huffman       → ruta destino: /tmp/prueba.bin
[6] Abrir archivo comprimido  → ruta: /tmp/prueba.bin
```

### Flujo — compresión + cifrado RC4

```
[9] Crear archivo de prueba       → elegir tipo
[b] Guardar con Huffman + RC4     → ruta: /tmp/enc.bin  → llave: mi_llave
[6] Abrir archivo comprimido      → ruta: /tmp/enc.bin  → llave: mi_llave
```

> La llave se ingresa por `stdin` (nunca por argumentos ni hardcoded). Se borra de la RAM inmediatamente tras usarse con `secure_erase`.

---

## Arquitectura del pipeline

### Escritura (Guardar)

```
┌──────────────────────────────────────────── USER SPACE ───┐
│                                                            │
│  Gap Buffer (RAM)                                          │
│  [texto antes][   GAP   ][texto después]                   │
│        │  gap_buffer_get_content()                         │
│        ▼                                                   │
│  ┌───────────────────────────────────┐                     │
│  │  PASO 1: COMPRIMIR                │                     │
│  │  RLE / Huffman / LZ77             │                     │
│  │  N bytes  →  M bytes  (M < N)     │                     │
│  └──────────────────┬────────────────┘                     │
│                     │  ← checksum FNV-1a del buffer comp.  │
│                     ▼                                      │
│  ┌───────────────────────────────────┐                     │
│  │  PASO 2: CIFRAR con RC4           │  (solo si [a/b/c])  │
│  │  Llave pedida por consola         │                     │
│  │  M bytes → M bytes (sin padding)  │                     │
│  │  Llave borrada con secure_erase() │                     │
│  └──────────────────┬────────────────┘                     │
│                     │                                      │
│  [Header 64 bytes] + [datos]   →  write() bloques 4 KB    │
└─────────────────────┼──────────────────────────────────────┘
          syscall boundary
┌─────────────────────┼──────────────────────────────────────┐
│  KERNEL             │  VFS → Page Cache → Disco            │
└─────────────────────┴──────────────────────────────────────┘
```

### Lectura (Abrir)

```
Disco → read() → Header → [DESCIFRAR RC4] → verificar checksum → DESCOMPRIMIR → Gap Buffer
```

Si la llave es incorrecta, el checksum del resultado no coincide con el del header → se rechaza antes de descomprimir.

---

## Criptografía RC4 y seguridad de llaves

### ¿Por qué RC4?

RC4 es un **cifrador de flujo simétrico**:
- Implementado en ~40 líneas de C puro, sin librerías externas.
- **Sin padding:** el tamaño cifrado es idéntico al comprimido.
- Descifrar = cifrar (mismo XOR con el mismo keystream).
- `rc4_decrypt` es un alias de `rc4_encrypt` — misma función.

### Implementación (`src/crypto.c`)

```c
// KSA — mezcla el estado S[256] con la llave
static void rc4_ksa(uint8_t S[256], const uint8_t *key, size_t key_len) {
    for (int i = 0; i < 256; i++) S[i] = (uint8_t)i;
    int j = 0;
    for (int i = 0; i < 256; i++) {
        j = (j + S[i] + key[i % key_len]) & 0xFF;
        uint8_t t = S[i]; S[i] = S[j]; S[j] = t;
    }
}

// PRGA — genera keystream y XOR con los datos
uint8_t *rc4_encrypt(const uint8_t *key, size_t key_len,
                     const uint8_t *in,  size_t in_len, size_t *out_len) {
    // ... keystream XOR ...
    secure_erase(S, sizeof(S));   // borrar estado interno del stack
    *out_len = in_len;
    return out;
}
```

### Gestión segura de la llave en RAM

```c
// En cmd_save() — main.c
char key[256];
memset(key, 0, sizeof(key));
fgets(key, sizeof(key), stdin);        // 1. Pedir por consola (no por argv)

rc4_encrypt((uint8_t *)key, ...);      // 2. Usar la llave

secure_erase(key, sizeof(key));        // 3. BORRAR INMEDIATAMENTE de la RAM
```

**Por qué no alcanza con `memset`:**

El compilador puede eliminar un `memset` si detecta que la variable no se usa después (dead-code elimination). `secure_erase` usa el calificador `volatile`, que le indica al compilador que esa memoria puede observarse externamente — **la escritura no puede omitirse**:

```c
void secure_erase(void *ptr, size_t len) {
    volatile uint8_t *p = (volatile uint8_t *)ptr;
    while (len--) *p++ = 0;   // el compilador DEBE ejecutar esta línea
}
```

### Niveles de seguridad implementados

| Nivel | Técnica | Estado |
|---|---|---|
| Sin hardcode | Llave pedida por `stdin` | ✅ |
| Sin argv | Nunca `./editor llave` | ✅ |
| Borrado de RAM | `secure_erase` (≡ `explicit_bzero`) | ✅ |
| Sin Swap | `mlock()` bloquea la página de RAM | Mencionado en sustentación |

---

## Formato de archivo binario

Cabecera de exactamente **64 bytes** con `__attribute__((packed))`, seguida de los datos:

```
Offset  Bytes  Campo
──────  ─────  ────────────────────────────────────────────────
 0       9     magic: "EDITORCMP"
 9       1     version: 1
10       1     algo: 0=RLE  1=Huffman  2=LZ77
11       1     encrypted: 0=sin cifrado  1=RC4
12       4     reserved
16       4     original_size (bytes antes de comprimir)
20       4     compressed_size (bytes comprimidos, antes de cifrar)
24       8     checksum FNV-1a de los datos comprimidos
32      32     filename (nombre del archivo fuente)
────────────────────────────────────────────────────────────────
64       N     datos: comprimidos | cifrados(comprimidos)
```

**Compatibilidad hacia atrás:** archivos del parcial 3 (sin `encrypted`) tienen ese byte en cero (era `reserved[0]`) y abren correctamente con el nuevo código.

**Verificación del checksum al abrir:**

- **Sin cifrado:** checksum del bloque de datos → debe coincidir.
- **Con cifrado:** descifrar → checksum del resultado → debe coincidir con el header. Si la llave fue incorrecta, el checksum falla antes de descomprimir.

---

## Módulos del sistema

### Gap Buffer (`buffer.h/c`)

Estructura clásica de editores de texto (Emacs, VS Code).

```
[ texto antes del cursor ][ G  A  P ][ texto después ]
                          ↑          ↑
                      gap_start    gap_end
```

| Operación | Array simple | Gap Buffer |
|---|---|---|
| Insertar en posición actual | O(n) | O(1) amortizado |
| Mover cursor | O(1) | O(1) |
| Acceso aleatorio | O(1) | O(1) |
| Localidad de caché | Buena | Excelente |

### Compresión (`compress.h/c`)

| Algoritmo | Mejor para | Complejidad | Ratio típico (texto) |
|---|---|---|---|
| **RLE** | Texto muy repetitivo (`AAAA…`) | O(n) | Variable |
| **Huffman** | Texto natural (frecuencias desiguales) | O(n log k) | ~48% |
| **LZ77** | Código fuente (patrones a distancia) | O(n·W) | ~30-60% |

### Cifrado (`crypto.h/c`)

| Función | Descripción |
|---|---|
| `rc4_encrypt` | Cifra un buffer en RAM. Retorna buffer malloc'd. |
| `rc4_decrypt` | Alias de `rc4_encrypt` (RC4 es simétrico). |
| `secure_erase` | Borrado garantizado con `volatile` (≡ `explicit_bzero`). |

### I/O POSIX (`io.h/c`)

| Método | Syscalls | Mejor para |
|---|---|---|
| `open + write` | N/4096 | Streaming de datos |
| `mmap + msync` | 1 | Archivos grandes, acceso aleatorio |

Buffer alineado a `PAGE_SIZE = 4096` bytes — coincide con el tamaño de página de memoria virtual en x86 y el bloque de ext4.

---

## Benchmark y resultados

### Ejecutar el benchmark

```bash
./bin/bench             # los 3 enfoques + tabla comparativa
./bin/bench plain       # A — solo clásico
./bin/bench compressed  # B — solo compresión
./bin/bench encrypted   # C — compresión + RC4

# Con strace (cuenta syscalls por enfoque)
strace -c ./bin/bench plain
strace -c ./bin/bench compressed
strace -c ./bin/bench encrypted

# Con time (mide wall-clock y CPU)
time ./bin/bench plain
time ./bin/bench compressed
time ./bin/bench encrypted
```

### Tabla comparativa (1 MB de texto natural)

| Métrica | A. Clásico | B. Solo compresión | C. Compresión + RC4 | A vs C |
|---|---|---|---|---|
| **Tamaño al disco** | 1024 KB | ~527 KB | ~527 KB | **−48%** |
| **CPU compresión** | 0.000 ms | ~X ms | ~X ms | aumento |
| **CPU cifrado** | 0.000 ms | 0.000 ms | ~Y ms | aumento adicional |
| **CPU total** | 0.000 ms | ~X ms | ~(X+Y) ms | dobla vs solo comprimir |
| **write() calls** | 256 | ~132 | ~132 | **−48%** |
| **Tiempo I/O** | ~23 ms | ~12 ms | ~12 ms | **−48%** |

> Los valores exactos X e Y varían por hardware. Ejecutar `make profile` para obtenerlos en el equipo donde se evalúa.

### Cómo se aíslan los tiempos de CPU (`bench.c`)

```c
struct timespec t0, t1, t2, t3;

// Solo compresión
clock_gettime(CLOCK_MONOTONIC, &t0);
comp = huffman_compress(data, len, &comp_len);
clock_gettime(CLOCK_MONOTONIC, &t1);

// Solo cifrado
clock_gettime(CLOCK_MONOTONIC, &t2);
enc = rc4_encrypt(key, key_len, comp, comp_len, &enc_len);
clock_gettime(CLOCK_MONOTONIC, &t3);

printf("CPU compresión : %.3f ms\n", diff_ms(t0, t1));
printf("CPU cifrado    : %.3f ms\n", diff_ms(t2, t3));
printf("CPU total      : %.3f ms\n", diff_ms(t0,t1) + diff_ms(t2,t3));
```

### Conclusión

> Añadir cifrado RC4 casi duplica el costo de CPU respecto a solo comprimir, pero el tamaño en disco permanece ~48% menor que el enfoque clásico. El sistema opera en un tiempo total similar al clásico inseguro, pero con **datos 100% cifrados en reposo** y ocupando la mitad del espacio.

---

## Gestión de memoria

### Política de propiedad de buffers

Toda función que retorna `uint8_t *` transfiere la propiedad al llamador:

```c
// Estas funciones retornan buffers que el llamador DEBE liberar con free()
uint8_t *rc4_encrypt(...)        → free() en el llamador
uint8_t *huffman_compress(...)   → free() en el llamador
uint8_t *io_read_fd(...)         → free() en el llamador
char    *gap_buffer_get_content  → free() en el llamador
```

### Verificación con Valgrind

```bash
make valgrind
```

```
HEAP SUMMARY:
    in use at exit: 0 bytes in 0 blocks
  total heap usage: 8 allocs, 8 frees, 31,731 bytes allocated

All heap blocks were freed -- no leaks are possible
ERROR SUMMARY: 0 errors from 0 contexts
```

---

## Preguntas de sustentación

### ¿Por qué comprimir antes de cifrar?

El cifrado RC4 genera salida pseudoaleatoria de **alta entropía** — no hay bytes repetidos ni patrones estadísticos. Los algoritmos de compresión explotan la redundancia. Sin redundancia, la compresión no reduce el tamaño: el archivo puede incluso crecer por la cabecera del compresor. Comprimir primero aprovecha los patrones del texto original; cifrar después protege los datos ya reducidos.

### ¿`memset` es suficiente para borrar la llave?

No. El compilador puede eliminar un `memset` si determina que la variable no se lee después (optimización por dead-code elimination). `secure_erase` usa `volatile`, que prohíbe al compilador omitir la escritura. Es funcionalmente equivalente a `explicit_bzero` de glibc.

### ¿Y si el SO manda la llave a Swap antes de borrarla?

Es un riesgo real. Si el kernel envía la página de RAM con la llave al archivo de Swap (por presión de memoria), la llave queda en texto plano en el disco aunque se llame a `secure_erase`. La solución es `mlock()`, que bloquea la página en RAM e impide que el kernel la envíe a Swap:

```c
mlock(key, sizeof(key));         // bloquear la página
// ... usar la llave ...
secure_erase(key, sizeof(key));  // borrar
munlock(key, sizeof(key));
```

### ¿Por qué buffer de 4096 bytes en I/O?

4096 bytes = 4 KB es el tamaño estándar de **página de memoria virtual** en x86/x86_64 y coincide con el tamaño de bloque de ext4. Cada `write()` llena exactamente una página en la Page Cache del kernel, minimizando los context switches:

```
1 MB con buffer 4 KB →    256 write() calls
1 MB con buffer 1  B → 1.048.576 write() calls  (×4096 peor)
```

### ¿RC4 agrega padding?

No. RC4 es un **cifrador de flujo**: genera un keystream del mismo largo que los datos y aplica XOR byte a byte. El resultado tiene exactamente el mismo tamaño que la entrada. Por eso el ahorro de la compresión se preserva íntegro después del cifrado.

### ¿Cómo se detecta una llave incorrecta?

El checksum FNV-1a se calcula sobre los datos **comprimidos** (antes de cifrar) y se guarda en el header. Al abrir un archivo cifrado:

1. Se descifran los datos con la llave ingresada.
2. Se calcula el checksum del resultado (= datos comprimidos si la llave es correcta).
3. Se compara con el checksum del header.

Si la llave es incorrecta, el descifrado produce basura, el checksum falla y el editor reporta error sin intentar descomprimir datos corruptos.

---

## Requisitos

| Herramienta | Versión mínima |
|---|---|
| `gcc` | 9.0+ |
| `make` | 4.0+ |
| `strace` | cualquiera |
| `valgrind` | cualquiera |
| SO | Linux / WSL2 Ubuntu |

