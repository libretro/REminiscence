
/*
 * REminiscence - Flashback interpreter
 * Copyright (C) 2005-2015 Gregory Montoir (cyx@users.sourceforge.net)
 */

#include <SDL.h>
#include <ctype.h>
#include <getopt.h>
#include <sys/stat.h>
#include "file.h"
#include "fs.h"
#include "game.h"
#include "util.h"

static const char *USAGE =
	                  "REminiscence - Flashback Interpreter\n"
	                  "Usage: %s [OPTIONS]...\n"
	                  "  --datapath=PATH   Path to data files (default 'DATA')\n"
	                  "  --savepath=PATH   Path to save files (default '.')\n"
	                  "  --levelnum=NUM    Start to level, bypass introduction\n"
	                  "  --fullscreen      Fullscreen display\n"
	                  "  --language=LANG   Language (fr,en,de,sp,it,jp)\n"
;

static int detectVersion(FileSystem *fs) {
	static const struct {
		const char *filename;
		int type;
		const char *name;
	} table[] = {
		{ "DEMO_UK.ABA", kResourceTypeDOS, "DOS (Demo)" },
		{ "INTRO.SEQ", kResourceTypeDOS, "DOS CD" },
		{ "LEVEL1.MAP", kResourceTypeDOS, "DOS" },
		{ "LEVEL1.LEV", kResourceTypeAmiga, "Amiga" },
		{ "DEMO.LEV", kResourceTypeAmiga, "Amiga (Demo)" },
		{ 0, -1, 0 }
	};
	for (int i = 0; table[i].filename; ++i) {
		File f;
		if (f.open(table[i].filename, "rb", fs)) {
			debug(DBG_INFO, "Detected %s version", table[i].name);
			return table[i].type;
		}
	}
	return -1;
}

static Language detectLanguage(FileSystem *fs) {
	static const struct {
		const char *filename;
		Language language;
	} table[] = {
		// PC
		{ "ENGCINE.TXT", LANG_EN },
		{ "FR_CINE.TXT", LANG_FR },
		{ "GERCINE.TXT", LANG_DE },
		{ "SPACINE.TXT", LANG_SP },
		{ "ITACINE.TXT", LANG_IT },
		// Amiga
		{ "FRCINE.TXT", LANG_FR },
		{ 0, LANG_EN }
	};
	for (int i = 0; table[i].filename; ++i) {
		File f;
		if (f.open(table[i].filename, "rb", fs)) {
			return table[i].language;
		}
	}
	return LANG_EN;
}

const char *g_caption = "REminiscence";

static void initOptions() {
	// defaults
	g_options.play_disabled_cutscenes = false;
	g_options.enable_password_menu = false;
	g_options.use_text_cutscenes = false;
	g_options.use_seq_cutscenes = true;
	// read configuration file
	struct {
		const char *name;
		bool *value;
	} opts[] = {
		{ "play_disabled_cutscenes", &g_options.play_disabled_cutscenes },
		{ "enable_password_menu", &g_options.enable_password_menu },
		{ "use_text_cutscenes", &g_options.use_text_cutscenes },
		{ "use_seq_cutscenes", &g_options.use_seq_cutscenes },
		{ 0, 0 }
	};
	static const char *filename = "rs.cfg";
	FILE *fp = fopen(filename, "rb");
	if (fp) {
		char buf[256];
		while (fgets(buf, sizeof(buf), fp)) {
			if (buf[0] == '#') {
				continue;
			}
			const char *p = strchr(buf, '=');
			if (p) {
				++p;
				while (*p && isspace(*p)) {
					++p;
				}
				if (*p) {
					const bool value = (*p == 't' || *p == 'T' || *p == '1');
					for (int i = 0; opts[i].name; ++i) {
						if (strncmp(buf, opts[i].name, strlen(opts[i].name)) == 0) {
							*opts[i].value = value;
							break;
						}
					}
				}
			}
		}
		fclose(fp);
	}
}

static const int kJoystickCommitValue = 3200;

