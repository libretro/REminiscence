
/*
 * REminiscence - Flashback interpreter
 * Copyright (C) 2005-2015 Gregory Montoir (cyx@users.sourceforge.net)
 */

#include "mixer.h"
#include "game.h"

Mixer::Mixer(FileSystem *fs, Game *game)
	: _game(game), _musicType(MT_NONE), _mod(this, fs), _sfx(this) {
}

void Mixer::init()
{
	unsigned i;
	for (i = 0; i < NUM_CHANNELS; ++i)
	{
		MixerChannel *cur = &_channels[i];
		cur->active       = false;
		cur->volume       = 0;
		cur->chunk.data   = NULL;
		cur->chunk.len    = 0;
		cur->chunkPos     = 0;
		cur->chunkInc     = 0;
	}
	_premixHook = 0;
}

void Mixer::free()
{
	setPremixHook(0, 0);
	stopAll();
}

void Mixer::setPremixHook(PremixHook premixHook, void *userData)
{
	_premixHook = premixHook;
	_premixHookData = userData;
}

void Mixer::play(const MixerChunk *mc, uint16_t freq, uint8_t volume)
{
   unsigned i;
   MixerChannel *ch = NULL;

   for (i = 0; i < NUM_CHANNELS; ++i)
   {
      MixerChannel *cur = &_channels[i];
      if (cur->active)
      {
         if (cur->chunk.data == mc->data)
         {
            cur->chunkPos = 0;
            return;
         }
      }
      else
      {
         ch = cur;
         break;
      }
   }

   if (ch)
   {
      ch->active = true;
      ch->volume = volume;
      ch->chunk = *mc;
      ch->chunkPos = 0;
      ch->chunkInc = (freq << FRAC_BITS) / _game->getOutputSampleRate();
   }
}

bool Mixer::isPlaying(const MixerChunk *mc) const
{
   unsigned i;
   for (i = 0; i < NUM_CHANNELS; ++i)
   {
	   const MixerChannel *ch = &_channels[i];
	   if (ch->active && ch->chunk.data == mc->data)
		   return true;
   }
   return false;
}

uint32_t Mixer::getSampleRate() const {
	return _game->getOutputSampleRate();
}

void Mixer::stopAll() {
   unsigned i;

   for (i = 0; i < NUM_CHANNELS; ++i)
      _channels[i].active = false;
}

static bool isMusicSfx(int num)
{
	return (num >= 68 && num <= 75);
}

void Mixer::playMusic(int num)
{
	if (isMusicSfx(num))
	{
		/* level action sequence */
		_sfx.play(num);
		if (_sfx._playing)
			_musicType = MT_SFX;
	}
	else
	{
		/* cutscene */
		_mod.play(num);
		if (_mod._playing)
			_musicType = MT_MOD;
	}
}

void Mixer::stopMusic()
{
   switch (_musicType)
   {
      case MT_NONE:
         break;
      case MT_MOD:
         _mod.stop();
         break;
      case MT_SFX:
         _sfx.stop();
         break;
   }
   _musicType = MT_NONE;
}

void Mixer::mix(int16_t *out, int len)
{
   unsigned i;
	if (_premixHook)
   {
		if (!_premixHook(_premixHookData, out, len))
      {
			_premixHook = 0;
			_premixHookData = 0;
		}
	}
	for (i = 0; i < NUM_CHANNELS; ++i)
   {
		MixerChannel *ch = &_channels[i];
		if (ch->active)
      {
         int pos;
         for (pos = 0; pos < len; ++pos)
         {
            int sample;
            if ((ch->chunkPos >> FRAC_BITS) >= (ch->chunk.len - 1))
            {
               ch->active = false;
               break;
            }
            sample        = ch->chunk.getPCM(ch->chunkPos >> FRAC_BITS);
            out[pos]      = ADDC_S16(out[pos], (sample * ch->volume / Mixer::MAX_VOLUME) << 8);
            ch->chunkPos += ch->chunkInc;
         }
      }
	}
}

void Mixer::mixCallback(void *param, int16_t *buf, int len)
{
	((Mixer *)param)->mix(buf, len);
}
