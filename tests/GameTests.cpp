#include "gtest/gtest.h"

#include "file.h"
#include "fs.h"
#include "game.h"
#include "systemstub.h"
#include "util.h"

struct MockStub : SystemStub {
	void init(const char *title, int w, int h, bool fullscreen) override {
		SystemStub::init(title, w, h, fullscreen);
	}

	void destroy() override {

	}

	void setScreenSize(int w, int h) override {

	}

	void setPalette(const uint8_t *pal, int n) override {

	}

	void setPaletteEntry(int i, const Color *c) override {

	}

	void getPaletteEntry(int i, Color *c) override {

	}

	void copyRect(int x, int y, int w, int h, const uint8_t *buf, int pitch) override {

	}

	void renderScreen(int shakeOffset) override {
		std::cerr << "render" << std::endl;
	}

	void processEvents() override {

	}

	void sleep(int duration) override {

	}

	uint32_t getTimeStamp() override {
		return 0;
	}

	void startAudio(AudioCallback callback, void *param) override {

	}

	void stopAudio() override {

	}

	uint32_t getOutputSampleRate() override {
		return 0;
	}

	void lockAudio() override {

	}

	void unlockAudio() override {

	}
};


void independentMethod(int &i) {
	i = 0;
}

// IndependentMethod is a test case - here, we have 2 tests for this 1 test case
TEST(IndependentMethod, ResetsToZero) {
	MockStub stub;
	FileSystem fs(DATA_PATH);
	Game *g = new Game(&stub, &fs, "", 0, Language::LANG_EN);
	g->init();

	stub.start(g);
	stub.resume(TS_SECONDS(0));
	std::cerr << "back 1" << std::endl;
	stub.resume(TS_SECONDS(1));
	std::cerr << "back 2" << std::endl;
	stub._pi.quit = true;
	stub.resume(TS_SECONDS(2));
	std::cerr << "back 3" << std::endl;
	EXPECT_FALSE(stub.isRunning());

}
