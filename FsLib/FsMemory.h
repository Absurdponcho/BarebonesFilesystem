#pragma once
#include "FsTypes.h"
#include "FsCheck.h"

class FsMemoryAllocator
{
public:
	FsMemoryAllocator();
	virtual ~FsMemoryAllocator() {}

	virtual void* Allocate(uint64 Size) = 0;
	virtual void Free(void* Memory) = 0;

protected:
	friend class FsMemory;

	static FsMemoryAllocator* Instance;
};

class FsMemory
{
public:
	static void Copy(void* Destination, const void* Source, uint64 Size);
	static void Set(void* Destination, uint8 Value, uint64 Size);
	static void Zero(void* Destination, uint64 Size);
	static void Move(void* Destination, const void* Source, uint64 Size);
	static void Swap(void* A, void* B, uint64 Size);
	static void* Allocate(uint64 Size);

	template<typename T>
	static T* Allocate()
	{
		return static_cast<T*>(Allocate(sizeof(T)));
	}

	template<typename T>
	static T* Allocate(uint64 Count)
	{
		return static_cast<T*>(Allocate(sizeof(T) * Count));
	}

	static void Free(void* Memory);
};