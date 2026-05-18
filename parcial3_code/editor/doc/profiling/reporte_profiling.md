# Reporte de Profiling — Editor de Texto con Optimización de Bus I/O

## Configuración del experimento

- **Archivo de prueba:** 1 MB de texto natural repetitivo (texto generado en tiempo de ejecución)
- **Hardware:** CPU x86_64, Linux (WSL2 Ubuntu 24.04)
- **Herramientas:** `strace 6.8`, `valgrind 3.22`, `gcc 13.3`
- **Comando:** `strace -c ./bench [plain|compressed]`

---

## 1. Enfoque Clásico — Texto Plano (sin compresión)

```
=== ENFOQUE CLÁSICO (texto plano) ===
Archivo de salida : /tmp/bench_plain.txt
Tamaño            : 1048576 bytes (1024.0 KB)
Bytes escritos    : 1048576
Llamadas write()  : 256
Tiempo            : 23.818 ms
```

### Salida de `strace -c`:

```
% time     seconds  usecs/call     calls    errors syscall
------ ----------- ----------- --------- --------- ----------------
 98.33    0.005664          22       257           write
  1.42    0.000082          41         2           munmap
  0.24    0.000014           4         3           close
  ...
100.00    0.005760          19       294         1 total
```

---

## 2. Enfoque Propuesto — Compresión en User Space (Huffman)

```
=== ENFOQUE PROPUESTO (compresión en User Space) ===
Archivo de salida : /tmp/bench_compressed.bin
Tamaño original   : 1048576 bytes (1024.0 KB)
Tamaño comprimido : 540400  bytes (527.7 KB)
Ratio compresión  : 48.5%
Bytes escritos    : 540400
Llamadas write()  : 132
Tiempo            : 12.700 ms
```

### Salida de `strace -c`:

```
% time     seconds  usecs/call     calls    errors syscall
------ ----------- ----------- --------- --------- ----------------
 71.43    0.002848          21       133           write
  9.18    0.000366          33        11           mmap
  6.70    0.000267          66         4           munmap
  ...
100.00    0.003987          22       178         1 total
```

---

## 3. Tabla Comparativa

| Métrica del Kernel         | Enfoque Clásico | Enfoque Propuesto | Impacto         |
|----------------------------|-----------------|-------------------|-----------------|
| Volumen de datos al disco  | 1024.0 KB       | 527.7 KB          | **−48.5%**      |
| Llamadas a `write()`       | 257             | 133               | **−48.2%**      |
| Tiempo total en syscalls   | 5.760 ms        | 3.987 ms          | **−30.8%**      |
| Tiempo de I/O (User Space) | 23.818 ms       | 12.700 ms         | **−46.7%**      |
| Tiempo CPU User Mode       | 0.00 s          | 0.00 s            | +CPU (compres.) |
| Outputs (bloques de disco) | 2048            | 1056              | **−48.4%**      |

---

## 4. Análisis

### ¿Por qué hay menos llamadas a `write()`?

El buffer de I/O tiene tamaño fijo de `PAGE_SIZE = 4096` bytes:

- **Texto plano:** 1,048,576 bytes ÷ 4096 = **256 llamadas** a `write()`
- **Comprimido:** 540,400 bytes ÷ 4096 = **132 llamadas** a `write()`

Al reducir el volumen de datos a la mitad, también se reducen a la mitad
las llamadas al kernel. Cada `write()` implica un **context switch**
de User Space al Kernel Space — reducirlos disminuye directamente la
latencia del bus de I/O.

### Trade-off: CPU vs. I/O

El enfoque propuesto **invierte ciclos de CPU** (en el algoritmo Huffman)
para **ahorrar operaciones de I/O** (que son órdenes de magnitud más lentas).
Este es el principio fundamental de la optimización de bus I/O:

```
CPU (ns) << Disco (µs-ms)
```

Comprimir en User Space es rentable siempre que:

```
T_compresión + T_IO_comprimido < T_IO_plano
```

Como demuestran las métricas, este umbral se supera con creces para
archivos de texto con patrones repetitivos.

### Validación de integridad

Cada archivo comprimido incluye un checksum FNV-1a de 64 bits en el
header binario. Al cargar, se verifica antes de descomprimir — si el
checksum no coincide, el archivo se rechaza y no se cargan datos corruptos.

---

## 5. Verificación con Valgrind

```
valgrind --leak-check=full bin/editor

HEAP SUMMARY:
    in use at exit: 0 bytes in 0 blocks
  total heap usage: 8 allocs, 8 frees, 31,731 bytes allocated

All heap blocks were freed -- no leaks are possible
ERROR SUMMARY: 0 errors from 0 contexts
```

**Sin fugas de memoria.** Todos los `malloc`/`calloc` tienen su `free` correspondiente.

---

## 6. Comandos utilizados

```bash
# Compilar benchmark
make bench

# Generar toda la evidencia automáticamente
make profile

# Verificar memoria
make valgrind

# Perfilar el editor interactivo
strace -c ./bin/editor
```
