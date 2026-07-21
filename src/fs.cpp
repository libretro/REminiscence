
/*
 * REminiscence - Flashback interpreter
 * Copyright (C) 2005-2015 Gregory Montoir (cyx@users.sourceforge.net)
 */

#include <stdio.h>
#include <streams/file_stream.h>  /* pulls in vfs/vfs_implementation.h (dir API) */
#include <retro_miscellaneous.h>  /* PATH_MAX_LENGTH */
#include "fs.h"

struct FileName
{
   char *name;
   int dir;
};

struct FileSystem_impl
{
   char **_dirsList;
   int _dirsCount;
   FileName *_filesList;
   int _filesCount;

   FileSystem_impl() :
      _dirsList(0), _dirsCount(0), _filesList(0), _filesCount(0) {
      }

   ~FileSystem_impl() {
      for (int i = 0; i < _dirsCount; ++i) {
         free(_dirsList[i]);
      }
      free(_dirsList);
      for (int i = 0; i < _filesCount; ++i) {
         free(_filesList[i].name);
      }
      free(_filesList);
   }

   void setRootDirectory(const char *dir) {
      getPathListFromDirectory(dir);
   }

   int findPathIndex(const char *name) const {
      for (int i = 0; i < _filesCount; ++i) {
         if (strcasecmp(_filesList[i].name, name) == 0) {
            return i;
         }
      }
      return -1;
   }

   char *getPath(const char *name) const {
      const int i = findPathIndex(name);
      if (i >= 0) {
         const char *dir = _dirsList[_filesList[i].dir];
         const int len = strlen(dir) + 1 + strlen(_filesList[i].name) + 1;
         char *p = (char *)malloc(len);
         if (p) {
            snprintf(p, len, "%s/%s", dir, _filesList[i].name);
         }
         return p;
      }
      return 0;
   }

   void addPath(const char *dir, const char *name) {
      int index = -1;
      for (int i = 0; i < _dirsCount; ++i) {
         if (strcmp(_dirsList[i], dir) == 0) {
            index = i;
            break;
         }
      }
      if (index == -1) {
         char **tmp = (char **)realloc(_dirsList, (_dirsCount + 1) * sizeof(char *));
         if (tmp) {
            _dirsList = tmp;
            _dirsList[_dirsCount] = strdup(dir);
            index = _dirsCount;
            ++_dirsCount;
         }
      }
      {
         FileName *tmp = (FileName *)realloc(_filesList, (_filesCount + 1) * sizeof(FileName));
         if (tmp) {
            _filesList = tmp;
            _filesList[_filesCount].name = strdup(name);
            _filesList[_filesCount].dir = index;
            ++_filesCount;
         }
      }
   }

   void getPathListFromDirectory(const char *dir);
};

/* Directory enumeration through the libretro VFS -- one implementation for all
 * platforms (replaces the previous Win32 FindFirstFile / POSIX opendir+stat
 * code paths). */
void FileSystem_impl::getPathListFromDirectory(const char *dir) {
	libretro_vfs_implementation_dir *d = retro_vfs_opendir_impl(dir, true);
	if (d) {
		while (retro_vfs_readdir_impl(d)) {
			const char *name = retro_vfs_dirent_get_name_impl(d);
			if (!name || name[0] == '.') {
				continue;
			}
			char filePath[PATH_MAX_LENGTH];
			snprintf(filePath, sizeof(filePath), "%s/%s", dir, name);
			if (retro_vfs_dirent_is_dir_impl(d)) {
				getPathListFromDirectory(filePath);
			} else {
				addPath(dir, name);
			}
		}
		retro_vfs_closedir_impl(d);
	}
}

FileSystem::FileSystem(const char *dataPath) {
	_impl = new FileSystem_impl;
	_impl->setRootDirectory(dataPath);
}

FileSystem::~FileSystem() {
	delete _impl;
}

char *FileSystem::findPath(const char *filename) const {
	return _impl->getPath(filename);
}

bool FileSystem::exists(const char *filename) const {
	if (_impl->findPathIndex(filename) >= 0)
		return true;
	return false;
}
