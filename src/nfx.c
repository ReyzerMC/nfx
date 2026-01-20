#define _POSIX_C_SOURCE 200809L // Para strdup y funciones POSIX
#include "nfx.h"
#include "progress.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <errno.h>
#include <zstd.h>

#define BUF_SIZE 65536 // 64KB Buffer

#pragma pack(push,1)
typedef struct {
    char magic[3];     // 'n','f','x'
    uint8_t version;
    uint8_t algo;
    uint32_t files;    // Cantidad total de archivos
} nfx_header_t;
#pragma pack(pop)

/* ---------- Estructura para lista enlazada de archivos ---------- */
typedef struct FileNode {
    char *rel_path;       // Ruta relativa (para guardar en el nfx)
    char *full_path;      // Ruta absoluta/real (para leer del disco)
    uint64_t size;
    struct FileNode *next;
} FileNode;

/* ---------- Utils ---------- */

// Función recursiva para listar archivos
static void scan_directory(const char *base_path, const char *current_sub, FileNode **head, FileNode **tail, uint32_t *count, uint64_t *total_size) {
    char full_path[2048];
    // Construir ruta actual de exploración
    if (current_sub) snprintf(full_path, sizeof(full_path), "%s/%s", base_path, current_sub);
    else snprintf(full_path, sizeof(full_path), "%s", base_path);

    DIR *d = opendir(full_path);
    if (!d) {
        // Si no es directorio, intentamos ver si es un archivo suelto
        struct stat st;
        if (stat(full_path, &st) == 0 && S_ISREG(st.st_mode)) {
            FileNode *node = malloc(sizeof(FileNode));
            node->rel_path = strdup(current_sub ? current_sub : base_path); // Usar nombre base si es archivo único
            node->full_path = strdup(full_path);
            node->size = st.st_size;
            node->next = NULL;
            
            if (*tail) (*tail)->next = node;
            else *head = node;
            *tail = node;
            
            (*count)++;
            *total_size += st.st_size;
        }
        return;
    }

    struct dirent *entry;
    while ((entry = readdir(d)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;

        char new_rel[2048];
        if (current_sub) snprintf(new_rel, sizeof(new_rel), "%s/%s", current_sub, entry->d_name);
        else snprintf(new_rel, sizeof(new_rel), "%s", entry->d_name);

        char entry_full_path[2048];
        snprintf(entry_full_path, sizeof(entry_full_path), "%s/%s", base_path, new_rel);

        struct stat st;
        if (stat(entry_full_path, &st) == 0) {
            if (S_ISDIR(st.st_mode)) {
                // RECURSIVIDAD
                scan_directory(base_path, new_rel, head, tail, count, total_size);
            } else if (S_ISREG(st.st_mode)) {
                // Añadir archivo a la lista
                FileNode *node = malloc(sizeof(FileNode));
                node->rel_path = strdup(new_rel);
                node->full_path = strdup(entry_full_path);
                node->size = st.st_size;
                node->next = NULL;

                if (*tail) (*tail)->next = node;
                else *head = node;
                *tail = node;

                (*count)++;
                *total_size += st.st_size;
            }
        }
    }
    closedir(d);
}

static void free_file_list(FileNode *head) {
    while (head) {
        FileNode *next = head->next;
        free(head->rel_path);
        free(head->full_path);
        free(head);
        head = next;
    }
}

/* ---------- Compression ---------- */
int nfx_compress(const char *input_path, const char *output_file, int level) {
    FileNode *head = NULL;
    FileNode *tail = NULL;
    uint32_t file_count = 0;
    uint64_t total_bytes = 0;

    printf("Scaning for files...\n");
    // Escaneo inteligente: detecta si input_path es archivo o carpeta
    struct stat path_stat;
    if (stat(input_path, &path_stat) != 0) { perror("stat input"); return -1; }
    
    if (S_ISDIR(path_stat.st_mode)) {
        scan_directory(input_path, NULL, &head, &tail, &file_count, &total_bytes);
    } else {
        // Es un solo archivo
        scan_directory(input_path, NULL, &head, &tail, &file_count, &total_bytes);
        // Ajuste cosmético: si es solo un archivo, queremos guardar solo el nombre base, no toda la ruta
        if(head) {
             char *p = strrchr(input_path, '/');
             if(p) { free(head->rel_path); head->rel_path = strdup(p+1); }
        }
    }

    if (file_count == 0) {
        printf("No files found.\n");
        return 0;
    }

    FILE *out = fopen(output_file, "wb");
    if (!out) { perror("output"); free_file_list(head); return -1; }

    /* Escribir Header Global */
    nfx_header_t hdr = { {'n','f','x'}, 1, 1, file_count };
    fwrite(&hdr, sizeof(hdr), 1, out);

    /* ZSTD Context */
    ZSTD_CCtx *cctx = ZSTD_createCCtx();
    ZSTD_CCtx_setParameter(cctx, ZSTD_c_compressionLevel, level);
    
    unsigned char *inbuf = malloc(BUF_SIZE);
    unsigned char *outbuf = malloc(BUF_SIZE);
    
    uint64_t processed_global = 0;
    progress_start(total_bytes);

    FileNode *curr = head;
    while (curr) {
        /* 1. Escribir Metadata del archivo actual (Sin comprimir) */
        uint16_t path_len = (uint16_t)strlen(curr->rel_path);
        fwrite(&path_len, sizeof(path_len), 1, out);
        fwrite(curr->rel_path, 1, path_len, out);
        fwrite(&curr->size, sizeof(curr->size), 1, out);

        /* 2. Comprimir contenido del archivo */
        FILE *in = fopen(curr->full_path, "rb");
        if (!in) { 
            fprintf(stderr, "Error reading: %s\n", curr->full_path);
            curr = curr->next; 
            continue; 
        }

        /* Reiniciar contexto para empezar un frame nuevo y limpio */
        ZSTD_CCtx_reset(cctx, ZSTD_reset_session_only);

        size_t read;
        while ((read = fread(inbuf, 1, BUF_SIZE, in)) > 0) {
            ZSTD_inBuffer z_in = { inbuf, read, 0 };
            while (z_in.pos < z_in.size) {
                ZSTD_outBuffer z_out = { outbuf, BUF_SIZE, 0 };
                // Usamos ZSTD_e_continue mientras leemos el archivo
                ZSTD_compressStream2(cctx, &z_out, &z_in, ZSTD_e_continue);
                fwrite(outbuf, 1, z_out.pos, out);
            }
            processed_global += read;
            progress_update(processed_global);
        }

        /* 3. Finalizar Frame (Flush) para este archivo */
        int done = 0;
        ZSTD_inBuffer null_in = { NULL, 0, 0 }; // FIX DEL SEGFAULT
        while (!done) {
            ZSTD_outBuffer z_out = { outbuf, BUF_SIZE, 0 };
            // ZSTD_e_end marca el fin del frame. El descompresor sabrá parar aquí.
            size_t ret = ZSTD_compressStream2(cctx, &z_out, &null_in, ZSTD_e_end);
            fwrite(outbuf, 1, z_out.pos, out);
            if (ret == 0) done = 1;
        }

        fclose(in);
        curr = curr->next;
    }

    progress_finish();
    free(inbuf);
    free(outbuf);
    ZSTD_freeCCtx(cctx);
    fclose(out);
    free_file_list(head);
    return 0;
}

/* ---------- Decompression ---------- */

typedef struct {
    FILE *f;
    uint8_t *buf;
    size_t size;  // Tamaño total buffer
    size_t pos;   // Posición actual lectura
    size_t limit; // Bytes válidos en buffer
} BufferedReader;

static int buf_read_byte(BufferedReader *br, uint8_t *out_b) {
    if (br->pos >= br->limit) {
        br->limit = fread(br->buf, 1, br->size, br->f);
        br->pos = 0;
        if (br->limit == 0) return 0; // EOF
    }
    *out_b = br->buf[br->pos++];
    return 1;
}

static uint64_t get_file_size(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) return 0;
    return st.st_size;
}

