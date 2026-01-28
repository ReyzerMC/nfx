#include "nfx.h"
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv) {
    if (argc < 4) {
        printf("Usage:\n");
        printf("  nfx c [file or folder] output.nfx\n");
        printf("  nfx x file.nfx 'Folder' \n  [Folder is where the file/s will be extracted at]\n");
        printf("  nfx z file.zip 'Folder' Uncompress Zip files");
        return 1;
    }

    if (strcmp(argv[1], "c") == 0) {
        return nfx_compress(argv[2], argv[3], 5);
    }

    if (strcmp(argv[1], "x") == 0) {
        return nfx_decompress(argv[2], argv[3]);
    }

    if (strcmp(argv[1], "z") == 0) {
        return uncompress_zip(argv[2], argv[3]);
    }

    printf("Unknown command\n");
    return 1;
}