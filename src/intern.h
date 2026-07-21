
/*
 * REminiscence - Flashback interpreter
 * Copyright (C) 2005-2015 Gregory Montoir (cyx@users.sourceforge.net)
 */

#ifndef INTERN_H__
#define INTERN_H__

#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <stdint.h>

#include <retro_inline.h>
#include <libretro.h>

#include "intern_structs.h"

extern retro_log_printf_t          log_cb;

/* Result of one frame-step of a resumable subsystem. A subsystem's step()
 * runs the work for exactly one presented host frame and returns STEP_RUNNING;
 * it returns STEP_DONE once the whole activity (fade, screen, cutscene, ...)
 * has finished. This is what replaces the libco per-frame yield: instead of
 * suspending a call stack, each subsystem keeps its progress in members and
 * advances one frame per call. */
enum StepResult {
	STEP_RUNNING,
	STEP_DONE,
};

template<typename T>
static INLINE void SWAP(T &a, T &b) {
	T tmp = a;
	a = b;
	b = tmp;
}

struct AnimBuffers {
	struct AnimBufferState *_states[4];
	uint8_t         _curPos[4];

	void addState(uint8_t stateNum, int16_t x, int16_t y, const uint8_t *dataPtr, struct LivePGE *pge, uint8_t w,
	              uint8_t h);
};

extern Options    g_options;

#endif // INTERN_H__
