
#include "resource_aba.h"
#include "unpack.h"

const char *ResourceAba::FILENAME = "DEMO_UK.ABA";

ResourceAba::ResourceAba(FileSystem *fs)
	: _fs(fs)
{
   _entries = 0;
   _entriesCount = 0;
}

ResourceAba::~ResourceAba()
{
	free(_entries);
}

void ResourceAba::readEntries()
{
   if (_f.open(FILENAME, "rb", _fs))
   {
      int i;
      uint32_t nextOffset = 0;
      _entriesCount = _f.readUint16BE();
      _entries = (ResourceAbaEntry *)calloc(_entriesCount, sizeof(ResourceAbaEntry));
      for (i = 0; i < _entriesCount; ++i)
      {
         _f.read(_entries[i].name, sizeof(_entries[i].name));
         _entries[i].offset = _f.readUint32BE();
         _entries[i].compressedSize = _f.readUint32BE();
         _entries[i].size = _f.readUint32BE();
         if (i != 0) {
            assert(nextOffset == _entries[i].offset);
         }
         nextOffset = _entries[i].offset + _entries[i].compressedSize;
      }
   }
}

const ResourceAbaEntry *ResourceAba::findEntry(const char *name) const
{
   for (int i = 0; i < _entriesCount; ++i)
   {
      if (strcasecmp(_entries[i].name, name) == 0)
         return &_entries[i];
   }
   return 0;
}

uint8_t *ResourceAba::loadEntry(const char *name, uint32_t *size)
{
   uint8_t *dst = 0;
   const ResourceAbaEntry *e = findEntry(name);
   if (e)
   {
      if (size)
         *size = e->size;
      uint8_t *tmp = (uint8_t *)malloc(e->compressedSize);
      _f.seek(e->offset);
      _f.read(tmp, e->compressedSize);
      if (e->compressedSize == e->size)
         dst = tmp;
      else
      {
         dst = (uint8_t *)malloc(e->size);
         const bool ret = delphine_unpack(dst, tmp, e->compressedSize);
         if (!ret) {
            log_cb(RETRO_LOG_ERROR, "Bad CRC for '%s'\n", name);
         }
         free(tmp);
      }
   }
   return dst;
}
