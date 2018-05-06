#include <screenshot.h>
#include "gtest/gtest.h"

#include "file.h"
#include "fs.h"
#include "game.h"
#include "util.h"

#define TS_SECONDS(sec) static_cast<uint32_t>((sec) * 1000)

// IndependentMethod is a test case - here, we have 2 tests for this 1 test case
TEST(IndependentMethod, ResetsToZero) {
	FileSystem fs(DATA_PATH);
	Game *g = new Game(&fs, "", 0, Language::LANG_EN);
	g->init();

	g->update(TS_SECONDS(15));
	std::cerr << "back 1" << std::endl;
	g->_pi.quit = true;
	g->update(TS_SECONDS(1));
	std::cerr << "back 3" << std::endl;
	EXPECT_FALSE(g->isRunning());

	char dir[512];
	snprintf(dir, sizeof(dir), "%s/tmp", getenv("HOME"));
	saveTGA(dir, "screenshot.tga", (const uint8_t *)g->getFramebuffer(), Video::GAMESCREEN_W, Video::GAMESCREEN_H);
}
