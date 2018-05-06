
/*
 * REminiscence - Flashback interpreter
 * Copyright (C) 2005-2015 Gregory Montoir (cyx@users.sourceforge.net)
 */

#include "resource.h"
#include "game.h"
#include "unpack.h"
#include "util.h"
#include "video.h"

Video::Video(Resource *res, Game *game)
	: _res(res), _game(game) {
	_frameBuffer          = (uint32_t *) calloc(1, Video::GAMESCREEN_W * Video::GAMESCREEN_H * sizeof(uint32_t));
	_frontLayer           = (uint8_t *) calloc(1, Video::LAYER_SIZE);
	_backLayer            = (uint8_t *) calloc(1, Video::LAYER_SIZE);
	_tempLayer            = (uint8_t *) calloc(1, Video::LAYER_SIZE);
	_tempLayer2           = (uint8_t *) calloc(1, Video::LAYER_SIZE);
	_shakeOffset          = 0;
	_charFrontColor       = 0;
	_charTransparentColor = 0;
	_charShadowColor      = 0;
}

Video::~Video() {
	free(_frameBuffer);
	free(_frontLayer);
	free(_backLayer);
	free(_tempLayer);
	free(_tempLayer2);
}

void Video::updateScreen() {
	debug(DBG_VIDEO, "Video::updateScreen()");
	// TODO(sgc): handle _shakeOffset when copying
	copyRect(0, 0, Video::GAMESCREEN_W, Video::GAMESCREEN_H, _frontLayer, 256);
	_game->setFrameReady();
	_game->yield();
	if (_shakeOffset != 0) {
		_shakeOffset = 0;
	}
}

void Video::fadeOut() {
	debug(DBG_VIDEO, "Video::fadeOut()");
	fadeOutPalette();
}

void Video::fadeOutPalette() {
	for (int step = 16; step >= 0; --step) {
		for (int c = 0; c < 256; ++c) {
			Color col;
			getPaletteEntry(c, &col);
			col.r = col.r * step >> 4;
			col.g = col.g * step >> 4;
			col.b = col.b * step >> 4;
			setPaletteEntry(c, &col);
		}
		updateScreen();
		_game->sleep(50);
	}
}

void Video::setPaletteSlotBE(int palSlot, int palNum) {
	debug(DBG_VIDEO, "Video::setPaletteSlotBE()");
	const uint8_t *p = _res->_pal + palNum * 0x20;
	for (int      i  = 0; i < 16; ++i) {
		const int color = READ_BE_UINT16(p);
		p += 2;
		Color c = AMIGA_convertColor(color, true);
		setPaletteEntry(palSlot * 0x10 + i, &c);
	}
}

void Video::setPaletteSlotLE(int palSlot, const uint8_t *palData) {
	debug(DBG_VIDEO, "Video::setPaletteSlotLE()");
	for (int i = 0; i < 16; ++i) {
		uint16_t color = READ_LE_UINT16(palData);
		palData += 2;
		Color c = AMIGA_convertColor(color);
		setPaletteEntry(palSlot * 0x10 + i, &c);
	}
}

void Video::setTextPalette() {
	debug(DBG_VIDEO, "Video::setTextPalette()");
	setPaletteSlotLE(0xE, _textPal);
}

void Video::setPalette0xF() {
	debug(DBG_VIDEO, "Video::setPalette0xF()");
	const uint8_t *p = _palSlot0xF;
	for (int      i  = 0; i < 16; ++i) {
		Color c;
		c.r = *p++;
		c.g = *p++;
		c.b = *p++;
		setPaletteEntry(0xF0 + i, &c);
	}
}

void Video::PC_decodeLev(int level, int room) {
	uint8_t *tmp = _res->_mbk;
	_res->_mbk = _res->_bnq;
	_res->clearBankData();
	AMIGA_decodeLev(level, room);
	_res->_mbk = tmp;
	_res->clearBankData();
}

