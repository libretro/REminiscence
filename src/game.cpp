
/*
 * REminiscence - Flashback interpreter
 * Copyright (C) 2005-2015 Gregory Montoir (cyx@users.sourceforge.net)
 */

#include <ctime>
#include <stdio.h>
#include <libco.h>
#include "file.h"
#include "fs.h"
#include "game.h"
#include "seq_player.h"
#include "unpack.h"
#include "util.h"

struct StateManager {
	Game *_game;
	int  _old;

	StateManager(Game *game, int to)
		: _game(game), _old(game->_state) {
		game->_state = to;
	}

	~StateManager() {
		_game->_state = _old;
	}
};

Options g_options = {};

Game *Game::instance;

Game::Game(FileSystem *fs, const char *savePath, int level, Language lang)
	: _cut(&_res, this, &_vid), _menu(&_res, this, &_vid),
	  _mix(fs, this), _res(fs, lang), _seq(&_vid, this, &_mix), _vid(&_res, this),
	  _fs(fs), _savePath(savePath), _sleep(0), _state(STATE_INIT) {
	_stateSlot     = 1;
	_inp_demPos    = 0;
	_skillLevel    = _menu._skill = 1;
	_currentLevel  = _menu._level = level;
	_demoBin       = -1;
	Game::instance = this;
}

Game::~Game() {
	Game::instance = NULL;
}

static void game_thread() {
	Game::instance->run();
	Game::instance->running = false;
	while (true) {
		/* Running dead emulator */
		Game::instance->yield();
	}
}

void Game::init() {
	_randSeed = time(0);

	_res.init();
	_res.load_TEXT();

	_res.load("FB_TXT", Resource::OT_FNT);
	if (g_options.use_seq_cutscenes) {
		_res._hasSeqData = _fs->exists("INTRO.SEQ");
	}
	if (_fs->exists("logosssi.cmd")) {
		_cut._patchedOffsetsTable = Cutscene::_ssiOffsetsTable;
	}
	_mix.init();
	_mix._mod._isAmiga = false;

	running    = true;
	mainThread = co_active();
	gameThread = co_create(65536 * sizeof(void *), game_thread);
}

void Game::run() {
	playCutscene(0x40);
	playCutscene(0x0D);

	_res.load("GLOBAL", Resource::OT_ICN);
	_res.load("GLOBAL", Resource::OT_SPC);
	_res.load("PERSO", Resource::OT_SPR);
	_res.load_SPR_OFF("PERSO", _res._spr1);
	_res.load_FIB("GLOBAL");

	while (!_pi.quit) {
		if (true || _res._isDemo) {
			// do not present title screen and menus
		} else {
			StateManager sm(this, STATE_MAIN_MENU);

			_mix.playMusic(1);
			_menu.handleTitleScreen();
			if (_menu._selectedOption == Menu::MENU_OPTION_ITEM_QUIT || _pi.quit) {
				_pi.quit = true;
				break;
			}
			if (_menu._selectedOption == Menu::MENU_OPTION_ITEM_DEMO) {
				_demoBin = (_demoBin + 1) % ARRAY_SIZE(_demoInputs);
				const char *fn = _demoInputs[_demoBin].name;
				_res.load_DEM(fn);
				if (_res._demLen == 0) {
					continue;
				}
				_skillLevel   = 1;
				_currentLevel = _demoInputs[_demoBin].level;
				_randSeed     = 0;
				_mix.stopMusic();
				break;
			}
			_demoBin      = -1;
			_skillLevel   = _menu._skill;
			_currentLevel = _menu._level;
			_mix.stopMusic();
			if (_pi.quit) {
				break;
			}
		}
		if (_currentLevel == 7) {
			_vid.fadeOut();
			_vid.setTextPalette();
			playCutscene(0x3D);
		} else {
			_vid.setTextPalette();
			_vid.setPalette0xF();
			_vid._unkPalSlot1 = 0;
			_vid._unkPalSlot2 = 0;
			_score = 0;
			loadLevelData();
			resetGameState();
			_endLoop        = false;
			_frameTimestamp = getTimeStamp();

			StateManager sm(this, STATE_MAIN_LOOP);
			while (!_pi.quit && !_endLoop) {
				mainLoop();
				if (_demoBin != -1 && _inp_demPos >= _res._demLen) {
					// exit level
					_demoBin = -1;
					_endLoop = true;
				}
			}
			// flush inputs
			_pi.dirMask = 0;
			_pi.use     = false;
			_pi.weapon  = false;
			_pi.action  = false;
		}
	}

	_res.free_TEXT();
	_mix.free();
	_res.fini();
}

void Game::tick() {
	_lastTimestamp += MS_PER_FRAME;
	co_switch(gameThread);
}

void Game::yield() {
	co_switch(mainThread);
}

void Game::sleep(int ms) {
	const int ms_per_frame = 1000 / getFrameRate();
	_sleep += ms;

	while (_sleep >= ms_per_frame) {
		yield();
		_sleep -= ms_per_frame;
	}
}

void Game::processEvents() {

}

uint32_t Game::getTimeStamp() {
	return _lastTimestamp;
}

void Game::processFragment(int16_t *stream, int len) {
	_mix.mix(stream, len);
}

uint32_t *Game::getFrameBuffer() {
	return _vid._frameBuffer;
}

void Game::resetGameState() {
	_animBuffers._states[0] = _animBuffer0State;
	_animBuffers._curPos[0] = 0xFF;
	_animBuffers._states[1] = _animBuffer1State;
	_animBuffers._curPos[1] = 0xFF;
	_animBuffers._states[2] = _animBuffer2State;
	_animBuffers._curPos[2] = 0xFF;
	_animBuffers._states[3] = _animBuffer3State;
	_animBuffers._curPos[3] = 0xFF;
	_currentRoom = _res._pgeInit[0].init_room;
	_cut._deathCutsceneId = 0xFFFF;
	_pge_opTempVar2       = 0xFFFF;
	_deathCutsceneCounter = 0;
	_saveStateCompleted   = false;
	_loadMap              = true;
	pge_resetGroups();
	_blinkingConradCounter = 0;
	_pge_processOBJ        = false;
	_pge_opTempVar1        = 0;
	_textToDisplay         = 0xFFFF;
}

