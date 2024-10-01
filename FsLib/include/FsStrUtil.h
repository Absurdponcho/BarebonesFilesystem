#pragma once
#include "FsTypes.h"

// strcpy FsFunction
static inline void FsStrCpy(char* Destination, const char* Source)
{
	while (*Source)
	{
		*Destination = *Source;
		Destination++;
		Source++;
	}
	*Destination = '\0';
}

// strlen FsFunction
static inline uint64 FsStrLen(const char* String)
{
	uint64 Length = 0;
	while (*String)
	{
		Length++;
		String++;
	}
	return Length;
}

// strcmp FsFunction
static inline uint64 FsStrCmp(const char* String1, const char* String2)
{
	while (*String1 && *String2 && *String1 == *String2)
	{
		String1++;
		String2++;
	}
	return *String1 - *String2;
}