static void PC_decodeMapPlane(int sz, const uint8_t *src, uint8_t *dst) {
	const uint8_t *end = src + sz;
	while (src < end) {
		int16_t code = (int8_t) *src++;
		if (code < 0) {
			const int len = 1 - code;
			memset(dst, *src++, len);
			dst += len;
		} else {
			++code;
			memcpy(dst, src, code);
			src += code;
			dst += code;
		}
	}
}

void Video::PC_decodeMap(int level, int room) {
	debug(DBG_VIDEO, "Video::PC_decodeMap(%d)", room);
	assert(room < 0x40);
	int32_t off = READ_LE_UINT32(_res->_map + room * 6);
	if (off == 0) {
		error("Invalid room %d", room);
	}
	// int size = READ_LE_UINT16(_res->_map + room * 6 + 4);
	bool packed = true;
	if (off < 0) {
		off    = -off;
		packed = false;
	}
	const uint8_t *p = _res->_map + off;
	_mapPalSlot1 = *p++;
	_mapPalSlot2 = *p++;
	_mapPalSlot3 = *p++;
	_mapPalSlot4 = *p++;
	if (level == 4 && room == 60) {
		// workaround for wrong palette colors (fire)
		_mapPalSlot4 = 5;
	}
	static const int kPlaneSize = 256 * 224 / 4;
	if (packed) {
		for (int i = 0; i < 4; ++i) {
			const int sz = READ_LE_UINT16(p);
			p += 2;
			PC_decodeMapPlane(sz, p, _res->_scratchBuffer);
			p += sz;
			memcpy(_frontLayer + i * kPlaneSize, _res->_scratchBuffer, kPlaneSize);
		}
	} else {
		for (int i = 0; i < 4; ++i) {
			for (int y = 0; y < 224; ++y) {
				for (int x = 0; x < 64; ++x) {
					_frontLayer[i + x * 4 + 256 * y] = p[kPlaneSize * i + x + 64 * y];
				}
			}
		}
	}
	memcpy(_backLayer, _frontLayer, Video::LAYER_SIZE);
}

void Video::PC_setLevelPalettes() {
	debug(DBG_VIDEO, "Video::PC_setLevelPalettes()");
	if (_unkPalSlot2 == 0) {
		_unkPalSlot2 = _mapPalSlot3;
	}
	if (_unkPalSlot1 == 0) {
		_unkPalSlot1 = _mapPalSlot3;
	}
	setPaletteSlotBE(0x0, _mapPalSlot1);
	setPaletteSlotBE(0x1, _mapPalSlot2);
	setPaletteSlotBE(0x2, _mapPalSlot3);
	setPaletteSlotBE(0x3, _mapPalSlot4);
	if (_unkPalSlot1 == _mapPalSlot3) {
		setPaletteSlotLE(4, _conradPal1);
	} else {
		setPaletteSlotLE(4, _conradPal2);
	}
	// slot 5 is monster palette
	setPaletteSlotBE(0x8, _mapPalSlot1);
	setPaletteSlotBE(0x9, _mapPalSlot2);
	setPaletteSlotBE(0xA, _unkPalSlot2);
	setPaletteSlotBE(0xB, _mapPalSlot4);
	// slots 0xC and 0xD are cutscene palettes
	setTextPalette();
}

void Video::PC_decodeIcn(const uint8_t *src, int num, uint8_t *dst) {
	const int     offset = READ_LE_UINT16(src + num * 2);
	const uint8_t *p     = src + offset + 2;
	for (int      i      = 0; i < 16 * 16 / 2; ++i) {
		*dst++ = p[i] >> 4;
		*dst++ = p[i] & 15;
	}
}

void Video::PC_decodeSpc(const uint8_t *src, int w, int h, uint8_t *dst) {
	const int size = w * h / 2;
	for (int  i    = 0; i < size; ++i) {
		*dst++ = src[i] >> 4u;
		*dst++ = src[i] & 15;
	}
}

static void AMIGA_decodeRle(uint8_t *dst, const uint8_t *src) {
	const int size = READ_BE_UINT16(src) & 0x7FFF;
	src += 2;
	for (int i = 0; i < size;) {
		int code = src[i++];
		if ((code & 0x80) == 0) {
			++code;
			if (i + code > size) {
				code = size - i;
			}
			memcpy(dst, &src[i], code);
			i += code;
		} else {
			code = 1 - ((int8_t) code);
			memset(dst, src[i], code);
			++i;
		}
		dst += code;
	}
}