void Game::mainLoop() {
	playCutscene();
	if (_cut._id == 0x3D) {
		showFinalScore();
		_endLoop = true;
		return;
	}
	if (_deathCutsceneCounter) {
		--_deathCutsceneCounter;
		if (_deathCutsceneCounter == 0) {
			playCutscene(_cut._deathCutsceneId);
			if (!handleContinueAbort()) {
				playCutscene(0x41);
				_endLoop = true;
			} else {
				if (_validSaveState) {
					if (!loadGameState(0))
						_endLoop = true;
				} else {
					loadLevelData();
					resetGameState();
				}
			}
			return;
		}
	}
	memcpy(_vid._frontLayer, _vid._backLayer, Video::GAMESCREEN_SIZE);
	pge_getInput();
	pge_prepare();
	col_prepareRoomState();
	uint8_t       oldLevel = _currentLevel;
	for (uint16_t i        = 0; i < _res._pgeNum; ++i) {
		LivePGE *pge = _pge_liveTable2[i];
		if (pge) {
			_col_currentPiegeGridPosY = (pge->pos_y / 36) & ~1;
			_col_currentPiegeGridPosX = (pge->pos_x + 8) >> 4;
			pge_process(pge);
		}
	}
	if (oldLevel != _currentLevel) {
		if (_res._isDemo)
			_currentLevel = oldLevel;
		changeLevel();
		_pge_opTempVar1 = 0;
		return;
	}
	if (_loadMap) {
		if (_currentRoom == 0xFF || !hasLevelMap(_currentLevel, _pgeLive[0].room_location)) {
			_cut._id = 6;
			_deathCutsceneCounter = 1;
		} else {
			_currentRoom = _pgeLive[0].room_location;
			loadLevelMap();
			_loadMap = false;
		}
	}
	prepareAnims();
	drawAnims();
	drawCurrentInventoryItem();
	drawLevelTexts();
	printLevelCode();
	if (_blinkingConradCounter != 0) {
		--_blinkingConradCounter;
	}
	_vid.updateScreen();
	updateTiming();
	drawStoryTexts();
	if (_pi.inventory_skip) {
		_pi.inventory_skip = false;
		handleInventory();
	}
	if (_pi.escape) {
		_pi.escape = false;
		if (_demoBin != -1 || handleConfigPanel()) {
			_endLoop = true;
			return;
		}
	}
	inp_handleSpecialKeys();
}

void Game::updateTiming() {
	static const int frameHz = 30;
	int32_t          delay   = getTimeStamp() - _frameTimestamp;
	int32_t          pause   = (_pi.dbgMask & PlayerInput::DF_FASTMODE) ? 20 : (1000 / frameHz);
	pause -= delay;
	if (pause > 0) {
		sleep(pause);
	}
	_frameTimestamp = getTimeStamp();
}

void Game::playCutscene(int id) {
	if (id != -1) {
		_cut._id = id;
	}
	if (_cut._id != 0xFFFF) {
		StateManager sm(this, STATE_CUT_SCENE);

		_mix.stopMusic();
		if (_res._hasSeqData) {
			int num = 0;
			switch (_cut._id) {
			case 0x02: {
				static const uint8_t tab[] = {1, 2, 1, 3, 3, 4, 4};
				num = tab[_currentLevel];
			}
				break;
			case 0x05: {
				static const uint8_t tab[] = {1, 2, 3, 5, 5, 4, 4};
				num = tab[_currentLevel];
			}
				break;
			case 0x0A: {
				static const uint8_t tab[] = {1, 2, 2, 2, 2, 2, 2};
				num = tab[_currentLevel];
			}
				break;
			case 0x10: {
				static const uint8_t tab[] = {1, 1, 1, 2, 2, 3, 3};
				num = tab[_currentLevel];
			}
				break;
			case 0x3C: {
				static const uint8_t tab[] = {1, 1, 1, 1, 1, 2, 2};
				num = tab[_currentLevel];
			}
				break;
			case 0x40:
				return;
			case 0x4A:
				return;
			}
			if (SeqPlayer::_namesTable[_cut._id]) {
				char name[16];
				snprintf(name, sizeof(name), "%s.SEQ", SeqPlayer::_namesTable[_cut._id]);
				char *p = strchr(name, '0');
				if (p) {
					*p += num;
				}
				if (playCutsceneSeq(name)) {
					if (_cut._id == 0x3D) {
						playCutsceneSeq("CREDITS.SEQ");
						_cut._interrupted = false;
					} else {
						_cut._id = 0xFFFF;
					}
					return;
				}
			}
		}
		if (_cut._id != 0x4A) {
			_mix.playMusic(Cutscene::_musicTable[_cut._id]);
		}
		_cut.play();
		if (id == 0xD && !_cut._interrupted) {
			_cut._id = 0x4A;
			_cut.play();
		}
		if (id == 0x3D) {
			_cut.playCredits();
		}
		_mix.stopMusic();
	}
}

bool Game::playCutsceneSeq(const char *name) {
	File f;
	if (f.open(name, "rb", _fs)) {
		_seq.setBackBuffer(_res._scratchBuffer);
		_seq.play(&f);
		return true;
	}
	return false;
}

void Game::inp_handleSpecialKeys() {
	if (_pi.dbgMask & PlayerInput::DF_SETLIFE) {
		_pgeLive[0].life = 0x7FFF;
	}
	if (_pi.load) {
		loadGameState(_stateSlot);
		_pi.load = false;
	}
	if (_pi.save) {
		saveGameState(_stateSlot);
		_pi.save = false;
	}
	if (_pi.stateSlot != 0) {
		int8_t slot = _stateSlot + _pi.stateSlot;
		if (slot >= 1 && slot < 100) {
			_stateSlot = slot;
		}
		_pi.stateSlot = 0;
	}
}

void Game::drawCurrentInventoryItem() {
	uint16_t src = _pgeLive[0].current_inventory_PGE;
	if (src != 0xFF) {
		_currentIcon = _res._pgeInit[src].icon_num;
		drawIcon(_currentIcon, 232, 8, 0xA);
	}
}

void Game::showFinalScore() {
	StateManager sm(this, STATE_FINAL_SCORE);
	playCutscene(0x49);
	char buf[50];
	snprintf(buf, sizeof(buf), "SCORE %08u", _score);
	_vid.drawString(buf, (256 - strlen(buf) * 8) / 2, 40, 0xE5);
	strcpy(buf, _menu._passwords[7][_skillLevel]);
	_vid.drawString(buf, (256 - strlen(buf) * 8) / 2, 16, 0xE7);
	while (!_pi.quit) {
		_vid.copyRect(0, 0, Video::GAMESCREEN_W, Video::GAMESCREEN_H, _vid._frontLayer, 256);
		yield();
		processEvents();
		if (_pi.use) {
			_pi.use = false;
			break;
		}
		sleep(100);
	}
}

