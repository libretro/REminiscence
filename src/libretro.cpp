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
#include "video.h"
#include <file/file_path.h>

#define RE_VERSION "0.3.6"

static const int kAudioHz = 44100;

FileSystem  *fs;
Game        *game;
PlayerInput lastInput;

retro_log_printf_t          log_cb;
static retro_video_refresh_t       video_cb;
static retro_input_poll_t          input_poll_cb;
static retro_input_state_t         input_state_cb;
static retro_environment_t         environ_cb;
static retro_audio_sample_t        audio_cb;
static retro_audio_sample_batch_t  audio_batch_cb;

static bool libretro_supports_bitmasks = false;
static int16_t joypad_bits;

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
	info->library_name     = "REminiscence";
	info->library_version  = RE_VERSION;
	info->need_fullpath    = true;
	info->valid_extensions = "map";
}

void retro_get_system_av_info(struct retro_system_av_info *info) {
	memset(info, 0, sizeof(*info));
	info->timing.fps            = 50.0;
	info->timing.sample_rate    = kAudioHz;
	info->geometry.base_width   = Video::GAMESCREEN_W;
	info->geometry.base_height  = Video::GAMESCREEN_H;
	info->geometry.max_width    = 1024;
	info->geometry.max_height   = 768;
	info->geometry.aspect_ratio = 8.0f / 7.0f;
}

void retro_set_controller_port_device(unsigned port, unsigned device) {
	(void) port;
	(void) device;
}

size_t retro_serialize_size(void) {
	return 128 * 1024; // arbitrary max size?
}

bool retro_serialize(void *data, size_t size)
{
   File f;

   if (!game->isStateMainLoop())
      return false;

   f.open(new MemFile(static_cast<uint8_t *>(data),
            static_cast<uint32_t>(size)));
   game->saveState(&f);

   return !f.ioErr();
}

bool retro_unserialize(const void *data, size_t size)
{
   File f;
	if (!game->isStateMainLoop())
		return false;

	f.open(new ReadOnlyMemFile(static_cast<const uint8_t *>(data),
            static_cast<uint32_t>(size)));
	game->loadState(&f);

	return !f.ioErr();
}

void retro_cheat_reset(void) {}

void retro_cheat_set(unsigned index, bool enabled, const char *code)
{
	(void) index;
	(void) enabled;
	(void) code;
}

static int detectVersion(FileSystem *fs)
{
   unsigned i;
   static const struct {
      const char *filename;
      int        type;
      const char *name;
   }        table[] = {
      {"DEMO_UK.ABA", kResourceTypeDOS,   "DOS (Demo)"},
      {"INTRO.SEQ",   kResourceTypeDOS,   "DOS CD"},
      {"LEVEL1.MAP",  kResourceTypeDOS,   "DOS"},
      {"LEVEL1.LEV",  kResourceTypeAmiga, "Amiga"},
      {"DEMO.LEV",    kResourceTypeAmiga, "Amiga (Demo)"},
      {0,             -1,                 0}
   };

   for (i = 0; table[i].filename; ++i)
   {
      File f;
      if (f.open(table[i].filename, "rb", fs))
         return table[i].type;
   }
   return -1;
}

static Language detectLanguage(FileSystem *fs)
{
   unsigned i;
   static const struct {
      const char *filename;
      Language   language;
   }        table[] = {
      // PC
      {"ENGCINE.TXT", LANG_EN},
      {"FR_CINE.TXT", LANG_FR},
      {"GERCINE.TXT", LANG_DE},
      {"SPACINE.TXT", LANG_SP},
      {"ITACINE.TXT", LANG_IT},
      // Amiga
      {"FRCINE.TXT",  LANG_FR},
      {0,             LANG_EN}
   };
   for (i = 0; table[i].filename; ++i)
   {
      File f;
      if (f.open(table[i].filename, "rb", fs))
         return table[i].language;
   }
   return LANG_EN;
}

bool retro_load_game(const struct retro_game_info *info)
{
	enum retro_pixel_format fmt = RETRO_PIXEL_FORMAT_XRGB8888;

	struct retro_input_descriptor desc[] = {
		{0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT,  "Left"},
		{0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP,    "Up"},
		{0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN,  "Down"},
		{0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT, "Right"},
		{0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X,     "Action"},
		{0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B,     "Draw / Holster"},
		{0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A,     "Use"},
		{0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y,     "Inventory / Skip"},

		{0},
	};

	char *dataPath = strdup(info->path);
	path_basedir(dataPath);
	fs = new FileSystem(dataPath);
	free(dataPath);
	const int version = detectVersion(fs);
	if (version != kResourceTypeDOS)
		return false;

	environ_cb(RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS, desc);

	if (!environ_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt))
   {
		if (log_cb)
			log_cb(RETRO_LOG_INFO, "[RE]: XRGB8888 is not supported.\n");
		return false;
	}

	const Language language = detectLanguage(fs);
	game = new Game(fs, "", 0, language);
	game->init();
	memset(&lastInput, 0, sizeof(lastInput));

	return true;
}

