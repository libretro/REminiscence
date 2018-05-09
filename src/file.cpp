
/*
 * REminiscence - Flashback interpreter
 * Copyright (C) 2005-2015 Gregory Montoir (cyx@users.sourceforge.net)
 */

#include <sys/param.h>
#include "file.h"
#include "fs.h"
#include "util.h"

#ifdef USE_ZLIB
#include "zlib.h"
#endif


struct StdioFile : File_impl {
	FILE *_fp;

	StdioFile() : _fp(0) {}

	bool open(const char *path, const char *mode) {
		_ioErr = false;
		_fp    = fopen(path, mode);
		return (_fp != 0);
	}

	void close() {
		if (_fp) {
			fclose(_fp);
			_fp = 0;
		}
	}

	uint32_t size() {
		uint32_t sz = 0;
		if (_fp) {
			int pos = ftell(_fp);
			fseek(_fp, 0, SEEK_END);
			sz = ftell(_fp);
			fseek(_fp, pos, SEEK_SET);
		}
		return sz;
	}

	void seek(int32_t off) {
		if (_fp) {
			fseek(_fp, off, SEEK_SET);
		}
	}

	uint32_t read(void *ptr, uint32_t len) {
		if (_fp) {
			uint32_t r = fread(ptr, 1, len, _fp);
			if (r != len) {
				_ioErr = true;
			}
			return r;
		}
		return 0;
	}

	uint32_t write(const void *ptr, uint32_t len) {
		if (_fp) {
			uint32_t r = fwrite(ptr, 1, len, _fp);
			if (r != len) {
				_ioErr = true;
			}
			return r;
		}
		return 0;
	}
};

#ifdef USE_ZLIB
struct GzipFile : File_impl {
	gzFile _fp;
	GzipFile() : _fp(0) {}
	bool open(const char *path, const char *mode) {
		_ioErr = false;
		_fp = gzopen(path, mode);
		return (_fp != 0);
	}
	void close() {
		if (_fp) {
			gzclose(_fp);
			_fp = 0;
		}
	}
	uint32_t size() {
		uint32_t sz = 0;
		if (_fp) {
			int pos = gztell(_fp);
			gzseek(_fp, 0, SEEK_END);
			sz = gztell(_fp);
			gzseek(_fp, pos, SEEK_SET);
		}
		return sz;
	}
	void seek(int32_t off) {
		if (_fp) {
			gzseek(_fp, off, SEEK_SET);
		}
	}
	uint32_t read(void *ptr, uint32_t len) {
		if (_fp) {
			uint32_t r = gzread(_fp, ptr, len);
			if (r != len) {
				_ioErr = true;
			}
			return r;
		}
		return 0;
	}
	uint32_t write(const void *ptr, uint32_t len) {
		if (_fp) {
			uint32_t r = gzwrite(_fp, ptr, len);
			if (r != len) {
				_ioErr = true;
			}
			return r;
		}
		return 0;
	}
};
#endif

File::File()
	: _impl(0) {
}

File::~File() {
	if (_impl) {
		_impl->close();
		delete _impl;
	}
}

bool File::open(const char *filename, const char *mode, FileSystem *fs) {
	cleanup();
	assert(mode[0] != 'z');
	_impl = new StdioFile;
	char *path = fs->findPath(filename);
	if (path) {
		debug(DBG_FILE, "Open file name '%s' mode '%s' path '%s'", filename, mode, path);
		bool ret = _impl->open(path, mode);
		free(path);
		return ret;
	}
	return false;
}

bool File::open(const char *filename, const char *mode, const char *directory) {
	cleanup();
#ifdef USE_ZLIB
	if (mode[0] == 'z') {
		_impl = new GzipFile;
		++mode;
	}
#endif
	if (!_impl) {
		_impl = new StdioFile;
	}
	char path[MAXPATHLEN];
	snprintf(path, sizeof(path), "%s/%s", directory, filename);
	debug(DBG_FILE, "Open file name '%s' mode '%s' path '%s'", filename, mode, path);
	return _impl->open(path, mode);
}

bool File::open(File_impl *impl) {
	cleanup();
	_impl = impl;
	return true;
}

void File::cleanup() {
	if (_impl) {
		_impl->close();
		delete _impl;
		_impl = NULL;
	}
}


