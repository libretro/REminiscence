
/*
 * REminiscence - Flashback interpreter
 * Copyright (C) 2005-2015 Gregory Montoir (cyx@users.sourceforge.net)
 */

#ifndef UNPACK_H__
#define UNPACK_H__

#include <boolean.h>

#ifdef __cplusplus
extern "C" {
#endif

extern bool delphine_unpack(uint8_t *dst, const uint8_t *src, int len);

#ifdef __cplusplus
}
#endif

#endif // UNPACK_H__