bool Game::handleConfigPanel() {
	const int x = 7;
	const int y = 10;
	const int w = 17;
	const int h = 12;

	_vid._charShadowColor      = 0xE2;
	_vid._charFrontColor       = 0xEE;
	_vid._charTransparentColor = 0xFF;

	// the panel background is drawn using special characters from FB_TXT.FNT
	static const bool kUseDefaultFont = true;

	// top-left rounded corder
	_vid.PC_drawChar(0x81, y, x, kUseDefaultFont);
	// top horizontal line
	for (int i = 1; i < w; ++i) {
		_vid.PC_drawChar(0x85, y, x + i, kUseDefaultFont);
	}
	// top-right rounded corner
	_vid.PC_drawChar(0x82, y, x + w, kUseDefaultFont);
	for (int j = 1; j < h; ++j) {
		// left vertical line
		_vid.PC_drawChar(0x86, y + j, x, kUseDefaultFont);
		for (int i                 = 1; i < w; ++i) {
			_vid._charTransparentColor = 0xE2;
			_vid.PC_drawChar(0x20, y + j, x + i, kUseDefaultFont);
		}
		_vid._charTransparentColor = 0xFF;
		// right vertical line
		_vid.PC_drawChar(0x87, y + j, x + w, kUseDefaultFont);
	}
	// bottom-left rounded corner
	_vid.PC_drawChar(0x83, y + h, x, kUseDefaultFont);
	// bottom horizontal line
	for (int i = 1; i < w; ++i) {
		_vid.PC_drawChar(0x88, y + h, x + i, kUseDefaultFont);
	}
	// bottom-right rounded corner
	_vid.PC_drawChar(0x84, y + h, x + w, kUseDefaultFont);

	_menu._charVar3   = 0xE4;
	_menu._charVar4   = 0xE5;
	_menu._charVar1   = 0xE2;
	_menu._charVar2   = 0xEE;

	enum {
		MENU_ITEM_LOAD = 1, MENU_ITEM_SAVE = 2, MENU_ITEM_ABORT = 3
	};
	uint8_t  colors[] = {2, 3, 3, 3};
	int      current  = 0;
	while (!_pi.quit) {
		_menu.drawString(_res.getMenuString(LocaleData::LI_18_RESUME_GAME), y + 2, 9, colors[0]);
		_menu.drawString(_res.getMenuString(LocaleData::LI_20_LOAD_GAME), y + 4, 9, colors[1]);
		_menu.drawString(_res.getMenuString(LocaleData::LI_21_SAVE_GAME), y + 6, 9, colors[2]);
		_menu.drawString(_res.getMenuString(LocaleData::LI_19_ABORT_GAME), y + 8, 9, colors[3]);
		char buf[30];
		snprintf(buf, sizeof(buf), "%s : %d-%02d", _res.getMenuString(LocaleData::LI_22_SAVE_SLOT), _currentLevel + 1,
		         _stateSlot);
		_menu.drawString(buf, y + 10, 9, 1);

		_vid.updateScreen();
		sleep(80);
		inp_update();

		int prev = current;
		if (_pi.dirMask & PlayerInput::DIR_UP) {
			_pi.dirMask &= ~PlayerInput::DIR_UP;
			current = (current + 3) % 4;
		}
		if (_pi.dirMask & PlayerInput::DIR_DOWN) {
			_pi.dirMask &= ~PlayerInput::DIR_DOWN;
			current = (current + 1) % 4;
		}
		if (_pi.dirMask & PlayerInput::DIR_LEFT) {
			_pi.dirMask &= ~PlayerInput::DIR_LEFT;
			--_stateSlot;
			if (_stateSlot < 1) {
				_stateSlot = 1;
			}
		}
		if (_pi.dirMask & PlayerInput::DIR_RIGHT) {
			_pi.dirMask &= ~PlayerInput::DIR_RIGHT;
			++_stateSlot;
			if (_stateSlot > 99) {
				_stateSlot = 99;
			}
		}
		if (prev != current) {
			SWAP(colors[prev], colors[current]);
		}
		if (_pi.use) {
			_pi.use = false;
			switch (current) {
			case MENU_ITEM_LOAD:
				_pi.load = true;
				break;
			case MENU_ITEM_SAVE:
				_pi.save = true;
				break;
			}
			break;
		}
		if (_pi.escape) {
			_pi.escape = false;
			break;
		}
	}
	return (current == MENU_ITEM_ABORT);
}

bool Game::handleContinueAbort() {
	playCutscene(0x48);
	int     timeout       = 100;
	int     current_color = 0;
	uint8_t colors[]      = {0xE4, 0xE5};
	uint8_t color_inc     = 0xFF;
	Color   col;
	_vid.getPaletteEntry(0xE4, &col);
	memcpy(_vid._tempLayer, _vid._frontLayer, Video::GAMESCREEN_SIZE);
	while (timeout >= 0 && !_pi.quit) {
		const char *str;
		str = _res.getMenuString(LocaleData::LI_01_CONTINUE_OR_ABORT);
		_vid.drawString(str, (256 - strlen(str) * 8) / 2, 64, 0xE3);
		str = _res.getMenuString(LocaleData::LI_02_TIME);
		char buf[50];
		snprintf(buf, sizeof(buf), "%s : %d", str, timeout / 10);
		_vid.drawString(buf, 96, 88, 0xE3);
		str = _res.getMenuString(LocaleData::LI_03_CONTINUE);
		_vid.drawString(str, (256 - strlen(str) * 8) / 2, 104, colors[0]);
		str = _res.getMenuString(LocaleData::LI_04_ABORT);
		_vid.drawString(str, (256 - strlen(str) * 8) / 2, 112, colors[1]);
		snprintf(buf, sizeof(buf), "SCORE  %08u", _score);
		_vid.drawString(buf, 64, 154, 0xE3);
		if (_pi.dirMask & PlayerInput::DIR_UP) {
			_pi.dirMask &= ~PlayerInput::DIR_UP;
			if (current_color > 0) {
				SWAP(colors[current_color], colors[current_color - 1]);
				--current_color;
			}
		}
		if (_pi.dirMask & PlayerInput::DIR_DOWN) {
			_pi.dirMask &= ~PlayerInput::DIR_DOWN;
			if (current_color < 1) {
				SWAP(colors[current_color], colors[current_color + 1]);
				++current_color;
			}
		}
		if (_pi.use) {
			_pi.use = false;
			return (current_color == 0);
		}
		_vid.copyRect(0, 0, Video::GAMESCREEN_W, Video::GAMESCREEN_H, _vid._frontLayer, 256);
		yield();
		static const int COLOR_STEP = 8;
		static const int COLOR_MIN  = 16;
		static const int COLOR_MAX  = 256 - 16;
		if (col.b >= COLOR_MAX) {
			color_inc = 0;
		} else if (col.b < COLOR_MIN) {
			color_inc = 0xFF;
		}
		if (color_inc == 0xFF) {
			col.b += COLOR_STEP;
			col.g += COLOR_STEP;
		} else {
			col.b -= COLOR_STEP;
			col.g -= COLOR_STEP;
		}
		_vid.setPaletteEntry(0xE4, &col);
		processEvents();
		sleep(100);
		--timeout;
		memcpy(_vid._frontLayer, _vid._tempLayer, Video::GAMESCREEN_SIZE);
	}
	return false;
}

void Game::printLevelCode() {
	if (_printLevelCodeCounter != 0) {
		--_printLevelCodeCounter;
		if (_printLevelCodeCounter != 0) {
			char       buf[32];
			const char *code = Menu::_passwords[_currentLevel][_skillLevel];
			snprintf(buf, sizeof(buf), "CODE: %s", code);
			_vid.drawString(buf, (Video::GAMESCREEN_W - strlen(buf) * 8) / 2, 16, 0xE7);
		}
	}
}

void Game::printSaveStateCompleted() {
	if (_saveStateCompleted) {
		const char *str = _res.getMenuString(LocaleData::LI_05_COMPLETED);
		_vid.drawString(str, (176 - strlen(str) * 8) / 2, 34, 0xE6);
	}
}

