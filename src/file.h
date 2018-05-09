
/*
 * REminiscence - Flashback interpreter
 * Copyright (C) 2005-2015 Gregory Montoir (cyx@users.sourceforge.net)
 */

#ifndef FILE_H__
#define FILE_H__

#include "intern.h"

struct File_impl {
	bool _ioErr;

	File_impl() : _ioErr(false) {}

	virtual ~File_impl() {}

	virtual bool open(const char *path, const char *mode) = 0;
	virtual void close() = 0;
	virtual uint32_t size() = 0;
	virtual void seek(int32_t off) = 0;
	virtual uint32_t read(void *ptr, uint32_t len) = 0;
	virtual uint32_t write(const void *ptr, uint32_t len) = 0;
};

struct FileSystem;

struct File {
	File();
	~File();

	File_impl *_impl;

	bool open(const char *filename, const char *mode, FileSystem *fs);
	bool open(const char *filename, const char *mode, const char *directory);
	bool open(File_impl *impl);
	void close();
	void cleanup();
	bool ioErr() const;
	uint32_t size();
	void seek(int32_t off);
	uint32_t read(void *ptr, uint32_t len);
	uint8_t readByte();
	uint16_t readUint16LE();
	uint32_t readUint32LE();
	uint16_t readUint16BE();
	uint32_t readUint32BE();
	uint32_t write(const void *ptr, uint32_t size);
	void writeByte(uint8_t b);
	void writeUint16BE(uint16_t n);
	void writeUint32BE(uint32_t n);
};

struct MemFile : File_impl {
	bool     _canResize;
	uint8_t  *_mem;
	uint32_t _pos;
	uint32_t _size;

	MemFile(uint8_t *mem, uint32_t size);

	MemFile();

	bool open(const char *path, const char *mode);

	void close() {}

	uint32_t size() { return _size; }

	void seek(int32_t off);

	uint32_t read(void *ptr, uint32_t len);

	uint32_t write(const void *ptr, uint32_t len);
};

struct ReadOnlyMemFile : File_impl {
	const uint8_t *_mem;
	uint32_t      _pos;
	uint32_t      _size;

	ReadOnlyMemFile(const uint8_t *mem, uint32_t size);

	bool open(const char *path, const char *mode);

	void close() {}

	uint32_t size() { return _size; }

	void seek(int32_t off);

	uint32_t read(void *ptr, uint32_t len);

	uint32_t write(const void *ptr, uint32_t len);
};

#endif // FILE_H__