void File::close() {
	if (_impl) {
		_impl->close();
	}
}

bool File::ioErr() const {
	return _impl->_ioErr;
}

uint32_t File::size() {
	return _impl->size();
}

void File::seek(int32_t off) {
	_impl->seek(off);
}

uint32_t File::read(void *ptr, uint32_t len) {
	return _impl->read(ptr, len);
}

uint8_t File::readByte() {
	uint8_t b;
	read(&b, 1);
	return b;
}

uint16_t File::readUint16LE() {
	uint8_t lo = readByte();
	uint8_t hi = readByte();
	return (hi << 8) | lo;
}

uint32_t File::readUint32LE() {
	uint16_t lo = readUint16LE();
	uint16_t hi = readUint16LE();
	return (hi << 16) | lo;
}

uint16_t File::readUint16BE() {
	uint8_t hi = readByte();
	uint8_t lo = readByte();
	return (hi << 8) | lo;
}

uint32_t File::readUint32BE() {
	uint16_t hi = readUint16BE();
	uint16_t lo = readUint16BE();
	return (hi << 16) | lo;
}

uint32_t File::write(const void *ptr, uint32_t len) {
	return _impl->write(ptr, len);
}

void File::writeByte(uint8_t b) {
	write(&b, 1);
}

void File::writeUint16BE(uint16_t n) {
	writeByte(n >> 8);
	writeByte(n & 0xFF);
}

void File::writeUint32BE(uint32_t n) {
	writeUint16BE(n >> 16);
	writeUint16BE(n & 0xFFFF);
}

MemFile::MemFile(uint8_t *mem, uint32_t size)
	: _canResize(false), _mem(mem), _pos(0), _size(size) {

}

MemFile::MemFile() : _canResize(true), _mem(NULL), _pos(0), _size(0) {}

bool MemFile::open(const char *path, const char *mode) {
	return true;
}

void MemFile::seek(int32_t off) {
	assert(off >= 0);
	_pos = static_cast<uint32_t>(off);
	if (_pos > _size) {
		_pos = _size;
	}
}

uint32_t MemFile::read(void *ptr, uint32_t len) {
	if (_ioErr) {
		return 0;
	}
	uint32_t to_read = len;
	uint32_t new_pos = _pos + len;
	if (new_pos > _size) {
		to_read = _size - _pos;
		_ioErr  = true;
	}

	memcpy(ptr, reinterpret_cast<const void *>(_mem[_pos]), to_read);
	_pos += to_read;

	return to_read;
}

uint32_t MemFile::write(const void *ptr, uint32_t len) {
	if (_ioErr) {
		return 0;
	}
	uint32_t new_pos  = _pos + len;
	uint32_t to_write = len;
	if (new_pos > _size) {
		if (!_canResize) {
			to_write = _size - _pos;
			_ioErr   = true;
		} else {
			_mem  = static_cast<uint8_t *>(realloc(_mem, new_pos));
			_size = new_pos;
		}
	}

	memcpy(reinterpret_cast<void *>(_mem + _pos), ptr, to_write);
	_pos += to_write;

	return to_write;
}

ReadOnlyMemFile::ReadOnlyMemFile(const uint8_t *mem, uint32_t size)
	: _mem(mem), _pos(0), _size(size) {

}

bool ReadOnlyMemFile::open(const char *path, const char *mode) {
	return true;
}

void ReadOnlyMemFile::seek(int32_t off) {
	assert(off >= 0);
	_pos = static_cast<uint32_t>(off);
	if (_pos > _size) {
		_pos = _size;
	}
}

uint32_t ReadOnlyMemFile::read(void *ptr, uint32_t len) {
	if (_ioErr) {
		return 0;
	}
	uint32_t to_read = len;
	uint32_t new_pos = _pos + len;
	if (new_pos > _size) {
		to_read = _size - _pos;
		_ioErr  = true;
	}

	memcpy(ptr, reinterpret_cast<const void *>(_mem + _pos), to_read);
	_pos += to_read;

	return to_read;
}

uint32_t ReadOnlyMemFile::write(const void *ptr, uint32_t len) {
	if (_ioErr) {
		return 0;
	}
	_ioErr = true;
	return 0;
}