void Game::drawLevelTexts() {
	LivePGE *pge        = &_pgeLive[0];
	int8_t  obj         = col_findCurrentCollidingObject(pge, 3, 0xFF, 0xFF, &pge);
	if (obj == 0) {
		obj = col_findCurrentCollidingObject(pge, 0xFF, 5, 9, &pge);
	}
	if (obj > 0) {
		_printLevelCodeCounter = 0;
		if (_textToDisplay == 0xFFFF) {
			uint8_t icon_num = obj - 1;
			drawIcon(icon_num, 80, 8, 0xA);
			uint8_t    txt_num = pge->init_PGE->text_num;
			const char *str    = (const char *) _res.getTextString(_currentLevel, txt_num);
			_vid.drawString(str, (176 - strlen(str) * 8) / 2, 26, 0xE6);
			if (icon_num == 2) {
				printSaveStateCompleted();
				return;
			}
		} else {
			_currentInventoryIconNum = obj - 1;
		}
	}
	_saveStateCompleted = false;
}

static int getLineLength(const uint8_t *str) {
	int len = 0;
	while (*str && *str != 0xB && *str != 0xA) {
		++str;
		++len;
	}
	return len;
}

void Game::drawStoryTexts() {
	if (_textToDisplay != 0xFFFF) {
		uint8_t       textColor = 0xE8;
		const uint8_t *str      = _res.getGameString(_textToDisplay);
		memcpy(_vid._tempLayer, _vid._frontLayer, Video::GAMESCREEN_SIZE);
		int textSpeechSegment = 0;
		while (!_pi.quit) {
			drawIcon(_currentInventoryIconNum, 80, 8, 0xA);
			if (*str == 0xFF) {
				if (_res._lang == LANG_JP) {
					switch (str[1]) {
					case 0:
						textColor = 0xE9;
						break;
					case 1:
						textColor = 0xEB;
						break;
					default:
						warning("Unhandled JP color code 0x%x", str[1]);
						break;
					}
					str += 2;
				} else {
					textColor = str[1];
					// str[2] is an unused color (possibly the shadow)
					str += 3;
				}
			}
			int        yPos = 26;
			while (1) {
				const int len = getLineLength(str);
				str = (const uint8_t *) _vid.drawString((const char *) str, (176 - len * 8) / 2, yPos, textColor);
				yPos += 8;
				if (*str == 0 || *str == 0xB) {
					break;
				}
				++str;
			}
			MixerChunk chunk;
			_res.load_VCE(_textToDisplay, textSpeechSegment++, &chunk.data, &chunk.len);
			if (chunk.data) {
				_mix.play(&chunk, 32000, Mixer::MAX_VOLUME);
			}
			_vid.updateScreen();
			while (!_pi.inventory_skip && !_pi.quit) {
				if (chunk.data && !_mix.isPlaying(&chunk)) {
					break;
				}
				inp_update();
				sleep(80);
			}
			if (chunk.data) {
				_mix.stopAll();
				free(chunk.data);
			}
			_pi.inventory_skip = false;
			if (*str == 0) {
				break;
			}
			++str;
			memcpy(_vid._frontLayer, _vid._tempLayer, Video::GAMESCREEN_SIZE);
		}
		_textToDisplay = 0xFFFF;
	}
}

void Game::prepareAnims() {
	if (!(_currentRoom & 0x80) && _currentRoom < 0x40) {
		int8_t  pge_room;
		LivePGE *pge = _pge_liveTable1[_currentRoom];
		while (pge) {
			prepareAnimsHelper(pge, 0, 0);
			pge = pge->next_PGE_in_room;
		}
		pge_room     = _res._ctData[CT_UP_ROOM + _currentRoom];
		if (pge_room >= 0 && pge_room < 0x40) {
			pge = _pge_liveTable1[pge_room];
			while (pge) {
				if ((pge->init_PGE->object_type != 10 && pge->pos_y > 176) ||
				    (pge->init_PGE->object_type == 10 && pge->pos_y > 216)) {
					prepareAnimsHelper(pge, 0, -216);
				}
				pge = pge->next_PGE_in_room;
			}
		}
		pge_room     = _res._ctData[CT_DOWN_ROOM + _currentRoom];
		if (pge_room >= 0 && pge_room < 0x40) {
			pge = _pge_liveTable1[pge_room];
			while (pge) {
				if (pge->pos_y < 48) {
					prepareAnimsHelper(pge, 0, 216);
				}
				pge = pge->next_PGE_in_room;
			}
		}
		pge_room     = _res._ctData[CT_LEFT_ROOM + _currentRoom];
		if (pge_room >= 0 && pge_room < 0x40) {
			pge = _pge_liveTable1[pge_room];
			while (pge) {
				if (pge->pos_x > 224) {
					prepareAnimsHelper(pge, -256, 0);
				}
				pge = pge->next_PGE_in_room;
			}
		}
		pge_room     = _res._ctData[CT_RIGHT_ROOM + _currentRoom];
		if (pge_room >= 0 && pge_room < 0x40) {
			pge = _pge_liveTable1[pge_room];
			while (pge) {
				if (pge->pos_x <= 32) {
					prepareAnimsHelper(pge, 256, 0);
				}
				pge = pge->next_PGE_in_room;
			}
		}
	}
}

void Game::prepareAnimsHelper(LivePGE *pge, int16_t dx, int16_t dy) {
	if (!(pge->flags & 8)) {
		if (pge->index != 0 && loadMonsterSprites(pge) == 0) {
			return;
		}
		assert(pge->anim_number < 1287);
		const uint8_t *dataPtr = _res._sprData[pge->anim_number];
		if (dataPtr == 0) {
			return;
		}
		const int8_t dw = (int8_t) dataPtr[0];
		const int8_t dh = (int8_t) dataPtr[1];
		uint8_t      w  = 0, h = 0;
		w = dataPtr[2];
		h = dataPtr[3];
		dataPtr += 4;
		const int16_t ypos = dy + pge->pos_y - dh + 2;
		int16_t       xpos = dx + pge->pos_x - dw;
		if (pge->flags & 2) {
			xpos        = dw + dx + pge->pos_x;
			uint8_t _cl = w;
			if (_cl & 0x40) {
				_cl = h;
			} else {
				_cl &= 0x3F;
			}
			xpos -= _cl;
		}
		if (xpos <= -32 || xpos >= 256 || ypos < -48 || ypos >= 224) {
			return;
		}
		xpos += 8;
		if (pge == &_pgeLive[0]) {
			_animBuffers.addState(1, xpos, ypos, dataPtr, pge, w, h);
		} else if (pge->flags & 0x10) {
			_animBuffers.addState(2, xpos, ypos, dataPtr, pge, w, h);
		} else {
			_animBuffers.addState(0, xpos, ypos, dataPtr, pge, w, h);
		}
	} else {
		assert(pge->anim_number < _res._numSpc);
		const uint8_t *dataPtr = _res._spc + READ_BE_UINT16(_res._spc + pge->anim_number * 2);
		const int16_t xpos     = dx + pge->pos_x + 8;
		const int16_t ypos     = dy + pge->pos_y + 2;
		if (pge->init_PGE->object_type == 11) {
			_animBuffers.addState(3, xpos, ypos, dataPtr, pge, 0, 0);
		} else if (pge->flags & 0x10) {
			_animBuffers.addState(2, xpos, ypos, dataPtr, pge, 0, 0);
		} else {
			_animBuffers.addState(0, xpos, ypos, dataPtr, pge, 0, 0);
		}
	}
}

