
#ifndef SCREENSHOT_H__
#define SCREENSHOT_H__

#include <stdint.h>

void saveTGA(const char *directory, const char *filename, const uint8_t *rgba, int w, int h);

#endif