static void PC_drawTileMask(uint8_t *dst, int x0, int y0, int w, int h, uint8_t *m, uint8_t *p, int size) {
	assert(size == (w * 2 * h));
	for (int y = 0; y < h; ++y) {
		for (int x = 0; x < w; ++x) {
			const int bits = READ_BE_UINT16(m);
			m += 2;
			for (int bit = 0; bit < 8; ++bit) {
				const int j = y0 + y;
				const int i = x0 + 2 * (x * 8 + bit);
				if (i >= 0 && i < Video::GAMESCREEN_W && j >= 0 && j < Video::GAMESCREEN_H) {
					const uint8_t color = *p;
					if (bits & (1 << (15 - (bit * 2)))) {
						dst[j * Video::GAMESCREEN_W + i] = color >> 4;
					}
					if (bits & (1 << (15 - (bit * 2 + 1)))) {
						dst[j * Video::GAMESCREEN_W + i + 1] = color & 15;
					}
				}
				++p;
			}
		}
	}
}

static void decodeSgd(uint8_t *dst, const uint8_t *src, const uint8_t *data) {
	int     num   = -1;
	uint8_t buf[256 * 32];
	int     count = READ_BE_UINT16(src) - 1;
	src += 2;
	do {
		int d2 = READ_BE_UINT16(src);
		src += 2;
		int d0 = READ_BE_UINT16(src);
		src += 2;
		int       d1         = READ_BE_UINT16(src);
		src += 2;
		if (d2 != 0xFFFF) {
			d2 &= ~(1 << 15);
			const int32_t offset = READ_BE_UINT32(data + d2 * 4);
			if (offset < 0) {
				const uint8_t *ptr = data - offset;
				const int     size = READ_BE_UINT16(ptr);
				ptr += 2;
				if (num != d2) {
					num = d2;
					assert(size <= (int) sizeof(buf));
					memcpy(buf, ptr, size);
				}
			} else {
				if (num != d2) {
					num = d2;
					const int size = READ_BE_UINT16(data + offset) & 0x7FFF;
					assert(size <= (int) sizeof(buf));
					AMIGA_decodeRle(buf, data + offset);
				}
			}
		}
		const int w          = (buf[0] + 1) >> 1;
		const int h          = buf[1] + 1;
		const int planarSize = READ_BE_UINT16(buf + 2);
		PC_drawTileMask(dst, (int16_t) d0, (int16_t) d1, w, h, buf + 4, buf + 4 + planarSize, planarSize);
	} while (--count >= 0);
}

static void PC_drawTile(uint8_t *dst, const uint8_t *src, int mask, const bool xflip, const bool yflip, int colorKey) {
	int pitch = Video::GAMESCREEN_W;
	if (yflip) {
		dst += 7 * pitch;
		pitch = -pitch;
	}
	int inc = 1;
	if (xflip) {
		dst += 7;
		inc = -inc;
	}
	for (int y = 0; y < 8; ++y) {
		for (int i = 0; i < 8; i += 2) {
			int color = *src >> 4;
			if (color != colorKey) {
				dst[inc * i] = mask | color;
			}
			color = *src & 15;
			if (color != colorKey) {
				dst[inc * (i + 1)] = mask | color;
			}
			++src;
		}
		dst += pitch;
	}
}