void Game::drawAnims() {
	_eraseBackground = false;
	drawAnimBuffer(2, _animBuffer2State);
	drawAnimBuffer(1, _animBuffer1State);
	drawAnimBuffer(0, _animBuffer0State);
	_eraseBackground = true;
	drawAnimBuffer(3, _animBuffer3State);
}

void Game::drawAnimBuffer(uint8_t stateNum, AnimBufferState *state) {
	assert(stateNum < 4);
	_animBuffers._states[stateNum] = state;
	uint8_t lastPos = _animBuffers._curPos[stateNum];
	if (lastPos != 0xFF) {
		uint8_t numAnims = lastPos + 1;
		state += lastPos;
		_animBuffers._curPos[stateNum] = 0xFF;
		do {
			LivePGE *pge = state->pge;
			if (!(pge->flags & 8)) {
				if (stateNum == 1 && (_blinkingConradCounter & 1)) {
					break;
				}
				if (!(state->dataPtr[-2] & 0x80)) {
					decodeCharacterFrame(state->dataPtr, _res._scratchBuffer);
					drawCharacter(_res._scratchBuffer, state->x, state->y, state->h, state->w, pge->flags);
				} else {
					drawCharacter(state->dataPtr, state->x, state->y, state->h, state->w, pge->flags);
				}
			} else {
				drawObject(state->dataPtr, state->x, state->y, pge->flags);
			}
			--state;
		} while (--numAnims != 0);
	}
}

void Game::drawObject(const uint8_t *dataPtr, int16_t x, int16_t y, uint8_t flags) {
	assert(dataPtr[0] < 0x4A);
	uint8_t slot  = _res._rp[dataPtr[0]];
	uint8_t *data = _res.findBankData(slot);
	if (data == 0) {
		data = _res.loadBankData(slot);
	}
	int16_t posy = y - (int8_t) dataPtr[2];
	int16_t posx = x;
	if (flags & 2) {
		posx += (int8_t) dataPtr[1];
	} else {
		posx -= (int8_t) dataPtr[1];
	}
	int count = 0;
	count = dataPtr[5];
	dataPtr += 6;
	for (int i = 0; i < count; ++i) {
		drawObjectFrame(data, dataPtr, posx, posy, flags);
		dataPtr += 4;
	}
}

void Game::drawObjectFrame(const uint8_t *bankDataPtr, const uint8_t *dataPtr, int16_t x, int16_t y, uint8_t flags) {
	const uint8_t *src = bankDataPtr + dataPtr[0] * 32;

	int16_t sprite_y = y + dataPtr[2];
	int16_t sprite_x;
	if (flags & 2) {
		sprite_x = x - dataPtr[1] - (((dataPtr[3] & 0xC) + 4) * 2);
	} else {
		sprite_x = x + dataPtr[1];
	}

	uint8_t sprite_flags = dataPtr[3];
	if (flags & 2) {
		sprite_flags ^= 0x10;
	}

	uint8_t sprite_h = (((sprite_flags >> 0) & 3) + 1) * 8;
	uint8_t sprite_w = (((sprite_flags >> 2) & 3) + 1) * 8;

	_vid.PC_decodeSpc(src, sprite_w, sprite_h, _res._scratchBuffer);

	src = _res._scratchBuffer;
	bool    sprite_mirror_x = false;
	int16_t sprite_clipped_w;
	if (sprite_x >= 0) {
		sprite_clipped_w = sprite_x + sprite_w;
		if (sprite_clipped_w < 256) {
			sprite_clipped_w = sprite_w;
		} else {
			sprite_clipped_w = 256 - sprite_x;
			if (sprite_flags & 0x10) {
				sprite_mirror_x = true;
				src += sprite_w - 1;
			}
		}
	} else {
		sprite_clipped_w = sprite_x + sprite_w;
		if (!(sprite_flags & 0x10)) {
			src -= sprite_x;
			sprite_x = 0;
		} else {
			sprite_mirror_x = true;
			src += sprite_x + sprite_w - 1;
			sprite_x        = 0;
		}
	}
	if (sprite_clipped_w <= 0) {
		return;
	}

	int16_t sprite_clipped_h;
	if (sprite_y >= 0) {
		sprite_clipped_h = 224 - sprite_h;
		if (sprite_y < sprite_clipped_h) {
			sprite_clipped_h = sprite_h;
		} else {
			sprite_clipped_h = 224 - sprite_y;
		}
	} else {
		sprite_clipped_h = sprite_h + sprite_y;
		src -= sprite_w * sprite_y;
		sprite_y         = 0;
	}
	if (sprite_clipped_h <= 0) {
		return;
	}

	if (!sprite_mirror_x && (sprite_flags & 0x10)) {
		src += sprite_w - 1;
	}

	uint32_t dst_offset      = 256 * sprite_y + sprite_x;
	uint8_t  sprite_col_mask = (flags & 0x60) >> 1;

	if (_eraseBackground) {
		if (!(sprite_flags & 0x10)) {
			_vid.drawSpriteSub1(src, _vid._frontLayer + dst_offset, sprite_w, sprite_clipped_h, sprite_clipped_w,
			                    sprite_col_mask);
		} else {
			_vid.drawSpriteSub2(src, _vid._frontLayer + dst_offset, sprite_w, sprite_clipped_h, sprite_clipped_w,
			                    sprite_col_mask);
		}
	} else {
		if (!(sprite_flags & 0x10)) {
			_vid.drawSpriteSub3(src, _vid._frontLayer + dst_offset, sprite_w, sprite_clipped_h, sprite_clipped_w,
			                    sprite_col_mask);
		} else {
			_vid.drawSpriteSub4(src, _vid._frontLayer + dst_offset, sprite_w, sprite_clipped_h, sprite_clipped_w,
			                    sprite_col_mask);
		}
	}
}

void Game::decodeCharacterFrame(const uint8_t *dataPtr, uint8_t *dstPtr) {
	int n = READ_BE_UINT16(dataPtr);
	dataPtr += 2;
	uint16_t len  = n * 2;
	uint8_t  *dst = dstPtr + 0x400;
	while (n--) {
		uint8_t c = *dataPtr++;
		dst[0] = (c & 0xF0) >> 4;
		dst[1] = (c & 0x0F) >> 0;
		dst += 2;
	}
	dst           = dstPtr;
	const uint8_t *src = dstPtr + 0x400;
	do {
		uint8_t c1 = *src++;
		if (c1 == 0xF) {
			uint8_t  c2 = *src++;
			uint16_t c3 = *src++;
			if (c2 == 0xF) {
				c1 = *src++;
				c2 = *src++;
				c3 = (c3 << 4) | c1;
				len -= 2;
			}
			memset(dst, c2, c3 + 4);
			dst += c3 + 4;
			len -= 3;
		} else {
			*dst++ = c1;
			--len;
		}
	} while (len != 0);
}

