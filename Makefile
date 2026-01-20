# Variables de compilación
CC      = gcc
# -MMD genera archivos de dependencia .d
CFLAGS  = -std=c11 -O2 -Wall -Wextra -MMD -Iinclude
LDFLAGS = -lzstd

# Directorios
SRC_DIR = src
OBJ_DIR = obj
BIN_DIR = .

# Archivos
TARGET  = $(BIN_DIR)/nfx
SRC     = $(wildcard $(SRC_DIR)/*.c)
# Esto cambia src/archivo.c por obj/archivo.o
OBJ     = $(SRC:$(SRC_DIR)/%.c=$(OBJ_DIR)/%.o)
DEP     = $(OBJ:.o=.d)

# Reglas principales
all: $(TARGET)

$(TARGET): $(OBJ)
	@mkdir -p $(BIN_DIR)
	$(CC) $(OBJ) -o $(TARGET) $(LDFLAGS)

# Compilación de objetos
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# Incluir las dependencias generadas por GCC
-include $(DEP)

# Utilidades
clean:
	rm -rf $(OBJ_DIR) $(TARGET)

install: $(TARGET)
	install -D -m 755 $(TARGET) /usr/local/bin/$(TARGET)

uninstall:
	rm -f /usr/local/bin/$(TARGET)

.PHONY: all clean install uninstall