static void
decodeLevHelper(uint8_t *dst, const uint8_t *src, int offset10, int offset12, const uint8_t *a5, bool sgdBuf) {
	if (offset10 != 0) {
		const uint8_t *a0 = src + offset10;
		for (int      y   = 0; y < 224; y += 8) {
			for (int x = 0; x < 256; x += 8) {
				const int d3 = READ_LE_UINT16(a0);
				a0 += 2;
				const int d0 = d3 & 0x7FF;
				if (d0 != 0) {
					const uint8_t *a2   = a5 + d0 * 32;
					const bool    yflip = (d3 & (1 << 12)) != 0;
					const bool    xflip = (d3 & (1 << 11)) != 0;
					int           mask  = 0;
					if ((d3 & 0x8000) != 0) {
						mask = 0x80 + ((d3 >> 6) & 0x10);
					}
					PC_drawTile(dst + y * 256 + x, a2, mask, xflip, yflip, -1);
				}
			}
		}
	}
	if (offset12 != 0) {
		const uint8_t *a0 = src + offset12;
		for (int      y   = 0; y < 224; y += 8) {
			for (int x = 0; x < 256; x += 8) {
				const int d3 = READ_LE_UINT16(a0);
				a0 += 2;
				int d0 = d3 & 0x7FF;
				if (d0 != 0 && sgdBuf) {
					d0 -= 896;
				}
				if (d0 != 0) {
					const uint8_t *a2   = a5 + d0 * 32;
					const bool    yflip = (d3 & (1 << 12)) != 0;
					const bool    xflip = (d3 & (1 << 11)) != 0;
					int           mask  = 0;
					if ((d3 & 0x6000) != 0 && sgdBuf) {
						mask = 0x10;
					} else if ((d3 & 0x8000) != 0) {
						mask = 0x80 + ((d3 >> 6) & 0x10);
					}
					PC_drawTile(dst + y * 256 + x, a2, mask, xflip, yflip, 0);
				}
			}
		}
	}
}

void Video::AMIGA_decodeLev(int level, int room) {
	uint8_t   *tmp   = _res->_scratchBuffer;
	const int offset = READ_BE_UINT32(_res->_lev + room * 4);
	if (!delphine_unpack(tmp, _res->_lev, offset)) {
		error("Bad CRC for level %d room %d", level, room);
	}
	uint16_t         offset10     = READ_BE_UINT16(tmp + 10);
	const uint16_t   offset12     = READ_BE_UINT16(tmp + 12);
	const uint16_t   offset14     = READ_BE_UINT16(tmp + 14);
	static const int kTempMbkSize = 1024;
	uint8_t          *buf         = (uint8_t *) malloc(kTempMbkSize * 32);
	if (!buf) {
		error("Unable to allocate mbk temporary buffer");
	}
	int sz = 0;
	memset(buf, 0, 32);
	sz += 32;
	const uint8_t *a1  = tmp + offset14;
	for (bool     loop = true; loop;) {
		int d0 = READ_BE_UINT16(a1);
		a1 += 2;
		if (d0 & 0x8000) {
			d0 &= ~0x8000;
			loop = false;
		}
		const int     d1  = _res->getBankDataSize(d0);
		const uint8_t *a6 = _res->findBankData(d0);
		if (!a6) {
			a6 = _res->loadBankData(d0);
		}
		const int d3 = *a1++;
		if (d3 == 255) {
			assert(sz + d1 <= kTempMbkSize * 32);
			memcpy(buf + sz, a6, d1);
			sz += d1;
		} else {
			for (int i = 0; i < d3 + 1; ++i) {
				const int d4 = *a1++;
				assert(sz + 32 <= kTempMbkSize * 32);
				memcpy(buf + sz, a6 + d4 * 32, 32);
				sz += 32;
			}
		}
	}
	memset(_frontLayer, 0, Video::LAYER_SIZE);
	if (tmp[1] != 0) {
		assert(_res->_sgd);
		decodeSgd(_frontLayer, tmp + offset10, _res->_sgd);
		offset10 = 0;
	}
	decodeLevHelper(_frontLayer, tmp, offset10, offset12, buf, tmp[1] != 0);
	free(buf);
	memcpy(_backLayer, _frontLayer, Video::LAYER_SIZE);
	_mapPalSlot1 = READ_BE_UINT16(tmp + 2);
	_mapPalSlot2 = READ_BE_UINT16(tmp + 4);
	_mapPalSlot3 = READ_BE_UINT16(tmp + 6);
	_mapPalSlot4 = READ_BE_UINT16(tmp + 8);
}