void Game::drawCharacter(const uint8_t *dataPtr, int16_t pos_x, int16_t pos_y, uint8_t a, uint8_t b, uint8_t flags) {
	bool var16 = false; // sprite_mirror_y
	if (b & 0x40) {
		b &= 0xBF;
		SWAP(a, b);
		var16 = true;
	}
	uint16_t sprite_h = a;
	uint16_t sprite_w = b;

	const uint8_t *src  = dataPtr;
	bool          var14 = false;

	int16_t sprite_clipped_w;
	if (pos_x >= 0) {
		if (pos_x + sprite_w < 256) {
			sprite_clipped_w = sprite_w;
		} else {
			sprite_clipped_w = 256 - pos_x;
			if (flags & 2) {
				var14 = true;
				if (var16) {
					src += (sprite_w - 1) * sprite_h;
				} else {
					src += sprite_w - 1;
				}
			}
		}
	} else {
		sprite_clipped_w = pos_x + sprite_w;
		if (!(flags & 2)) {
			if (var16) {
				src -= sprite_h * pos_x;
				pos_x = 0;
			} else {
				src -= pos_x;
				pos_x = 0;
			}
		} else {
			var14 = true;
			if (var16) {
				src += sprite_h * (pos_x + sprite_w - 1);
				pos_x = 0;
			} else {
				src += pos_x + sprite_w - 1;
				var14 = true;
				pos_x = 0;
			}
		}
	}
	if (sprite_clipped_w <= 0) {
		return;
	}

	int16_t sprite_clipped_h;
	if (pos_y >= 0) {
		if (pos_y < 224 - sprite_h) {
			sprite_clipped_h = sprite_h;
		} else {
			sprite_clipped_h = 224 - pos_y;
		}
	} else {
		sprite_clipped_h = sprite_h + pos_y;
		if (var16) {
			src -= pos_y;
		} else {
			src -= sprite_w * pos_y;
		}
		pos_y            = 0;
	}
	if (sprite_clipped_h <= 0) {
		return;
	}

	if (!var14 && (flags & 2)) {
		if (var16) {
			src += sprite_h * (sprite_w - 1);
		} else {
			src += sprite_w - 1;
		}
	}

	uint32_t dst_offset      = 256 * pos_y + pos_x;
	uint8_t  sprite_col_mask = ((flags & 0x60) == 0x60) ? 0x50 : 0x40;

	if (!(flags & 2)) {
		if (var16) {
			_vid.drawSpriteSub5(src, _vid._frontLayer + dst_offset, sprite_h, sprite_clipped_h, sprite_clipped_w,
			                    sprite_col_mask);
		} else {
			_vid.drawSpriteSub3(src, _vid._frontLayer + dst_offset, sprite_w, sprite_clipped_h, sprite_clipped_w,
			                    sprite_col_mask);
		}
	} else {
		if (var16) {
			_vid.drawSpriteSub6(src, _vid._frontLayer + dst_offset, sprite_h, sprite_clipped_h, sprite_clipped_w,
			                    sprite_col_mask);
		} else {
			_vid.drawSpriteSub4(src, _vid._frontLayer + dst_offset, sprite_w, sprite_clipped_h, sprite_clipped_w,
			                    sprite_col_mask);
		}
	}
}

int Game::loadMonsterSprites(LivePGE *pge) {
	InitPGE *init_pge = pge->init_PGE;
	if (init_pge->obj_node_number != 0x49 && init_pge->object_type != 10) {
		return 0xFFFF;
	}
	if (init_pge->obj_node_number == _curMonsterFrame) {
		return 0xFFFF;
	}
	if (pge->room_location != _currentRoom) {
		return 0;
	}

	const uint8_t *mList = _monsterListLevels[_currentLevel];
	while (*mList != init_pge->obj_node_number) {
		if (*mList == 0xFF) { // end of list
			return 0;
		}
		mList += 2;
	}
	_curMonsterFrame     = mList[0];
	if (_curMonsterNum != mList[1]) {
		_curMonsterNum = mList[1];
		const char *name = _monsterNames[0][_curMonsterNum];
		_res.load(name, Resource::OT_SPRM);
		_res.load_SPR_OFF(name, _res._sprm);
		_vid.setPaletteSlotLE(5, _monsterPals[_curMonsterNum]);
	}
	return 0xFFFF;
}

bool Game::hasLevelMap(int level, int room) const {
	if (_res._map) {
		return READ_LE_UINT32(_res._map + room * 6) != 0;
	} else if (_res._lev) {
		return READ_BE_UINT32(_res._lev + room * 4) != 0;
	}
	return false;
}

void Game::loadLevelMap() {
	_currentIcon = 0xFF;
	if (_res._map) {
		_vid.PC_decodeMap(_currentLevel, _currentRoom);
	} else if (_res._lev) {
		_vid.PC_decodeLev(_currentLevel, _currentRoom);
	}
	_vid.PC_setLevelPalettes();
}

void Game::loadLevelData() {
	_res.clearLevelRes();
	const Level *lvl = &_gameLevels[_currentLevel];
	_res.load(lvl->name, Resource::OT_MBK);
	_res.load(lvl->name, Resource::OT_CT);
	_res.load(lvl->name, Resource::OT_PAL);
	_res.load(lvl->name, Resource::OT_RP);
	_res.load(lvl->name, Resource::OT_MAP);
	_res.load(lvl->name2, Resource::OT_PGE);
	_res.load(lvl->name2, Resource::OT_OBJ);
	_res.load(lvl->name2, Resource::OT_ANI);
	_res.load(lvl->name2, Resource::OT_TBN);

	_cut._id = lvl->cutscene_id;
	if (_res._isDemo && _currentLevel == 5) { // PC demo does not include TELEPORT.*
		_cut._id = 0xFFFF;
	}

	_curMonsterNum   = 0xFFFF;
	_curMonsterFrame = 0;

	_res.clearBankData();
	_printLevelCodeCounter = 150;

	_col_slots2Cur  = _col_slots2;
	_col_slots2Next = 0;

	memset(_pge_liveTable2, 0, sizeof(_pge_liveTable2));
	memset(_pge_liveTable1, 0, sizeof(_pge_liveTable1));

	_currentRoom = _res._pgeInit[0].init_room;
	uint16_t n = _res._pgeNum;
	while (n--) {
		pge_loadForCurrentLevel(n);
	}

	if (_demoBin != -1) {
		_cut._id = -1;
		if (_demoInputs[_demoBin].room != 255) {
			_pgeLive[0].room_location = _demoInputs[_demoBin].room;
			_pgeLive[0].pos_x         = _demoInputs[_demoBin].x;
			_pgeLive[0].pos_y         = _demoInputs[_demoBin].y;
			_inp_demPos = 0;
		} else {
			_inp_demPos = 1;
		}
	}

	for (uint16_t i = 0; i < _res._pgeNum; ++i) {
		if (_res._pgeInit[i].skill <= _skillLevel) {
			LivePGE *pge = &_pgeLive[i];
			pge->next_PGE_in_room = _pge_liveTable1[pge->room_location];
			_pge_liveTable1[pge->room_location] = pge;
		}
	}
	pge_resetGroups();
	_validSaveState = false;

	_mix.playMusic(Mixer::MUSIC_TRACK + lvl->track);
}

