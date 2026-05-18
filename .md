# Reto Final: El Triángulo de Hierro (Espacio, Tiempo y Seguridad)

## Contexto y Desafío Actualizado

Optimizar el bus I/O mediante compresión fue su primer paso como ingenieros de sistemas. Sin embargo, en el mundo real, los datos en reposo (**Data at Rest**) que no están cifrados representan una vulnerabilidad crítica.

Su reto final es integrar una capa de seguridad criptográfica simétrica al editor de archivos.

El archivo no solo debe viajar por el bus I/O comprimido, sino también encriptado. Esto introduce un dilema profundo: la encriptación requiere ciclos de CPU y puede requerir relleno de bytes (*padding*), mientras que la compresión busca reducirlos.

Su misión es diseñar un *pipeline* de memoria en C que integre ambas operaciones sin corromper los datos, gestionando de forma segura las llaves criptográficas en la memoria RAM y demostrando matemáticamente que el sistema sigue siendo rentable frente al cuello de botella del disco.

> ¡Cuidado con la entropía!  
> El orden en que apliquen estas transformaciones determinará el éxito o el fracaso absoluto de su arquitectura.

---

# Rúbrica de Evaluación

| Criterio de Evaluación | Excelente (100%) - Nivel Arquitecto OS | Aceptable (70%) - Nivel Desarrollador | Deficiente (30%) - Nivel Básico |
|---|---|---|---|
| **Arquitectura de Pipeline (Compresión vs. Entropía)** | Demuestra maestría teórica: **Comprime primero, encripta después**. Justifica esto explicando cómo la encriptación aumenta la entropía, haciendo imposible la compresión posterior. El pipeline fluye limpiamente en memoria usando buffers unificados. | Implementa ambas, pero el orden es incorrecto (**Encripta → Comprime**). El archivo resultante no se reduce de tamaño (o crece), fracasando en el objetivo de optimizar el bus I/O, aunque el código sea funcional. | Falla al integrar ambas. La base de código no soporta aplicar dos transformaciones secuenciales en memoria, provocando corrupción de datos o `segmentation faults`. |
| **Gestión Segura de Memoria y Llaves en RAM** | La llave de encriptación se solicita al usuario (no está quemada en el código). Una vez utilizada en la función, la memoria que contiene la llave en texto plano es destruida inmediatamente y de forma segura usando funciones como `explicit_bzero` o equivalentes, evitando que quede en la RAM. | La llave no está quemada en el código (se pide por consola), pero no se borra de la memoria dinámicamente, dejando vulnerabilidades de *Memory Scraping* si el proceso hace un *core dump*. | La llave de encriptación está *hardcoded* (quemada) en el archivo `.c` o se pasa de forma insegura por los argumentos de la línea de comandos (`argv`). |
| **Profiling de CPU (Aislamiento de Cargas)** | El análisis con `time` y `strace` aísla perfectamente el *overhead* de la CPU: muestra cuánto tiempo cuesta comprimir y cuánto tiempo adicional cuesta encriptar. Justifica si el ahorro de I/O sigue compensando el doble castigo a la CPU. | Muestra el tiempo total (**Comprimir + Encriptar**) vs (**Guardado Clásico**), pero no logra separar analíticamente cuánto pesa cada algoritmo en el procesador. | No analiza el impacto de la encriptación, asumiendo erróneamente que “la seguridad es gratis” en términos computacionales. |

---

# Nueva Regla Arquitectónica (Restricción Técnica)

## 6. Criptografía en C Space

### Mandato

Deben implementar un algoritmo de encriptación simétrico básico (puede ser una implementación propia de **RC4**, **ChaCha20**, o un cifrado de bloque simple con *padding* gestionado manualmente).

Si se permite el uso de una librería como `libcrypto` (OpenSSL), está estrictamente prohibido usar sus funciones de alto nivel que escriben a disco; solo pueden usar la librería para transformar el buffer en memoria RAM (`EVP_EncryptUpdate`) antes de hacer la llamada al sistema `write()`.

### Seguridad de la Llave

La llave debe ser borrada de la memoria RAM inmediatamente después de usarse.

> Un ingeniero de OS no deja basura criptográfica en la pila (*stack*) ni en el *heap*.

---

# El Benchmark Actualizado (El Entregable Analítico)

**Archivo de prueba:** Documento de 50 MB *(ejemplo)*.

| Métrica del Kernel | A. Clásico (Plano directo) | B. Solo Compresión | C. Compresión + Encriptación | Impacto Final (A vs C) |
|---|---|---|---|---|
| **Tamaño Transmitido (I/O)** | 50 MB | 15 MB | 15.1 MB (Por el *padding* de bloque) | -69.8% (Éxito en I/O) |
| **Tiempo de CPU (User Mode)** | 0.01 ms | 35.0 ms | 65.0 ms (Suma de ambos algoritmos) | Aumento significativo de CPU |
| **Tiempo de Espera I/O** | 120.0 ms | 43.0 ms | 43.5 ms | -63% (Ahorro de latencia) |
| **Tiempo Total (Wall-clock)** | 120.2 ms | 78.0 ms | 108.5 ms | Sistema 9% más rápido y seguro |

---

## Conclusión Esperada

> “Añadir seguridad casi anula el beneficio de tiempo ganado por la compresión, pero logramos un sistema 100% cifrado y que ocupa un 70% menos en el disco duro, operando en un tiempo similar al enfoque clásico inseguro”.

---

# 🛡️ Preguntas de Sustentación (Defensa Oral)

Para verificar que no usaron IA a ciegas o copiaron código, pueden hacerse estas preguntas durante la presentación del proyecto.

---

## La trampa de la entropía

> “Veo en su código que comprimen y luego encriptan. ¿Qué pasaría si invertimos el orden? Si yo encripto primero con AES y luego le paso su algoritmo de compresión, ¿qué le pasa al tamaño final del archivo y por qué?”

### Respuesta esperada

El archivo no se comprimirá en absoluto.

La encriptación genera datos pseudoaleatorios (**alta entropía**). Los algoritmos de compresión buscan patrones repetitivos; al no existir patrones legibles, la compresión se vuelve inútil y el archivo incluso podría aumentar de tamaño.

---

## La trampa de la memoria virtual

> “Ustedes borran la llave de encriptación de la RAM usando un `memset()` o `free()`. ¿Pero qué pasa si el Sistema Operativo, justo antes de que ustedes borren la memoria, decide mandar esa página de RAM a la partición de *Swap* en el disco duro porque se quedó sin espacio? ¿La llave queda en el disco?”

### Respuesta esperada

Sí, es un riesgo real del Sistema Operativo.

Un alumno excepcional mencionará el uso de `mlock()` en Linux para bloquear esa página de memoria y evitar que el kernel la envíe al disco de *Swap*.

---

## El tamaño de página

> “Usaron un buffer de 4096 bytes para leer y escribir. ¿Por qué exactamente 4096 y no 4000 o 5000?”

### Respuesta esperada

4096 bytes (**4 KB**) es el tamaño estándar de una página de memoria virtual en arquitecturas **x86/Linux** y también coincide normalmente con el tamaño de bloque del sistema de archivos (**ext4**).

Alinear los buffers a este tamaño evita lecturas parciales o fragmentadas, reduciendo operaciones innecesarias y maximizando la eficiencia del bus I/O.
