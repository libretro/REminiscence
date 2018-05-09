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
	Game       *g = new Game(&fs, "", 0, Language::LANG_EN);
	g->init();

	g->tick();
	EXPECT_TRUE(g->isRunning());

	g->_pi.quit = true;
	g->tick();
	EXPECT_FALSE(g->isRunning());
}
