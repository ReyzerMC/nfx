#ifndef PROGRESS_H
#define PROGRESS_H

#include <stdint.h>

void progress_start(uint64_t total);
void progress_update(uint64_t processed);
void progress_finish(void);

#endif