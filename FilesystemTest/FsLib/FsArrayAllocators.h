#pragma once
#include "FsMemory.h"
#include "FsStrUtil.h"

class FsArrayAllocator
{
public:
	virtual ~FsArrayAllocator()
	{
	}

	virtual void Resize(uint64 NewCapacity) = 0;
	virtual void* GetData() = 0;
	virtual const void* GetData() const = 0;
	virtual uint64 GetCapacity() const = 0;

protected:
	friend class FsDirectAllocatorAccessor;

	virtual void SetDataDirect(void* NewData) = 0;
	virtual void SetCapacityDirect(uint64 NewCapacity) = 0;
};

class FsDirectAllocatorAccessor
{
public:
	static void SetAllocatorData(FsArrayAllocator* Allocator, void* Data)
	{
		Allocator->SetDataDirect(Data);
	}

	static void SetAllocatorCapacity(FsArrayAllocator* Allocator, uint64 Capacity)
	{
		Allocator->SetCapacityDirect(Capacity);
	}
};

template<typename TElement>
class FsDefaultArrayAllocator : public FsArrayAllocator
{
public:
	using FsArrayAllocator::FsArrayAllocator;
	
	virtual ~FsDefaultArrayAllocator()
	{
		if (Data)
		{
			FsMemory::Free(Data);
			Data = nullptr;
			Capacity = 0;
		}
	}

	virtual void Resize(uint64 NewCapacity) override
	{
		if (NewCapacity == Capacity)
		{
			return;
		}

		TElement* const NewData = NewCapacity > 0 ? FsMemory::Allocate<TElement>(NewCapacity) : nullptr;
		if (Data)
		{
			if (NewData)
			{
				const uint64 CopyCount = NewCapacity < Capacity ? NewCapacity : Capacity;
				FsMemory::Copy(NewData, Data, CopyCount * sizeof(TElement));
			}
			FsMemory::Free(Data);
		}

		Data = NewData;
		Capacity = NewCapacity;
	}

	// @brief Returns the data ptr
	virtual void* GetData() override
	{
		return Data;
	}

	// @brief Returns the data ptr
	virtual const void* GetData() const override
	{
		return Data;
	}

	virtual uint64 GetCapacity() const override
	{
		return Capacity;
	}

protected:

	virtual void SetDataDirect(void* NewData) override
	{
		Data = static_cast<TElement*>(NewData);
	}

	virtual void SetCapacityDirect(uint64 NewCapacity) override
	{
		Capacity = NewCapacity;
	}

	TElement* Data = nullptr;
	uint64 Capacity = 0;
};

// @brief Fixed Length Inline allocator for small memory allocations
// @details This allocator is used for small memory allocations that are known at compile time.
// The memory is allocated on the stack and is not freed.
// @tparam Size The size of the inline allocator
template<typename TElement, uint64 FixedLength>
class FsFixedLengthArrayAllocator : public FsArrayAllocator
{
public:
	using FsArrayAllocator::FsArrayAllocator;

	virtual void Resize(uint64 NewCapacity) override
	{
		fsCheck(false, "Cannot resize fixed length array");
	}

	virtual void* GetData() override
	{
		return Data;
	}

	virtual const void* GetData() const override
	{
		return Data;
	}

	virtual uint64 GetCapacity() const override
	{
		return FixedLength;
	}

protected:

	virtual void SetDataDirect(void* NewData) override
	{
		fsCheck(false, "Cannot change data of fixed length array");
	}

	virtual void SetCapacityDirect(uint64 NewCapacity) override
	{
		fsCheck(false, "Cannot change capacity of fixed length array");
	}

	TElement Data[FixedLength]{};
};