void processEvent(const SDL_Event &ev, PlayerInput &_pi, SDL_Joystick *_joystick, SDL_GameController *_controller) {
	switch (ev.type) {
	case SDL_QUIT:
		_pi.quit = true;
		break;
	case SDL_JOYHATMOTION:
		if (_joystick) {
			_pi.dirMask = 0;
			if (ev.jhat.value & SDL_HAT_UP) {
				_pi.dirMask |= PlayerInput::DIR_UP;
			}
			if (ev.jhat.value & SDL_HAT_DOWN) {
				_pi.dirMask |= PlayerInput::DIR_DOWN;
			}
			if (ev.jhat.value & SDL_HAT_LEFT) {
				_pi.dirMask |= PlayerInput::DIR_LEFT;
			}
			if (ev.jhat.value & SDL_HAT_RIGHT) {
				_pi.dirMask |= PlayerInput::DIR_RIGHT;
			}
		}
		break;
	case SDL_JOYAXISMOTION:
		if (_joystick) {
			switch (ev.jaxis.axis) {
			case 0:
				_pi.dirMask &= ~(PlayerInput::DIR_RIGHT | PlayerInput::DIR_LEFT);
				if (ev.jaxis.value > kJoystickCommitValue) {
					_pi.dirMask |= PlayerInput::DIR_RIGHT;
				} else if (ev.jaxis.value < -kJoystickCommitValue) {
					_pi.dirMask |= PlayerInput::DIR_LEFT;
				}
				break;
			case 1:
				_pi.dirMask &= ~(PlayerInput::DIR_UP | PlayerInput::DIR_DOWN);
				if (ev.jaxis.value > kJoystickCommitValue) {
					_pi.dirMask |= PlayerInput::DIR_DOWN;
				} else if (ev.jaxis.value < -kJoystickCommitValue) {
					_pi.dirMask |= PlayerInput::DIR_UP;
				}
				break;
			}
		}
		break;
	case SDL_JOYBUTTONDOWN:
	case SDL_JOYBUTTONUP:
		if (_joystick) {
			const bool pressed = (ev.jbutton.state == SDL_PRESSED);
			switch (ev.jbutton.button) {
			case 0:
				_pi.weapon = pressed;
				break;
			case 1:
				_pi.action = pressed;
				break;
			case 2:
				_pi.use = pressed;
				break;
			case 3:
				_pi.inventory_skip = pressed;
				break;
			}
		}
		break;
	case SDL_CONTROLLERAXISMOTION:
		if (_controller) {
			switch (ev.caxis.axis) {
			case SDL_CONTROLLER_AXIS_LEFTX:
			case SDL_CONTROLLER_AXIS_RIGHTX:
				if (ev.caxis.value < -kJoystickCommitValue) {
					_pi.dirMask |= PlayerInput::DIR_LEFT;
				} else {
					_pi.dirMask &= ~PlayerInput::DIR_LEFT;
				}
				if (ev.caxis.value > kJoystickCommitValue) {
					_pi.dirMask |= PlayerInput::DIR_RIGHT;
				} else {
					_pi.dirMask &= ~PlayerInput::DIR_RIGHT;
				}
				break;
			case SDL_CONTROLLER_AXIS_LEFTY:
			case SDL_CONTROLLER_AXIS_RIGHTY:
				if (ev.caxis.value < -kJoystickCommitValue) {
					_pi.dirMask |= PlayerInput::DIR_UP;
				} else {
					_pi.dirMask &= ~PlayerInput::DIR_UP;
				}
				if (ev.caxis.value > kJoystickCommitValue) {
					_pi.dirMask |= PlayerInput::DIR_DOWN;
				} else {
					_pi.dirMask &= ~PlayerInput::DIR_DOWN;
				}
				break;
			}
		}
		break;
	case SDL_CONTROLLERBUTTONDOWN:
	case SDL_CONTROLLERBUTTONUP:
		if (_controller) {
			const bool pressed = (ev.cbutton.state == SDL_PRESSED);
			switch (ev.cbutton.button) {
			case SDL_CONTROLLER_BUTTON_A:
				_pi.use = pressed; // USE
				break;
			case SDL_CONTROLLER_BUTTON_B:
				_pi.weapon = pressed; // ARM / DRAW or HOLSTER
				break;
			case SDL_CONTROLLER_BUTTON_X:
				_pi.action = pressed; // ACTION
				break;
			case SDL_CONTROLLER_BUTTON_Y:
				_pi.inventory_skip = pressed; // INVENTORY / SKIP ANIMATION
				break;
			case SDL_CONTROLLER_BUTTON_BACK:
			case SDL_CONTROLLER_BUTTON_START:
				_pi.escape = pressed;
				break;
			case SDL_CONTROLLER_BUTTON_DPAD_UP:
				if (pressed) {
					_pi.dirMask |= PlayerInput::DIR_UP;
				} else {
					_pi.dirMask &= ~PlayerInput::DIR_UP;
				}
				break;
			case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
				if (pressed) {
					_pi.dirMask |= PlayerInput::DIR_DOWN;
				} else {
					_pi.dirMask &= ~PlayerInput::DIR_DOWN;
				}
				break;
			case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
				if (pressed) {
					_pi.dirMask |= PlayerInput::DIR_LEFT;
				} else {
					_pi.dirMask &= ~PlayerInput::DIR_LEFT;
				}
				break;
			case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
				if (pressed) {
					_pi.dirMask |= PlayerInput::DIR_RIGHT;
				} else {
					_pi.dirMask &= ~PlayerInput::DIR_RIGHT;
				}
				break;
			}
		}
		break;
	case SDL_KEYUP:
		if (ev.key.keysym.mod & KMOD_CTRL) {
			switch (ev.key.keysym.sym) {
			case SDLK_f:
				_pi.dbgMask ^= PlayerInput::DF_FASTMODE;
				break;
			case SDLK_b:
				_pi.dbgMask ^= PlayerInput::DF_DBLOCKS;
				break;
			case SDLK_i:
				_pi.dbgMask ^= PlayerInput::DF_SETLIFE;
				break;
			case SDLK_s:
				_pi.save = true;
				break;
			case SDLK_l:
				_pi.load = true;
				break;
			case SDLK_KP_PLUS:
			case SDLK_PAGEUP:
				_pi.stateSlot = 1;
				break;
			case SDLK_KP_MINUS:
			case SDLK_PAGEDOWN:
				_pi.stateSlot = -1;
				break;
			}
			break;
		}
		_pi.lastChar = ev.key.keysym.sym;
		switch (ev.key.keysym.sym) {
		case SDLK_LEFT:
			_pi.dirMask &= ~PlayerInput::DIR_LEFT;
			break;
		case SDLK_RIGHT:
			_pi.dirMask &= ~PlayerInput::DIR_RIGHT;
			break;
		case SDLK_UP:
			_pi.dirMask &= ~PlayerInput::DIR_UP;
			break;
		case SDLK_DOWN:
			_pi.dirMask &= ~PlayerInput::DIR_DOWN;
			break;
		case SDLK_SPACE:
			_pi.weapon = false;
			break;
		case SDLK_RSHIFT:
		case SDLK_LSHIFT:
			_pi.action = false;
			break;
		case SDLK_RETURN:
			_pi.use = false;
			break;
		case SDLK_ESCAPE:
			_pi.escape = false;
			break;
		default:
			break;
		}
		break;
	case SDL_KEYDOWN:
		if (ev.key.keysym.mod & (KMOD_ALT | KMOD_CTRL)) {
			break;
		}
		switch (ev.key.keysym.sym) {
		case SDLK_LEFT:
			_pi.dirMask |= PlayerInput::DIR_LEFT;
			break;
		case SDLK_RIGHT:
			_pi.dirMask |= PlayerInput::DIR_RIGHT;
			break;
		case SDLK_UP:
			_pi.dirMask |= PlayerInput::DIR_UP;
			break;
		case SDLK_DOWN:
			_pi.dirMask |= PlayerInput::DIR_DOWN;
			break;
		case SDLK_BACKSPACE:
		case SDLK_TAB:
			_pi.inventory_skip = true;
			break;
		case SDLK_SPACE:
			_pi.weapon = true;
			break;
		case SDLK_RSHIFT:
		case SDLK_LSHIFT:
			_pi.action = true;
			break;
		case SDLK_RETURN:
			_pi.use = true;
			break;
		case SDLK_ESCAPE:
			_pi.escape = true;
			break;
		default:
			break;
		}
		break;
	default:
		break;
	}
}