void Video::drawSpriteSub1(const uint8_t *src, uint8_t *dst, int pitch, int h, int w, uint8_t colMask) {
	debug(DBG_VIDEO, "Video::drawSpriteSub1(0x%X, 0x%X, 0x%X, 0x%X)", pitch, w, h, colMask);
	while (h--) {
		for (int i = 0; i < w; ++i) {
			if (src[i] != 0) {
				dst[i] = src[i] | colMask;
			}
		}
		src += pitch;
		dst += 256;
	}
}

void Video::drawSpriteSub2(const uint8_t *src, uint8_t *dst, int pitch, int h, int w, uint8_t colMask) {
	debug(DBG_VIDEO, "Video::drawSpriteSub2(0x%X, 0x%X, 0x%X, 0x%X)", pitch, w, h, colMask);
	while (h--) {
		for (int i = 0; i < w; ++i) {
			if (src[-i] != 0) {
				dst[i] = src[-i] | colMask;
			}
		}
		src += pitch;
		dst += 256;
	}
}

void Video::drawSpriteSub3(const uint8_t *src, uint8_t *dst, int pitch, int h, int w, uint8_t colMask) {
	debug(DBG_VIDEO, "Video::drawSpriteSub3(0x%X, 0x%X, 0x%X, 0x%X)", pitch, w, h, colMask);
	while (h--) {
		for (int i = 0; i < w; ++i) {
			if (src[i] != 0 && !(dst[i] & 0x80)) {
				dst[i] = src[i] | colMask;
			}
		}
		src += pitch;
		dst += 256;
	}
}

void Video::drawSpriteSub4(const uint8_t *src, uint8_t *dst, int pitch, int h, int w, uint8_t colMask) {
	debug(DBG_VIDEO, "Video::drawSpriteSub4(0x%X, 0x%X, 0x%X, 0x%X)", pitch, w, h, colMask);
	while (h--) {
		for (int i = 0; i < w; ++i) {
			if (src[-i] != 0 && !(dst[i] & 0x80)) {
				dst[i] = src[-i] | colMask;
			}
		}
		src += pitch;
		dst += 256;
	}
}

void Video::drawSpriteSub5(const uint8_t *src, uint8_t *dst, int pitch, int h, int w, uint8_t colMask) {
	debug(DBG_VIDEO, "Video::drawSpriteSub5(0x%X, 0x%X, 0x%X, 0x%X)", pitch, w, h, colMask);
	while (h--) {
		for (int i = 0; i < w; ++i) {
			if (src[i * pitch] != 0 && !(dst[i] & 0x80)) {
				dst[i] = src[i * pitch] | colMask;
			}
		}
		++src;
		dst += 256;
	}
}

void Video::drawSpriteSub6(const uint8_t *src, uint8_t *dst, int pitch, int h, int w, uint8_t colMask) {
	debug(DBG_VIDEO, "Video::drawSpriteSub6(0x%X, 0x%X, 0x%X, 0x%X)", pitch, w, h, colMask);
	while (h--) {
		for (int i = 0; i < w; ++i) {
			if (src[-i * pitch] != 0 && !(dst[i] & 0x80)) {
				dst[i] = src[-i * pitch] | colMask;
			}
		}
		++src;
		dst += 256;
	}
}

void Video::PC_drawChar(uint8_t c, int16_t y, int16_t x, bool forceDefaultFont) {
	debug(DBG_VIDEO, "Video::PC_drawChar(0x%X, %d, %d)", c, y, x);
	const uint8_t *fnt = (_res->_lang == LANG_JP && !forceDefaultFont) ? _font8Jp : _res->_fnt;
	y *= 8;
	x *= 8;
	const uint8_t *src = fnt + (c - 32) * 32;
	uint8_t       *dst = _frontLayer + x + 256 * y;
	for (int      h    = 0; h < 8; ++h) {
		for (int i = 0; i < 4; ++i, ++src) {
			const uint8_t c1 = *src >> 4;
			if (c1 != 0) {
				if (c1 != 2) {
					*dst = _charFrontColor;
				} else {
					*dst = _charShadowColor;
				}
			} else if (_charTransparentColor != 0xFF) {
				*dst = _charTransparentColor;
			}
			++dst;
			const uint8_t c2 = *src & 15;
			if (c2 != 0) {
				if (c2 != 2) {
					*dst = _charFrontColor;
				} else {
					*dst = _charShadowColor;
				}
			} else if (_charTransparentColor != 0xFF) {
				*dst = _charTransparentColor;
			}
			++dst;
		}
		dst += 256 - 8;
	}
}

