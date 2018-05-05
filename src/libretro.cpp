//
//  libretro.cpp
//  REminiscence-retro
//
//  Created by Stuart Carnie on 4/28/18.
//

#include "libretro.h"
#include "file.h"
#include "fs.h"
#include "game.h"
#include "systemstub.h"
#include "util.h"
#include "video.h"

#define RE_VERSION "0.3.6"

struct SystemStub_libretro : SystemStub {

};

static const int kAudioHz = 22050;

const char *g_caption = "REminiscence";

Game *game;
SystemStub *runtime;


static retro_log_printf_t log_cb;
static retro_video_refresh_t video_cb;
static retro_input_poll_t input_poll_cb;
static retro_input_state_t input_state_cb;
static retro_environment_t environ_cb;
static retro_audio_sample_t audio_cb;
static retro_audio_sample_batch_t audio_batch_cb;
static struct retro_system_av_info g_av_info;

/************************************
 * libretro implementation
 ************************************/


void retro_set_environment(retro_environment_t cb) { environ_cb = cb; }

void retro_set_video_refresh(retro_video_refresh_t cb) { video_cb = cb; }

void retro_set_audio_sample(retro_audio_sample_t cb) { audio_cb = cb; }

void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb) { audio_batch_cb = cb; }

void retro_set_input_poll(retro_input_poll_t cb) { input_poll_cb = cb; }

void retro_set_input_state(retro_input_state_t cb) { input_state_cb = cb; }

void retro_get_system_info(struct retro_system_info *info) {
	memset(info, 0, sizeof(*info));
	info->library_name = "REminiscence";
	info->library_version = RE_VERSION;
	info->need_fullpath = false;
	info->valid_extensions = "";
}

void retro_get_system_av_info(struct retro_system_av_info *info) {
	memset(info, 0, sizeof(*info));
	info->timing.fps = 50.0;
	info->timing.sample_rate = kAudioHz;
	info->geometry.base_width = Video::GAMESCREEN_W;
	info->geometry.base_height = Video::GAMESCREEN_H;
	info->geometry.max_width = 1024;
	info->geometry.max_height = 768;
	info->geometry.aspect_ratio = 8.0f / 7.0f;
}

void retro_set_controller_port_device(unsigned port, unsigned device) {
	(void) port;
	(void) device;
}

size_t retro_serialize_size(void) {
	return 0;
}

bool retro_serialize(void *data, size_t size) {
	return false;
}

bool retro_unserialize(const void *data, size_t size) {
	return false;
}

void retro_cheat_reset(void) {}

void retro_cheat_set(unsigned index, bool enabled, const char *code) {
	(void) index;
	(void) enabled;
	(void) code;
}

bool retro_load_game(const struct retro_game_info *info) {
	enum retro_pixel_format fmt = RETRO_PIXEL_FORMAT_XRGB8888;

	struct retro_input_descriptor desc[] = {
			{0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT,   "Left"},
			{0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP,     "Up"},
			{0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN,   "Down"},
			{0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT,  "Right"},
			{0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B,      "Fire"},
			{0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L,      "Left Difficulty A"},
			{0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L2,     "Left Difficulty B"},
			{0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L3,     "Color"},
			{0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R,      "Right Difficulty A"},
			{0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R2,     "Right Difficulty B"},
			{0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R3,     "Black/White"},
			{0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT, "Select"},
			{0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START,  "Reset"},

			{1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT,   "Left"},
			{1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP,     "Up"},
			{1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN,   "Down"},
			{1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT,  "Right"},
			{1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B,      "Fire"},

			{0},
	};

	if (!info || info->size >= 96 * 1024)
		return false;

	environ_cb(RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS, desc);

	if (!environ_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt)) {
		if (log_cb)
			log_cb(RETRO_LOG_INFO, "[RE]: XRGB8888 is not supported.\n");
		return false;
	}


	// Get the game properties
	string cartMD5 = MD5((const uInt8 *) info->data, (uInt32) info->size);
	Properties props;
	osystem.propSet().getMD5(cartMD5, props);

	// Load the cart
	string cartType = props.get(Cartridge_Type);
	string cartId;//, romType("AUTO-DETECT");
	settings = new Settings(&osystem);
	settings->setValue("romloadcount", false);
	cartridge = Cartridge::create((const uInt8 *) info->data, (uInt32) info->size, cartMD5, cartType, cartId, osystem,
								  *settings);

	if (cartridge == 0) {
		if (log_cb)
			log_cb(RETRO_LOG_ERROR, "Stella: Failed to load cartridge.\n");
		return false;
	}

	// Create the console
	console = new Console(&osystem, cartridge, props);
	osystem.myConsole = console;

	// Init sound and video
	console->initializeVideo();
	console->initializeAudio();

	// Get the ROM's width and height
	TIA &tia = console->tia();
	videoWidth = tia.width();
	videoHeight = tia.height();

	return true;
}

bool retro_load_game_special(unsigned game_type, const struct retro_game_info *info, size_t num_info) {
	(void) game_type;
	(void) info;
	(void) num_info;
	return false;
}

void retro_unload_game(void) {
	if (game) {
		delete game;
		game = nullptr;
	}

	if (runtime) {
		delete runtime;
		runtime = nullptr;
	}
}

unsigned retro_get_region(void) {
	//console->getFramerate();
	return RETRO_REGION_NTSC;
}

unsigned retro_api_version(void) {
	return RETRO_API_VERSION;
}

void *retro_get_memory_data(unsigned id) {
	switch (id) {
		case RETRO_MEMORY_SYSTEM_RAM:
			return console->system().m6532().getRAM();
		default:
			return NULL;
	}
}

size_t retro_get_memory_size(unsigned id) {
	switch (id) {
		case RETRO_MEMORY_SYSTEM_RAM:
			return 128;
		default:
			return 0;
	}
}

void retro_init(void) {
	struct retro_log_callback log;
	unsigned level = 4;

	if (environ_cb(RETRO_ENVIRONMENT_GET_LOG_INTERFACE, &log))
		log_cb = log.log;
	else
		log_cb = NULL;

	environ_cb(RETRO_ENVIRONMENT_SET_PERFORMANCE_LEVEL, &level);
}

void retro_deinit(void) {
}

void retro_reset(void) {
	game->resetGameState();
}

void retro_run(void) {

	static int16_t *sampleBuffer[2048];
	static uint32_t frameBuffer[256 * 160];
	//Get the number of samples in a frame
	static uint32_t tiaSamplesPerFrame = (uint32_t) (31400.0f / console->getFramerate());

	//INPUT
	update_input();

	//EMULATE
	TIA &tia = console->tia();
	tia.update();

	//VIDEO
	//Get the frame info from stella
	videoWidth = tia.width();
	videoHeight = tia.height();

	const uint32_t *palette = console->getPalette(0);
	//Copy the frame from stella to libretro
	for (int i = 0; i < videoHeight * videoWidth; ++i)
		frameBuffer[i] = palette[tia.currentFrameBuffer()[i]];

	video_cb(frameBuffer, videoWidth, videoHeight, videoWidth << 2);

	//AUDIO
	//Process one frame of audio from stella
	SoundSDL *sound = (SoundSDL * ) & osystem.sound();
	sound->processFragment((int16_t *) sampleBuffer, tiaSamplesPerFrame);

	audio_batch_cb((int16_t *) sampleBuffer, tiaSamplesPerFrame);
}
