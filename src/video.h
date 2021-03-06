
/*
 * REminiscence - Flashback interpreter
 * Copyright (C) 2005-2015 Gregory Montoir (cyx@users.sourceforge.net)
 */

#ifndef VIDEO_H__
#define VIDEO_H__

#include "intern.h"

struct Resource;
struct Game;

struct Video {
	enum {
		GAMESCREEN_W    = 256,
		GAMESCREEN_H    = 224,
		GAMESCREEN_SIZE = GAMESCREEN_W * GAMESCREEN_H,
		CHAR_W          = 8,
		CHAR_H          = 8
	};

	static const uint8_t _conradPal1[];
	static const uint8_t _conradPal2[];
	static const uint8_t _textPal[];
	static const uint8_t _palSlot0xF[];
	static const uint8_t _font8Jp[];

	Resource *_res;
	Game     *_game;

	uint8_t *_frontLayer; // drawing layer
	uint8_t *_backLayer;  // background layer; used to clear screen between frames
	uint8_t *_tempLayer;
	uint8_t *_tempLayer2;
	uint8_t _unkPalSlot1, _unkPalSlot2;
	uint8_t _mapPalSlot1, _mapPalSlot2, _mapPalSlot3, _mapPalSlot4;
	uint8_t _charFrontColor;
	uint8_t _charTransparentColor;
	uint8_t _charShadowColor;
	uint8_t _shakeOffset;

	uint32_t _rgbPalette[256];
	uint32_t *_frameBuffer;

	Video(Resource *res, Game *game);
	~Video();

	void updateScreen();
	void fadeOut();
	void fadeOutPalette();

	// frame buffer
	void setPalette(const uint8_t *pal, int n);
	void setPaletteEntry(int i, const Color *c);
	void getPaletteEntry(int i, Color *c);
	void copyRect(int x, int y, int w, int h, const uint8_t *buf, int pitch);

	void setPaletteSlotBE(int palSlot, int palNum);
	void setPaletteSlotLE(int palSlot, const uint8_t *palData);
	void setTextPalette();
	void setPalette0xF();
	void PC_decodeLev(int level, int room);
	void PC_decodeMap(int level, int room);
	void PC_setLevelPalettes();
	void PC_decodeIcn(const uint8_t *src, int num, uint8_t *dst);
	void PC_decodeSpc(const uint8_t *src, int w, int h, uint8_t *dst);
	void AMIGA_decodeLev(int level, int room);

	void drawSpriteSub1(const uint8_t *src, uint8_t *dst, int pitch, int h, int w, uint8_t colMask);
	void drawSpriteSub2(const uint8_t *src, uint8_t *dst, int pitch, int h, int w, uint8_t colMask);
	void drawSpriteSub3(const uint8_t *src, uint8_t *dst, int pitch, int h, int w, uint8_t colMask);
	void drawSpriteSub4(const uint8_t *src, uint8_t *dst, int pitch, int h, int w, uint8_t colMask);
	void drawSpriteSub5(const uint8_t *src, uint8_t *dst, int pitch, int h, int w, uint8_t colMask);
	void drawSpriteSub6(const uint8_t *src, uint8_t *dst, int pitch, int h, int w, uint8_t colMask);
	void PC_drawChar(uint8_t c, int16_t y, int16_t x, bool forceDefaultFont);
	void PC_drawStringChar(uint8_t *dst, int pitch, const uint8_t *src, uint8_t color, uint8_t chr);

	const char *drawString(const char *str, int16_t x, int16_t y, uint8_t col);
	static Color AMIGA_convertColor(const uint16_t color, bool bgr);
};

#endif // VIDEO_H__
