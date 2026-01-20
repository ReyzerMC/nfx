#ifndef NFX_H
#define NFX_H

#include <stdint.h>

int nfx_compress(const char *input_path, const char *output_file, int level);
int nfx_decompress(const char *input_file, const char *output_dir);

#endif