void Game::drawIcon(uint8_t iconNum, int16_t x, int16_t y, uint8_t colMask) {
	uint8_t buf[16 * 16];
	_vid.PC_decodeIcn(_res._icn, iconNum, buf);
	_vid.drawSpriteSub1(buf, _vid._frontLayer + x + y * 256, 16, 16, 16, colMask << 4);
}

void Game::playSound(uint8_t sfxId, uint8_t softVol) {
	if (sfxId < _res._numSfx) {
		SoundFx *sfx = &_res._sfxList[sfxId];
		if (sfx->data) {
			MixerChunk mc;
			mc.data = sfx->data;
			mc.len  = sfx->len;
			const int freq = 6000;
			_mix.play(&mc, freq, Mixer::MAX_VOLUME >> softVol);
		}
	} else {
		// in-game music
		_mix.playMusic(sfxId);
	}
}

uint16_t Game::getRandomNumber() {
	uint32_t n = _randSeed * 2;
	if (((int32_t) _randSeed) >= 0) {
		n ^= 0x1D872B41;
	}
	_randSeed  = n;
	return n & 0xFFFF;
}

void Game::changeLevel() {
	_vid.fadeOut();
	loadLevelData();
	loadLevelMap();
	_vid.setPalette0xF();
	_vid.setTextPalette();
}

void Game::handleInventory() {
	StateManager sm(this, STATE_INVENTORY);

	LivePGE *selected_pge = 0;
	LivePGE *pge          = &_pgeLive[0];
	if (pge->life > 0 && pge->current_inventory_PGE != 0xFF) {
		playSound(66, 0);
		InventoryItem items[24];
		int           num_items = 0;
		uint8_t       inv_pge   = pge->current_inventory_PGE;
		while (inv_pge != 0xFF) {
			items[num_items].icon_num = _res._pgeInit[inv_pge].icon_num;
			items[num_items].init_pge = &_res._pgeInit[inv_pge];
			items[num_items].live_pge = &_pgeLive[inv_pge];
			inv_pge = _pgeLive[inv_pge].next_inventory_PGE;
			++num_items;
		}
		items[num_items].icon_num = 0xFF;
		int  current_item  = 0;
		int  num_lines     = (num_items - 1) / 4 + 1;
		int  current_line  = 0;
		bool display_score = false;
		while (!_pi.inventory_skip && !_pi.quit) {
			// draw inventory background
			int              icon_h     = 5;
			int              icon_y     = 140;
			int              icon_num   = 31;
			static const int icon_spr_w = 16;
			static const int icon_spr_h = 16;
			do {
				int icon_x = 56;
				int icon_w = 9;
				do {
					drawIcon(icon_num, icon_x, icon_y, 0xF);
					++icon_num;
					icon_x += icon_spr_w;
				} while (--icon_w);
				icon_y += icon_spr_h;
			} while (--icon_h);

			if (!display_score) {
				int      icon_x_pos = 72;
				for (int i          = 0; i < 4; ++i) {
					int item_it = current_line * 4 + i;
					if (items[item_it].icon_num == 0xFF) {
						break;
					}
					drawIcon(items[item_it].icon_num, icon_x_pos, 157, 0xA);
					if (current_item == item_it) {
						drawIcon(76, icon_x_pos, 157, 0xA);
						selected_pge = items[item_it].live_pge;
						uint8_t    txt_num = items[item_it].init_pge->text_num;
						const char *str    = (const char *) _res.getTextString(_currentLevel, txt_num);
						_vid.drawString(str, (256 - strlen(str) * 8) / 2, 189, 0xED);
						if (items[item_it].init_pge->init_flags & 4) {
							char buf[10];
							snprintf(buf, sizeof(buf), "%d", selected_pge->life);
							_vid.drawString(buf, (256 - strlen(buf) * 8) / 2, 197, 0xED);
						}
					}
					icon_x_pos += 32;
				}
				if (current_line != 0) {
					drawIcon(78, 120, 176, 0xA); // down arrow
				}
				if (current_line != num_lines - 1) {
					drawIcon(77, 120, 143, 0xA); // up arrow
				}
			} else {
				char buf[50];
				snprintf(buf, sizeof(buf), "SCORE %08u", _score);
				_vid.drawString(buf, (114 - strlen(buf) * 8) / 2 + 72, 158, 0xE5);
				snprintf(buf, sizeof(buf), "%s:%s", _res.getMenuString(LocaleData::LI_06_LEVEL),
				         _res.getMenuString(LocaleData::LI_13_EASY + _skillLevel));
				_vid.drawString(buf, (114 - strlen(buf) * 8) / 2 + 72, 166, 0xE5);
			}

			_vid.updateScreen();
			sleep(80);
			inp_update();

			if (_pi.dirMask & PlayerInput::DIR_UP) {
				_pi.dirMask &= ~PlayerInput::DIR_UP;
				if (current_line < num_lines - 1) {
					++current_line;
					current_item = current_line * 4;
				}
			}
			if (_pi.dirMask & PlayerInput::DIR_DOWN) {
				_pi.dirMask &= ~PlayerInput::DIR_DOWN;
				if (current_line > 0) {
					--current_line;
					current_item = current_line * 4;
				}
			}
			if (_pi.dirMask & PlayerInput::DIR_LEFT) {
				_pi.dirMask &= ~PlayerInput::DIR_LEFT;
				if (current_item > 0) {
					int item_num = current_item % 4;
					if (item_num > 0) {
						--current_item;
					}
				}
			}
			if (_pi.dirMask & PlayerInput::DIR_RIGHT) {
				_pi.dirMask &= ~PlayerInput::DIR_RIGHT;
				if (current_item < num_items - 1) {
					int item_num = current_item % 4;
					if (item_num < 3) {
						++current_item;
					}
				}
			}
			if (_pi.use) {
				_pi.use = false;
				display_score = !display_score;
			}
		}
		_pi.inventory_skip = false;
		if (selected_pge) {
			pge_setCurrentInventoryObject(selected_pge);
		}
		playSound(66, 0);
	}
}

void Game::inp_update() {
	processEvents();
	if (_demoBin != -1 && _inp_demPos < _res._demLen) {
		const int keymask = _res._dem[_inp_demPos++];
		_pi.dirMask        = keymask & 0xF;
		_pi.use            = (keymask & 0x10) != 0;
		_pi.weapon         = (keymask & 0x20) != 0;
		_pi.action         = (keymask & 0x40) != 0;
		_pi.inventory_skip = (keymask & 0x80) != 0;
	}
}

void Game::makeGameStateName(uint8_t slot, char *buf) {
	sprintf(buf, "rs-level%d-%02d.state", _currentLevel + 1, slot);
}

static const uint32_t TAG_FBSV = 0x46425356;

bool Game::saveGameState(uint8_t slot) {
	bool success = false;
	char stateFile[20];
	makeGameStateName(slot, stateFile);
	File f;
	if (!f.open(stateFile, "wb", _savePath)) {
		warning("Unable to save state file '%s'", stateFile);
	} else {
		// header
		f.writeUint32BE(TAG_FBSV);
		f.writeUint16BE(2);
		char buf[32];
		memset(buf, 0, sizeof(buf));
		snprintf(buf, sizeof(buf), "level=%d room=%d", _currentLevel + 1, _currentRoom);
		f.write(buf, sizeof(buf));
		// contents
		saveState(&f);
		if (f.ioErr()) {
			warning("I/O error when saving game state");
		} else {
			success = true;
		}
	}
	return success;
}

