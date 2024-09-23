#include "FsFormat.h"
#include <cstdarg>

void FsFormatter::Format(char* buffer, uint64 bufferSize, const char* format, ...)
{
	va_list args;
	va_start(args, format);

	uint64 bufferIndex = 0;

	for (const char* formatPtr = format; *formatPtr != '\0' && bufferIndex < bufferSize - 1; ++formatPtr)
	{
		if (*formatPtr != '%')
		{
			buffer[bufferIndex++] = *formatPtr;
			continue;
		}
		++formatPtr;
		if (*formatPtr == '\0')
		{
			break;
		}

		switch (*formatPtr)
		{
		case 's':
		{
			const char* str = va_arg(args, const char*);
			FormatString(buffer, bufferIndex, bufferSize, str);
			break;
		}
		case 'i':
		case 'd':
		{
			int64 num = va_arg(args, int64);
			FormatInteger(buffer, bufferIndex, bufferSize, num);
			break;
		}
		case 'u':
		{
			uint64 num = va_arg(args, uint64);
			FormatUnsignedInteger(buffer, bufferIndex, bufferSize, num);
			break;
		}
		case 'p':
		{
			void* ptr = va_arg(args, void*);
			FormatPointer(buffer, bufferIndex, bufferSize, ptr);
			break;
		}
		case 'c':
		{
			char c = va_arg(args, int);
			FormatCharacter(buffer, bufferIndex, bufferSize, c);
			break;
		}
		default:
			buffer[bufferIndex++] = *formatPtr;
			break;
		}
	}

	// null terminate
	buffer[bufferIndex] = '\0';

	va_end(args);
}

void FsFormatter::FormatString(char* buffer, uint64& bufferIndex, uint64 bufferSize, const char* str)
{
	while (*str != '\0' && bufferIndex < bufferSize - 1)
	{
		buffer[bufferIndex++] = *str++;
	}
}

void FsFormatter::FormatInteger(char* buffer, uint64& bufferIndex, uint64 bufferSize, int64 num)
{
	char tempBuffer[128];
	uint64 tempBufferIndex = 0;

	if (num < 0)
	{
		buffer[bufferIndex++] = '-';
		num = -num;
	}

	do
	{
		tempBuffer[tempBufferIndex++] = '0' + num % 10;
		num /= 10;
	} while (num > 0);

	while (tempBufferIndex > 0 && bufferIndex < bufferSize - 1)
	{
		buffer[bufferIndex++] = tempBuffer[--tempBufferIndex];
	}

	buffer[bufferIndex] = '\0';
}

void FsFormatter::FormatUnsignedInteger(char* buffer, uint64& bufferIndex, uint64 bufferSize, uint64 num)
{
	char tempBuffer[128];
	uint64 tempBufferIndex = 0;

	do
	{
		tempBuffer[tempBufferIndex++] = '0' + num % 10;
		num /= 10;
	} while (num > 0);

	while (tempBufferIndex > 0 && bufferIndex < bufferSize - 1)
	{
		buffer[bufferIndex++] = tempBuffer[--tempBufferIndex];
	}

	buffer[bufferIndex] = '\0';
}

void FsFormatter::FormatPointer(char* buffer, uint64& bufferIndex, uint64 bufferSize, void* ptr)
{
	buffer[bufferIndex++] = '0';
	buffer[bufferIndex++] = 'x';

	uint64 num = reinterpret_cast<uint64>(ptr);

	char tempBuffer[128];
	uint64 tempBufferIndex = 0;

	do
	{
		char digit = num % 16;
		if (digit < 10)
		{
			tempBuffer[tempBufferIndex++] = '0' + digit;
		}
		else
		{
			tempBuffer[tempBufferIndex++] = 'a' + digit - 10;
		}
		num /= 16;
	} while (num > 0);

	while (tempBufferIndex > 0 && bufferIndex < bufferSize - 1)
	{
		buffer[bufferIndex++] = tempBuffer[--tempBufferIndex];
	}

	buffer[bufferIndex] = '\0';
}

void FsFormatter::FormatCharacter(char* buffer, uint64& bufferIndex, uint64 bufferSize, char c)
{
	buffer[bufferIndex++] = c;
	buffer[bufferIndex] = '\0';
}