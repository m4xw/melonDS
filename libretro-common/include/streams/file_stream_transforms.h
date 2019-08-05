/* Copyright  (C) 2010-2017 The RetroArch team
*
* ---------------------------------------------------------------------------------------
* The following license statement only applies to this file (file_stream.h).
* ---------------------------------------------------------------------------------------
*
* Permission is hereby granted, free of charge,
* to any person obtaining a copy of this software and associated documentation files (the "Software"),
* to deal in the Software without restriction, including without limitation the rights to
* use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
* and to permit persons to whom the Software is furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
* INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
* WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
* OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#ifndef __LIBRETRO_SDK_FILE_STREAM_TRANSFORMS_H
#define __LIBRETRO_SDK_FILE_STREAM_TRANSFORMS_H

#include <streams/file_stream.h>
#include <string.h>

RETRO_BEGIN_DECLS

#define FILE RFILE

#define fopen rfopen
#define fclose rfclose
#define ftell rftell
#define fseek rfseek
#define fread rfread
#define fgets rfgets
#define fwrite rfwrite

inline RFILE* rfopen(const char* path, char* mode)
{
	unsigned int retro_mode = RFILE_MODE_READ_TEXT;
	if (strstr(mode, "r"))
	{
		if (strstr(mode, "b"))
		{
			retro_mode = RFILE_MODE_READ;
		}
	}
	if (strstr(mode, "w"))
	{
		retro_mode = RFILE_MODE_WRITE;
	}
	if (strstr(mode, "+"))
	{
		retro_mode = RFILE_MODE_READ_WRITE;
	}

	return filestream_open(path, retro_mode, -1);
}

inline int rfclose(RFILE* stream)
{
	return filestream_close(stream);
}

inline long rftell(RFILE* stream)
{
	return filestream_tell(stream);
}

inline int rfseek(RFILE* stream, long offset, int origin)
{
	return filestream_seek(stream, offset, origin);
}

inline size_t rfread(void* buffer, size_t elementSize, size_t elementCount, RFILE* stream)
{
	return filestream_read(stream, buffer, elementSize*elementCount);
}

inline char* rfgets(char* buffer, int maxCount, FILE* stream)
{
	return filestream_gets(stream, buffer, maxCount);
}

inline size_t rfwrite(void const* buffer, size_t elementSize, size_t elementCount, RFILE* stream)
{
	return filestream_write(stream, buffer, elementSize*elementCount);
}

RETRO_END_DECLS

#endif