void Video::PC_drawStringChar(uint8_t *dst, int pitch, const uint8_t *src, uint8_t color, uint8_t chr) {
	assert(chr >= 32);
	src += (chr - 32) * 8 * 4;
	for (int y = 0; y < 8; ++y) {
		for (int x = 0; x < 4; ++x) {
			const uint8_t c1 = src[x] >> 4;
			if (c1 != 0) {
				*dst = (c1 == 15) ? color : (0xE0 + c1);
			}
			++dst;
			const uint8_t c2 = src[x] & 15;
			if (c2 != 0) {
				*dst = (c2 == 15) ? color : (0xE0 + c2);
			}
			++dst;
		}
		src += 4;
		dst += pitch - CHAR_W;
	}
}

const char *Video::drawString(const char *str, int16_t x, int16_t y, uint8_t col) {
	debug(DBG_VIDEO, "Video::drawString('%s', %d, %d, 0x%X)", str, x, y, col);
	const uint8_t *fnt = (_res->_lang == LANG_JP) ? _font8Jp : _res->_fnt;
	int           len  = 0;
	uint8_t       *dst = _frontLayer + y * 256 + x;
	while (true) {
		const uint8_t c = *str++;
		if (c == 0 || c == 0xB || c == 0xA) {
			break;
		}
		this->PC_drawStringChar(dst, 256, fnt, col, c);
		dst += CHAR_W;
		++len;
	}
	return str - 1;
}

Color Video::AMIGA_convertColor(const uint16_t color, bool bgr) { // 4bits to 8bits
	int r = (color & 0xF00) >> 8;
	int g = (color & 0xF0) >> 4;
	int b = color & 0xF;
	if (bgr) {
		SWAP(r, b);
	}
	Color c;
	c.r = (r << 4) | r;
	c.g = (g << 4) | g;
	c.b = (b << 4) | b;
	return c;
}

void Video::setPalette(const uint8_t *pal, int n) {
	assert(n <= 256);
	for (int i = 0; i < n; ++i) {
		uint8_t r = pal[i * 3 + 0];
		uint8_t g = pal[i * 3 + 1];
		uint8_t b = pal[i * 3 + 2];
		_rgbPalette[i] = r << 24u | g << 16u | b << 8u;
	}
}

void Video::setPaletteEntry(int i, const Color *c) {
	_rgbPalette[i] = c->r << 24u | c->g << 16u | c->b << 8u;
}

void Video::getPaletteEntry(int i, Color *c) {
	c->r = static_cast<uint8_t>(_rgbPalette[i] >> 24u);
	c->g = static_cast<uint8_t>(_rgbPalette[i] >> 16u);
	c->b = static_cast<uint8_t>(_rgbPalette[i] >> 8u);
}

void Video::copyRect(int x, int y, int w, int h, const uint8_t *buf, int pitch) {
	if (x < 0) {
		x = 0;
	} else if (x >= Video::GAMESCREEN_W) {
		return;
	}
	if (y < 0) {
		y = 0;
	} else if (y >= Video::GAMESCREEN_H) {
		return;
	}
	if (x + w > Video::GAMESCREEN_W) {
		w = Video::GAMESCREEN_W - x;
	}
	if (y + h > Video::GAMESCREEN_H) {
		h = Video::GAMESCREEN_H - y;
	}

	uint32_t *p = _frameBuffer + y * Video::GAMESCREEN_W + x;
	buf += y * pitch + x;

	for (int j = 0; j < h; ++j) {
		for (int i = 0; i < w; ++i) {
			p[i] = _rgbPalette[buf[i]];
		}
		p += Video::GAMESCREEN_W;
		buf += pitch;
	}

//	if (_pi.dbgMask & PlayerInput::DF_DBLOCKS) {
//		drawRect(x, y, w, h, 0xE7);
//	}
}

