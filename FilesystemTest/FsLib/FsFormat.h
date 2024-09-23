#pragma once
#include "FsTypes.h"

class FsFormatter
{
public:
	static void Format(char* buffer, uint64 bufferSize, const char* format, ...);

protected:
	static void FormatString(char* buffer, uint64& bufferIndex, uint64 bufferSize, const char* str);
	static void FormatInteger(char* buffer, uint64& bufferIndex, uint64 bufferSize, int64 num);
	static void FormatUnsignedInteger(char* buffer, uint64& bufferIndex, uint64 bufferSize, uint64 num);
	static void FormatPointer(char* buffer, uint64& bufferIndex, uint64 bufferSize, void* ptr);
	static void FormatCharacter(char* buffer, uint64& bufferIndex, uint64 bufferSize, char c);
};