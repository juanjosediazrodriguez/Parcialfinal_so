CC      = gcc
CFLAGS  = -std=c11 -Wall -Wextra -O2 -D_POSIX_C_SOURCE=200809L
DBGFLAGS= -std=c11 -Wall -Wextra -g -fsanitize=address,undefined -D_POSIX_C_SOURCE=200809L
VGFLAGS = -std=c11 -Wall -g -D_POSIX_C_SOURCE=200809L

SRC_DIR = src
BIN_DIR = bin

SRCS = $(SRC_DIR)/main.c     \
       $(SRC_DIR)/buffer.c   \
       $(SRC_DIR)/compress.c \
       $(SRC_DIR)/io.c       \
       $(SRC_DIR)/format.c   \
       $(SRC_DIR)/crypto.c

TARGET       = $(BIN_DIR)/editor
TARGET_DBG   = $(BIN_DIR)/editor_debug
TARGET_VG    = $(BIN_DIR)/editor_vg
TARGET_BENCH = $(BIN_DIR)/bench

BENCH_SRCS = bench/bench.c       \
             $(SRC_DIR)/compress.c \
             $(SRC_DIR)/io.c       \
             $(SRC_DIR)/format.c   \
             $(SRC_DIR)/buffer.c   \
             $(SRC_DIR)/crypto.c

.PHONY: all run debug valgrind bench profile clean help

all: $(BIN_DIR) $(TARGET)

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) $(SRCS) -o $(TARGET)
	@echo "Compilado: $(TARGET)"

run: all
	$(TARGET)

debug: $(BIN_DIR)
	$(CC) $(DBGFLAGS) $(SRCS) -o $(TARGET_DBG)
	$(TARGET_DBG)

valgrind: $(BIN_DIR)
	$(CC) $(VGFLAGS) $(SRCS) -o $(TARGET_VG)
	valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes --error-exitcode=1 $(TARGET_VG)

strace-run: all
	strace -c $(TARGET)

bench: $(BIN_DIR) $(TARGET_BENCH)

$(TARGET_BENCH): $(BENCH_SRCS)
	$(CC) $(CFLAGS) $(BENCH_SRCS) -o $(TARGET_BENCH)
	@echo "Compilado: $(TARGET_BENCH)"

profile: bench
	@echo ""
	@echo ">>> strace: A — CLÁSICO (texto plano)"
	strace -c $(TARGET_BENCH) plain 2>&1 | tee doc/profiling/strace_plain.txt
	@echo ""
	@echo ">>> strace: B — COMPRIMIDO (solo compresión)"
	strace -c $(TARGET_BENCH) compressed 2>&1 | tee doc/profiling/strace_compressed.txt
	@echo ""
	@echo ">>> strace: C — COMPRIMIDO + CIFRADO RC4"
	strace -c $(TARGET_BENCH) encrypted 2>&1 | tee doc/profiling/strace_encrypted.txt
	@echo ""
	@echo ">>> time: A — CLÁSICO"
	{ time $(TARGET_BENCH) plain; } 2>&1 | tee doc/profiling/time_plain.txt
	@echo ""
	@echo ">>> time: B — COMPRIMIDO"
	{ time $(TARGET_BENCH) compressed; } 2>&1 | tee doc/profiling/time_compressed.txt
	@echo ""
	@echo ">>> time: C — COMPRIMIDO + CIFRADO"
	{ time $(TARGET_BENCH) encrypted; } 2>&1 | tee doc/profiling/time_encrypted.txt
	@echo ""
	@echo "Resultados guardados en doc/profiling/"

clean:
	rm -rf $(BIN_DIR)
	rm -f samples/comprimidos/*.bin samples/descomprimidos/*.bin

help:
	@echo ""
	@echo "  make           Compilar"
	@echo "  make run       Compilar y ejecutar"
	@echo "  make debug     Compilar con AddressSanitizer y ejecutar"
	@echo "  make valgrind  Analizar fugas de memoria con Valgrind"
	@echo "  make bench     Compilar benchmark de I/O"
	@echo "  make profile   Generar evidencia completa con strace + time"
	@echo "  make strace-run  Perfilar syscalls con strace (editor interactivo)"
	@echo "  make clean     Eliminar binarios y salidas"
	@echo ""