int main(int argc, char *argv[]) {
	const char *dataPath = "DATA";
	const char *savePath = ".";
	int levelNum = 0;
	bool fullscreen = false;
	int forcedLanguage = -1;
	if (argc == 2) {
		// data path as the only command line argument
		struct stat st;
		if (stat(argv[1], &st) == 0 && S_ISDIR(st.st_mode)) {
			dataPath = strdup(argv[1]);
		}
	}
	while (1) {
		static struct option options[] = {
			{ "datapath",   required_argument, 0, 1 },
			{ "savepath",   required_argument, 0, 2 },
			{ "levelnum",   required_argument, 0, 3 },
			{ "fullscreen", no_argument,       0, 4 },
			{ "language",   required_argument, 0, 5 },
			{ 0, 0, 0, 0 }
		};
		int index;
		const int c = getopt_long(argc, argv, "", options, &index);
		if (c == -1) {
			break;
		}
		switch (c) {
		case 1:
			dataPath = strdup(optarg);
			break;
		case 2:
			savePath = strdup(optarg);
			break;
		case 3:
			levelNum = atoi(optarg);
			break;
		case 4:
			fullscreen = true;
			break;
		case 5: {
			static const struct {
				int lang;
				const char *str;
			} languages[] = {
				{ LANG_FR, "FR" },
				{ LANG_EN, "EN" },
				{ LANG_DE, "DE" },
				{ LANG_SP, "SP" },
				{ LANG_IT, "IT" },
				{ LANG_JP, "JP" },
				{ -1, 0 }
			};
			for (int i = 0; languages[i].str; ++i) {
				if (strcasecmp(languages[i].str, optarg) == 0) {
					forcedLanguage = languages[i].lang;
					break;
				}
			}
		}
			break;
		default:
			printf(USAGE, argv[0]);
			return 0;
		}
	}
	initOptions();
	g_debugMask = DBG_INFO; // DBG_CUT | DBG_VIDEO | DBG_RES | DBG_MENU | DBG_PGE | DBG_GAME | DBG_UNPACK | DBG_COL | DBG_MOD | DBG_SFX | DBG_FILE;
	FileSystem fs(dataPath);
	const int version = detectVersion(&fs);
	if (version != kResourceTypeDOS) {
		error("Unable to find DOS data files, check that all required files are present");
		return -1;
	}
	const Language language = (forcedLanguage == -1) ? detectLanguage(&fs) : (Language)forcedLanguage;
	Game *g = new Game(&fs, savePath, levelNum, language);
	g->init();

	SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER);
	SDL_ShowCursor(SDL_DISABLE);
	SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0"); // nearest pixel sampling

	SDL_Window *window = nullptr;
	SDL_Renderer *renderer = nullptr;
	SDL_Texture *texture = nullptr;
	SDL_GameController *controller = nullptr;
	SDL_Joystick *joystick = nullptr;

	// joystick
	if (SDL_NumJoysticks() > 0) {
		SDL_GameControllerAddMappingsFromFile("gamecontrollerdb.txt");
		if (SDL_IsGameController(0)) {
			controller = SDL_GameControllerOpen(0);
		}
		if (!controller) {
			joystick = SDL_JoystickOpen(0);
		}
	}

	uint32_t flags = 0;
	if (fullscreen) {
		flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
	} else {
		flags |= SDL_WINDOW_RESIZABLE;
	}

	int windowW = Video::GAMESCREEN_W*3;
	int windowH = Video::GAMESCREEN_H*3;

	window = SDL_CreateWindow(g_caption, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, windowW, windowH, flags);
	renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
	SDL_RenderSetLogicalSize(renderer, windowW, windowH);
	texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, Video::GAMESCREEN_W, Video::GAMESCREEN_H);
	SDL_AudioSpec desired, spec;
	memset(&desired, 0, sizeof(desired));
	memset(&spec, 0, sizeof(spec));

	uint16_t samplesPerFrame = g->getOutputSampleRate()/g->getFrameRate();
	uint16_t samples = samplesPerFrame*2;

	desired.freq = g->getOutputSampleRate();
	desired.format = AUDIO_S16SYS;
	desired.channels = 1;
	desired.samples = samples;
	auto dev = SDL_OpenAudioDevice(nullptr, 0, &desired, &spec, 0);
	SDL_PauseAudioDevice(dev, 0);

	while(!g->_pi.quit) {
		uint32_t start = SDL_GetTicks();
		g->tick();

		SDL_UpdateTexture(texture, nullptr, g->getFrameBuffer(), Video::GAMESCREEN_W * sizeof(uint32_t));
		SDL_RenderClear(renderer);
		SDL_RenderCopy(renderer, texture, nullptr, nullptr);
		SDL_RenderPresent(renderer);

		SDL_Event ev;
		while (SDL_PollEvent(&ev)) {
			processEvent(ev, g->_pi, joystick, controller);
			if (g->_pi.quit) {
				break;
			}
		}

		int qa = SDL_GetQueuedAudioSize(dev);
		if (qa < samples) {
			int16_t buf[2048];
			memset(buf, 0, 2048*sizeof(int16_t));

			g->processFragment(buf, samples-qa);
			SDL_QueueAudio(dev, buf, (samples-qa)*sizeof(int16_t));
		}

		uint32_t delta = SDL_GetTicks() - start;

		int sleep = MS_PER_FRAME - delta;
		if (sleep > 0) {
			SDL_Delay(static_cast<Uint32>(sleep));
		}
	}

	delete g;
	return 0;
}