bool retro_load_game_special(unsigned game_type,
      const struct retro_game_info *info, size_t num_info)
{
	(void) game_type;
	(void) info;
	(void) num_info;
	return false;
}

void retro_unload_game(void)
{
   if (game)
   {
      delete game;
      game = NULL;
   }
   if (fs)
   {
      delete fs;
      fs = NULL;
   }
}

unsigned retro_get_region(void)
{
	return RETRO_REGION_NTSC;
}

unsigned retro_api_version(void)
{
   return RETRO_API_VERSION;
}

void *retro_get_memory_data(unsigned id)
{
   switch (id)
   {
      case RETRO_MEMORY_VIDEO_RAM:
         return game->getFrameBuffer();
      case RETRO_MEMORY_SYSTEM_RAM:
      default:
         break;
   }

   return NULL;
}

size_t retro_get_memory_size(unsigned id)
{
   switch (id)
   {
      case RETRO_MEMORY_SYSTEM_RAM:
         return 128;
      case RETRO_MEMORY_VIDEO_RAM:
         return Video::GAMESCREEN_SIZE * sizeof(uint32_t);
      default:
         break;
   }

   return 0;
}

void retro_init(void)
{
	struct retro_log_callback log;
	unsigned                  level = 2;

	if (environ_cb(RETRO_ENVIRONMENT_GET_LOG_INTERFACE, &log))
		log_cb = log.log;
	else
		log_cb = NULL;

	environ_cb(RETRO_ENVIRONMENT_SET_PERFORMANCE_LEVEL, &level);

	if (environ_cb(RETRO_ENVIRONMENT_GET_INPUT_BITMASKS, NULL))
		libretro_supports_bitmasks = true;
}

void retro_deinit(void)
{
	libretro_supports_bitmasks = false;
}

void retro_reset(void)
{
	game->resetGameState();
}

static void update_button(unsigned int id, bool &button, bool *old_val)
{
   bool new_val;

   if (libretro_supports_bitmasks)
      new_val = joypad_bits & (1 << id) ? 1 : 0;
   else
      new_val = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, id) != 0;

   if (old_val == NULL || new_val != *old_val)
   {
      button = new_val;
      if (old_val)
         *old_val = new_val;
   }
}

struct dir_map_t {
   unsigned int retro;
   int          player;
};

static void update_input(void)
{
   unsigned i;

   struct dir_map_t joy_map[] = {
      {RETRO_DEVICE_ID_JOYPAD_UP,    PlayerInput::DIR_UP},
      {RETRO_DEVICE_ID_JOYPAD_RIGHT, PlayerInput::DIR_RIGHT},
      {RETRO_DEVICE_ID_JOYPAD_DOWN,  PlayerInput::DIR_DOWN},
      {RETRO_DEVICE_ID_JOYPAD_LEFT,  PlayerInput::DIR_LEFT},
   };

   if (!input_poll_cb)
      return;

   input_poll_cb();

   PlayerInput &pi = game->_pi;

   pi.dirMask = 0;

   if (libretro_supports_bitmasks)
      joypad_bits = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_MASK);
   else
   {
      joypad_bits = 0;
      for (i = 0; i < (RETRO_DEVICE_ID_JOYPAD_R3+1); i++)
         joypad_bits |= input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, i) ? (1 << i) : 0;
   }

   for (i = 0; i < ARRAY_SIZE(joy_map); i++)
      pi.dirMask |= joypad_bits & (1 << joy_map[i].retro) ? joy_map[i].player : 0;

   update_button(RETRO_DEVICE_ID_JOYPAD_X, pi.action, NULL);
   update_button(RETRO_DEVICE_ID_JOYPAD_B, pi.weapon, NULL);
   update_button(RETRO_DEVICE_ID_JOYPAD_A, pi.use, &lastInput.use);
   update_button(RETRO_DEVICE_ID_JOYPAD_Y, pi.inventory_skip,
         &lastInput.inventory_skip);
}

void retro_run(void)
{
   unsigned i;
   static int16_t sampleBuffer[2048];
   static int16_t stereoBuffer[2048];
   /* Get the number of samples in a frame */
   uint16_t samplesPerFrame = game->getOutputSampleRate()
      / game->getFrameRate();

   //INPUT
   update_input();

   //EMULATE
   game->tick();

   //VIDEO
   video_cb(game->getFrameBuffer(),
         Video::GAMESCREEN_W, Video::GAMESCREEN_H,
         Video::GAMESCREEN_W * sizeof(uint32_t));

   //AUDIO
   memset(sampleBuffer, 0, samplesPerFrame * sizeof(int16_t));
   game->processFragment(sampleBuffer, samplesPerFrame);

   int16_t        *p        = stereoBuffer;
   for (i = 0; i < samplesPerFrame; i++)
   {
      p[0] = sampleBuffer[i];
      p[1] = sampleBuffer[i];
      p += 2;
   }

   audio_batch_cb((int16_t *) stereoBuffer, samplesPerFrame);
}
