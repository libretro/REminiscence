//
// Created by Stuart Carnie on 5/4/18.
//

#include "systemstub.h"
#include "game.h"
#include "util.h"
#include <libco.h>
#include <chrono>

SystemStub *SystemStub::instance;

SystemStub::SystemStub()
		: _deltaTime(0), _lastTimestamp(0) {
}

void SystemStub::start(Game *game) {
	running = true;

	SystemStub::instance = this;
	this->game = game;

	mainThread = co_active();

	auto fn = []() {
		SystemStub::instance->game->run();
		SystemStub::instance->running = false;
		while (true) {
			debug(DBG_INFO, "Running dead emulator\n");
			SystemStub::instance->yield();
		}
	};

	gameThread = co_create(65536 * sizeof(void *), fn);
}

void SystemStub::init(const char *title, int w, int h, bool fullscreen) {
	SystemStub::instance = this;
}

void SystemStub::resume(uint32_t ts) {
	_deltaTime = ts - _lastTimestamp;
	_lastTimestamp = ts;

	// slow?
	auto count = _deltaTime / MS_PER_FRAME;
	if (count > 0) {
		debug(DBG_INFO, "we're slow by %d frames\n", count);
	}

	co_switch(gameThread);
}

void SystemStub::yield() {
	co_switch(mainThread);
}

void SystemStub::updateScreen(int shakeOffset) {
	renderScreen(shakeOffset);
	yield();
}