static int buf_read_bytes(BufferedReader *br, void *dest, size_t count) {
    size_t remain = count;
    uint8_t *d = (uint8_t*)dest;
    while (remain > 0) {
        if (br->pos >= br->limit) {
            br->limit = fread(br->buf, 1, br->size, br->f);
            br->pos = 0;
            if (br->limit == 0) return 0; // Error o EOF inesperado
        }
        size_t available = br->limit - br->pos;
        size_t to_copy = (remain < available) ? remain : available;
        memcpy(d, br->buf + br->pos, to_copy);
        br->pos += to_copy;
        d += to_copy;
        remain -= to_copy;
    }
    return 1;
}

int nfx_decompress(const char *input_file, const char *output_dir) {
    FILE *in = fopen(input_file, "rb");
    if (!in) { perror("input_file"); return -1; }

    /* Obtenemos el tamaño total para la barra de progreso */
    uint64_t total_nfx_size = get_file_size(input_file);

    /* Setup Buffered Reader */
    uint8_t *io_buf = malloc(BUF_SIZE);
    BufferedReader br = { in, io_buf, BUF_SIZE, 0, 0 };

    /* Leer header */
    nfx_header_t hdr;
    if (!buf_read_bytes(&br, &hdr, sizeof(hdr))) { fclose(in); free(io_buf); return -2; }
    if (hdr.magic[0]!='n') { printf("Invalid Format\n"); fclose(in); free(io_buf); return -3; }

    /* Mantenemos el mensaje */
    printf("Extracting %d files to '%s'...\n", hdr.files, output_dir);
    
    /* Iniciamos la barra de progreso */
    progress_start(total_nfx_size);

    mkdir(output_dir, 0755);

    ZSTD_DCtx *dctx = ZSTD_createDCtx();
    unsigned char *outbuf = malloc(BUF_SIZE);

    for (uint32_t i=0; i < hdr.files; i++) {
        /* Leer metadata */
        uint16_t path_len;
        if (!buf_read_bytes(&br, &path_len, sizeof(path_len))) break;

        char path[1024];
        if (path_len >= sizeof(path)) path_len = sizeof(path)-1;
        buf_read_bytes(&br, path, path_len);
        path[path_len] = 0;

        uint64_t file_size;
        buf_read_bytes(&br, &file_size, sizeof(file_size));

        /* Crear directorios necesarios */
        char full_path[2048];
        snprintf(full_path, sizeof(full_path), "%s/%s", output_dir, path);
        
        char *slash = strrchr(full_path, '/');
        if (slash) {
            *slash = 0;
            char tmp[2048]; 
            snprintf(tmp, sizeof(tmp), "mkdir -p \"%s\"", full_path);
            system(tmp); 
            *slash = '/';
        }

        FILE *out = fopen(full_path, "wb");
        if (!out) { perror(full_path); continue; }

        /* Comentamos esto para no romper la barra de progreso visualmente */
        /* printf("  -> %s\n", path); */

        /* Descompresión del Frame ZSTD */
        ZSTD_DCtx_reset(dctx, ZSTD_reset_session_only);
        
        size_t ret = 1; 
        while (ret > 0) {
            if (br.pos >= br.limit) {
                br.limit = fread(br.buf, 1, br.size, br.f);
                br.pos = 0;
                if (br.limit == 0) break; 
            }

            ZSTD_inBuffer z_in = { br.buf, br.limit, br.pos };
            ZSTD_outBuffer z_out = { outbuf, BUF_SIZE, 0 };
            
            ret = ZSTD_decompressStream(dctx, &z_out, &z_in);
            
            if (ZSTD_isError(ret)) {
                /* Si hay error, limpiamos la línea de progreso para mostrar el mensaje */
                fprintf(stderr, "\nZSTD Error: %s\n", ZSTD_getErrorName(ret));
                break;
            }

            fwrite(outbuf, 1, z_out.pos, out);
            br.pos = z_in.pos;

            /* --- CÁLCULO DE PROGRESO --- */
            /* Calculamos la posición real en el archivo físico:
               Posición del puntero OS (ftell) - Lo que queda en el buffer sin consumir */
            long current_file_pos = ftell(in);
            long unconsumed_in_buffer = br.limit - br.pos;
            uint64_t actual_processed = (uint64_t)(current_file_pos - unconsumed_in_buffer);
            
            progress_update(actual_processed);
        }
        fclose(out);
    }

    progress_finish(); /* Finalizar barra (salto de línea) */

    free(outbuf);
    free(io_buf);
    ZSTD_freeDCtx(dctx);
    fclose(in);
    return 0;
}
