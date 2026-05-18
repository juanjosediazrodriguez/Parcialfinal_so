# Proyecto 3: Editor de Archivos con Optimización de Bus I/O en Entornos Linux/C

## Contexto y Desafío

En el diseño de Sistemas Operativos, la interacción con el hardware es el cuello de botella más crítico. En este proyecto, desarrollarán un editor de texto en C nativo para entornos Linux. Su objetivo principal no es solo la edición de texto, sino la manipulación directa de la memoria y la optimización del bus de I/O.

Exigimos que ningún archivo viaje al disco en texto claro. Deberán implementar un pipeline donde la información sea comprimida en el User Space antes de invocar las llamadas al sistema del kernel (como `write` o `mmap`). 

Como ingenieros, deberán utilizar herramientas nativas de Linux (como `strace` o `perf`) para demostrar empíricamente que invertir ciclos de CPU en comprimir datos con C resulta en un ahorro neto de tiempo al reducir la carga y latencia del disco físico.

---

## Rúbrica de Evaluación

La evaluación se estructura en cuatro ejes fundamentales, evaluando el rigor técnico en la gestión de memoria (C) y el uso estratégico de la API de Linux.

| Criterio de Evaluación | Excelente (100%) - Nivel Arquitecto OS | Aceptable (70%) - Nivel Desarrollador | Deficiente (30%) - Nivel Básico |
|------------------------|----------------------------------------|--------------------------------------|--------------------------------|
| **1. Arquitectura de I/O y Syscalls (Linux)** | Analiza y justifica el uso de la API POSIX elegida. Compara el rendimiento de hacer I/O basado en descriptores de archivos (`open`, `read`, `write` con buffers alineados al tamaño de página del SO, ej. 4KB) versus mapeo en memoria (`mmap`). | Utiliza `read/write` de forma estándar, pero sin justificar el tamaño del buffer o sin considerar el tamaño de página del sistema. | Utiliza librerías de alto nivel (`<stdio.h>`, `fopen`, `fprintf`) ignorando llamadas al sistema directas. |
| **2. Gestión de Memoria y Punteros (C)** | Manejo impecable de memoria dinámica (`malloc`, `calloc`, `free`). Código sin fugas ni errores (verificable con `valgrind`). | Funciona pero presenta ligeras fugas de memoria. | Mala gestión de punteros, provoca crashes o errores del sistema (OOM/Segfault). |
| **3. Funcionalidad del Editor y Estructuras (C structs)** | **Base:** Edición robusta usando Gap Buffers o listas enlazadas. <br> **Adicional:** Texto enriquecido con headers binarios usando `__attribute__((packed))`. | Editor funcional con compresión/descompresión correcta, sin texto enriquecido. | Compresión incorrecta, datos corruptos o caracteres basura. |
| **4. Profiling y Análisis de Rendimiento (Linux Tools)** | Análisis exhaustivo con `strace` y `time`, demostrando reducción de syscalls y context switches. | Métricas generales sin evidencia profunda del SO. | Sin métricas reales, solo suposiciones. |

---

## Estructura del Entregable

### 1. Matriz de Diseño del Pipeline I/O
- Diagrama de flujo detallando el paso de los datos.

### 2. Gestión de Memoria en C
- Explicación del diseño de estructuras (`struct`).
- Optimización para evitar padding innecesario.
- Estrategias para evitar fugas de memoria.

### 3. Manejo de Texto Enriquecido (Opcional)
- Especificación del formato binario:
  - Primeros 64 bytes: Magic Number.
  - Tabla de metadatos/estilos.
  - Payload comprimido.

### 4. Reporte de Profiling (Evidencia de Ingeniería)

---

## Ejemplo de Benchmark (medido con `strace` y `time` en Linux)

**Archivo de prueba:** Documento de 50 MB.

| Métrica del Kernel | Enfoque Clásico | Enfoque Propuesto | Impacto |
|--------------------|----------------|-------------------|---------|
| Volumen de Datos a Disco | 50 MB | 15 MB | -70% |
| Llamadas a `write()` | 12,800 | 3,750 | -70% |
| Tiempo de CPU (User Mode) | 0.01 ms | 35.0 ms | Aumento esperado |
| Tiempo de SO (Sys Mode) | 15.0 ms | 4.0 ms | -73% |
| Tiempo Total (Wall-clock) | 120.5 ms | 85.0 ms | 29% más rápido |

---

## Nota de Evaluación

El estudiante debe presentar capturas o extractos reales de la salida de:

```bash
strace -c ./editor
