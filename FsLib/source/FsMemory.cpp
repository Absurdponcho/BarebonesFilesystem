#include "FsMemory.h"
#include "Filesystem.h"
#include <cstring>


void FsMemory::Copy(void* Destination, const void* Source, uint64 Size)
{
	// custom implementation of memcpy
	uint8* Dest = (uint8*)Destination;
	const uint8* Src = (const uint8*)Source;
	for (uint64 i = 0; i < Size; i++)
	{
		Dest[i] = Src[i];
	}
}

void FsMemory::Set(void* Destination, uint8 Value, uint64 Size)
{
	// custom implementation of memset
	uint8* Dest = (uint8*)Destination;
	for (uint64 i = 0; i < Size; i++)
	{
		Dest[i] = Value;
	}
}

void FsMemory::Zero(void* Destination, uint64 Size)
{
	// custom implementation of memset
	Set(Destination, 0, Size);
}

void FsMemory::Move(void* Destination, const void* Source, uint64 Size)
{
	// custom implementation of memmove
	uint8* Dest = static_cast<uint8*>(Destination);
	const uint8* Src = static_cast<const uint8*>(Source);
	if (Dest < Src)
	{
		for (uint64 i = 0; i < Size; i++)
		{
			Dest[i] = Src[i];
		}
	}
	else
	{
		for (uint64 i = Size; i > 0; i--)
		{
			Dest[i - 1] = Src[i - 1];
		}
	}
}

void FsMemory::Swap(void* A, void* B, uint64 Size)
{
	uint8* a = static_cast<uint8*>(A);
	uint8* b = static_cast<uint8*>(B);

	for (uint64 i = 0; i < Size; ++i)
	{
		uint8 temp = a[i];
		a[i] = b[i];
		b[i] = temp;
	}
}

void* FsMemory::Allocate(uint64 Size)
{
	// custom implementation of malloc
	void* const Allocation = FsMemoryAllocator::Instance->Allocate(Size);
	return Allocation;
}

void FsMemory::Free(void* Memory)
{
	// custom implementation of free
	FsMemoryAllocator::Instance->Free(Memory);
}