void Game::saveState(File *f) {
	f->writeByte(_skillLevel);
	f->writeUint32BE(_score);
	if (_col_slots2Cur == 0) {
		f->writeUint32BE(0xFFFFFFFF);
	} else {
		f->writeUint32BE(_col_slots2Cur - &_col_slots2[0]);
	}
	if (_col_slots2Next == 0) {
		f->writeUint32BE(0xFFFFFFFF);
	} else {
		f->writeUint32BE(_col_slots2Next - &_col_slots2[0]);
	}
	for (int i = 0; i < _res._pgeNum; ++i) {
		LivePGE *pge = &_pgeLive[i];
		f->writeUint16BE(pge->obj_type);
		f->writeUint16BE(pge->pos_x);
		f->writeUint16BE(pge->pos_y);
		f->writeByte(pge->anim_seq);
		f->writeByte(pge->room_location);
		f->writeUint16BE(pge->life);
		f->writeUint16BE(pge->counter_value);
		f->writeByte(pge->collision_slot);
		f->writeByte(pge->next_inventory_PGE);
		f->writeByte(pge->current_inventory_PGE);
		f->writeByte(pge->unkF);
		f->writeUint16BE(pge->anim_number);
		f->writeByte(pge->flags);
		f->writeByte(pge->index);
		f->writeUint16BE(pge->first_obj_number);
		if (pge->next_PGE_in_room == 0) {
			f->writeUint32BE(0xFFFFFFFF);
		} else {
			f->writeUint32BE(pge->next_PGE_in_room - &_pgeLive[0]);
		}
		if (pge->init_PGE == 0) {
			f->writeUint32BE(0xFFFFFFFF);
		} else {
			f->writeUint32BE(pge->init_PGE - &_res._pgeInit[0]);
		}
	}
	f->write(&_res._ctData[0x100], 0x1C00);
	for (CollisionSlot2 *cs2 = &_col_slots2[0]; cs2 < _col_slots2Cur; ++cs2) {
		if (cs2->next_slot == 0) {
			f->writeUint32BE(0xFFFFFFFF);
		} else {
			f->writeUint32BE(cs2->next_slot - &_col_slots2[0]);
		}
		if (cs2->unk2 == 0) {
			f->writeUint32BE(0xFFFFFFFF);
		} else {
			f->writeUint32BE(cs2->unk2 - &_res._ctData[0x100]);
		}
		f->writeByte(cs2->data_size);
		f->write(cs2->data_buf, 0x10);
	}
}

bool Game::loadGameState(uint8_t slot) {
	bool success = false;
	char stateFile[20];
	makeGameStateName(slot, stateFile);
	File f;
	if (!f.open(stateFile, "zrb", _savePath)) {
		warning("Unable to open state file '%s'", stateFile);
	} else {
		uint32_t id = f.readUint32BE();
		if (id != TAG_FBSV) {
			warning("Bad save state format");
		} else {
			uint16_t ver = f.readUint16BE();
			if (ver != 2) {
				warning("Invalid save state version");
			} else {
				// header
				char buf[32];
				f.read(buf, sizeof(buf));
				// contents
				loadState(&f);
				if (f.ioErr()) {
					warning("I/O error when loading game state");
				} else {
					success = true;
				}
			}
		}
	}
	return success;
}

void Game::loadState(File *f) {
	uint16_t i;
	uint32_t off;
	_skillLevel = f->readByte();
	_score      = f->readUint32BE();
	memset(_pge_liveTable2, 0, sizeof(_pge_liveTable2));
	memset(_pge_liveTable1, 0, sizeof(_pge_liveTable1));
	off    = f->readUint32BE();
	if (off == 0xFFFFFFFF) {
		_col_slots2Cur = 0;
	} else {
		_col_slots2Cur = &_col_slots2[0] + off;
	}
	off    = f->readUint32BE();
	if (off == 0xFFFFFFFF) {
		_col_slots2Next = 0;
	} else {
		_col_slots2Next = &_col_slots2[0] + off;
	}
	for (i = 0; i < _res._pgeNum; ++i) {
		LivePGE *pge = &_pgeLive[i];
		pge->obj_type              = f->readUint16BE();
		pge->pos_x                 = f->readUint16BE();
		pge->pos_y                 = f->readUint16BE();
		pge->anim_seq              = f->readByte();
		pge->room_location         = f->readByte();
		pge->life                  = f->readUint16BE();
		pge->counter_value         = f->readUint16BE();
		pge->collision_slot        = f->readByte();
		pge->next_inventory_PGE    = f->readByte();
		pge->current_inventory_PGE = f->readByte();
		pge->unkF                  = f->readByte();
		pge->anim_number           = f->readUint16BE();
		pge->flags                 = f->readByte();
		pge->index                 = f->readByte();
		pge->first_obj_number      = f->readUint16BE();
		off = f->readUint32BE();
		if (off == 0xFFFFFFFF) {
			pge->next_PGE_in_room = 0;
		} else {
			pge->next_PGE_in_room = &_pgeLive[0] + off;
		}
		off = f->readUint32BE();
		if (off == 0xFFFFFFFF) {
			pge->init_PGE = 0;
		} else {
			pge->init_PGE = &_res._pgeInit[0] + off;
		}
	}
	f->read(&_res._ctData[0x100], 0x1C00);
	for (CollisionSlot2 *cs2 = &_col_slots2[0]; cs2 < _col_slots2Cur; ++cs2) {
		off = f->readUint32BE();
		if (off == 0xFFFFFFFF) {
			cs2->next_slot = 0;
		} else {
			cs2->next_slot = &_col_slots2[0] + off;
		}
		off            = f->readUint32BE();
		if (off == 0xFFFFFFFF) {
			cs2->unk2 = 0;
		} else {
			cs2->unk2 = &_res._ctData[0x100] + off;
		}
		cs2->data_size = f->readByte();
		f->read(cs2->data_buf, 0x10);
	}
	for (i = 0; i < _res._pgeNum; ++i) {
		if (_res._pgeInit[i].skill <= _skillLevel) {
			LivePGE *pge = &_pgeLive[i];
			if (pge->flags & 4) {
				_pge_liveTable2[pge->index] = pge;
			}
			pge->next_PGE_in_room = _pge_liveTable1[pge->room_location];
			_pge_liveTable1[pge->room_location] = pge;
		}
	}
	resetGameState();
}

void AnimBuffers::addState(uint8_t stateNum, int16_t x, int16_t y, const uint8_t *dataPtr, LivePGE *pge, uint8_t w,
                           uint8_t h) {
	assert(stateNum < 4);
	AnimBufferState *state = _states[stateNum];
	state->x       = x;
	state->y       = y;
	state->w       = w;
	state->h       = h;
	state->dataPtr = dataPtr;
	state->pge     = pge;
	++_curPos[stateNum];
	++_states[stateNum];
}
