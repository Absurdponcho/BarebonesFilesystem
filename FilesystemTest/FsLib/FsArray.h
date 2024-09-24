#pragma once
#include "FsTypes.h"
#include "FsMemory.h"
#include "FsCheck.h"
#include "FsArrayAllocators.h"
#include "FsLogger.h"
#include "FsNew.h"
#include "FsTypeTraits.h"

template<typename TElement>
class FsBaseArray;

template<typename TElement, typename TArray>
class FsArrayIterator
{
public:
	FsArrayIterator(uint64 InIndex, TArray& InArray)
		: Index(InIndex), Array(InArray)
	{
	}

	FsArrayIterator& operator++()
	{
		Index++;
		return *this;
	}

	bool operator!=(const FsArrayIterator& Other) const
	{
		return Index != Other.Index || &Array != &Other.Array;
	}

	bool operator==(const FsArrayIterator& Other) const
	{
		return Index == Other.Index && &Array == &Other.Array;
	}

	TElement& operator*()
	{
		return Array[Index];
	}

protected:
	uint64 Index = 0;
	TArray& Array;
};

template<typename TElement, typename TArray>
class FsConstArrayIterator
{
public:
	FsConstArrayIterator(uint64 InIndex, const TArray& InArray)
		: Index(InIndex), Array(InArray)
	{
	}

	FsConstArrayIterator& operator++()
	{
		Index++;
		return *this;
	}

	bool operator!=(const FsConstArrayIterator& Other) const
	{
		return Index != Other.Index || &Array != &Other.Array;
	}

	bool operator==(const FsConstArrayIterator& Other) const
	{
		return Index == Other.Index && &Array == &Other.Array;
	}

	const TElement& operator*() const
	{
		return Array[Index];
	}

protected:
	uint64 Index = 0;
	const TArray& Array;
};

template<typename TElement>
class FsBaseArray
{
public:
	virtual ~FsBaseArray()
	{

	}

	virtual void FillZeroed(uint64 NewCount) = 0;
	virtual TElement* GetData() = 0;
	virtual const TElement* GetData() const = 0;
	virtual uint64 Length() const = 0;
	virtual TElement& operator[](uint64 Index) = 0;
	virtual const TElement& operator[](uint64 Index) const = 0;
};

template<typename TElement, typename TAllocator = FsArrayAllocator>
class FsBaseArrayImpl : public FsBaseArray<TElement>
{
public:
	FsBaseArrayImpl()
	{

	}

	FsBaseArrayImpl(const FsBaseArrayImpl& Other)
	{
		*this = Other;
	}

	// move constructor
	FsBaseArrayImpl(FsBaseArrayImpl&& Other) noexcept
	{
		*this = FsMove(Other);
	}

	virtual ~FsBaseArrayImpl()
	{
	}

	FsArrayIterator<TElement, FsBaseArrayImpl<TElement, TAllocator>> begin()
	{
		return FsArrayIterator<TElement, FsBaseArrayImpl<TElement, TAllocator>>(0, *this);
	}

	FsArrayIterator<TElement, FsBaseArrayImpl<TElement, TAllocator>> end()
	{
		return FsArrayIterator<TElement, FsBaseArrayImpl<TElement, TAllocator>>(Length(), *this);
	}

	FsConstArrayIterator<TElement, FsBaseArrayImpl<TElement, TAllocator>> begin() const
	{
		return FsConstArrayIterator<TElement, FsBaseArrayImpl<TElement, TAllocator>>(0, *this);
	}

	FsConstArrayIterator<TElement, FsBaseArrayImpl<TElement, TAllocator>> end() const
	{
		return FsConstArrayIterator<TElement, FsBaseArrayImpl<TElement, TAllocator>>(Length(), *this);
	}

	// @brief Appends the contents of the other array to this array
	// @param Other The other array
	template <typename TOther>
	void Append(const TOther& Other)
	{
		EnsureCapacityForNewElements(Other.Count);
		for (uint64 i = 0; i < Other.Count; i++)
		{
			Add(Other[i]);
		}
	}

	// @brief Adds the element to the array
	// @param Element The element to add
	void Add(const TElement& Element)
	{
		EnsureCapacityForNewElements(1);
		fsCheck(Count < GetCapacity(), "Count is greater than capacity");

		// Initialize the new memory in place by calling its constructor
		new(GetData() + Count) TElement(Element);

		Count++;
	}

	// @brief Inserts the element at the specified index
	// @param Index The index to insert at
	// @param Element The element to insert
	void InsertAt(uint64 Index, const TElement& Element)
	{
		fsCheck(Index <= Count, "Index out of bounds");
		EnsureCapacityForNewElements(1);
		for (uint64 i = Count; i > Index; i--)
		{
			GetData()[i] = GetData()[i - 1];
		}
		new(GetData() + Index) TElement(Element);
		Count++;
	}

	// @brief Ensures that the array has enough capacity for the new elements
	// @param NewElements The number of new elements
	void EnsureCapacityForNewElements(uint64 NewElements)
	{
		// If we have enough capacity, return
		uint64 NewCapacity = Count + NewElements;
		if (NewCapacity <= GetCapacity())
		{
			return;
		}

		// See if we should use double the existing capacity or the new capacity, whichever is greater
		const uint64 DoubleCapacity = GetCapacity() * 2;
		if (NewCapacity < DoubleCapacity)
		{
			NewCapacity = DoubleCapacity;
		}		

		Reserve(NewCapacity);
	}

	// @brief Removes the element at the specified index
	// @param Index The index to remove
	// @param Amount The number of elements to remove
	void RemoveAt(uint64 Index, uint64 Amount = 1)
	{
		fsCheck(Index < Count, "Index out of bounds");
		fsCheck(Index + Amount <= Count, "Amount out of bounds");
		for (uint64 i = Index; i < Count - Amount; i++)
		{
			GetData()[i] = GetData()[i + Amount];
		}
		Count -= Amount;
	}

	// @brief Removes the specified element from the array
	// @param Element The element to remove
	// @return True if the element was removed
	bool Remove(const TElement& Element)
	{
		for (uint64 i = 0; i < Count; i++)
		{
			if (GetData()[i] == Element)
			{
				RemoveAt(i);
				return true;
			}
		}

		return false;
	}

	// @brief Checks if the array contains the specified element
	// @param Element The element to check
	// @return True if the element is in the array
	bool Contains(const TElement& Element) const
	{
		for (uint64 i = 0; i < Count; i++)
		{
			if (GetData()[i] == Element)
			{
				return true;
			}
		}

		return false;
	}

	// @brief Checks if the array contains the specified element by predicate
	// @param Predicate The predicate to check
	// @return True if the element is in the array
	template<typename TPredicate>
	bool ContainsByPredicate(const TPredicate& Predicate) const
	{
		for (uint64 i = 0; i < Count; i++)
		{
			if (Predicate(GetData()[i]))
			{
				return true;
			}
		}

		return false;
	}

	// @brief Fills the array with zeroed elements
	// @param NewCount The new count
	virtual void FillZeroed(uint64 NewCount) override
	{		
		Reserve(NewCount);
		FsMemory::Zero(GetData(), NewCount * sizeof(TElement));
		Count = NewCount;
	}

	// @brief Fills the array with default elements
	// @param NewCount The new count
	void FillDefault(uint64 NewCount)
	{
		Reserve(NewCount);
		for (uint64 i = 0; i < NewCount; i++)
		{
			new(GetData() + i) TElement();
		}
		Count = NewCount;
	}

	// @brief Adds the amount of zeroed elements to the array
	// @param Amount The amount of elements to add
	void AddZeroed(uint64 Amount)
	{
		EnsureCapacityForNewElements(Amount);
		FsMemory::Zero(GetData() + Count, Amount * sizeof(TElement));
		Count += Amount;
	}

	// @brief Adds the amount of default elements to the array
	// @param Amount The amount of elements to add
	void AddDefault(uint64 Amount)
	{
		EnsureCapacityForNewElements(Amount);
		for (uint64 i = 0; i < Amount; i++)
		{
			new(GetData() + Count + i) TElement();
		}
		Count += Amount;
	}

	// @brief Returns the element at the specified index
	virtual TElement& operator[](uint64 Index) override
	{
		fsCheck(Index < Count, "Index out of bounds");
		return GetData()[Index];
	}

	// @brief Returns the element at the specified index
	virtual const TElement& operator[](uint64 Index) const override
	{
		fsCheck(Index < Count, "Index out of bounds");
		return GetData()[Index];
	}

	// @brief Checks if the index is valid
	// @param Index The index to check
	// @return True if the index is valid
	bool IsValidIndex(uint64 Index) const
	{
		return Index < Count;
	}

	// @brief Returns the number of elements in the array
	virtual uint64 Length() const override
	{
		return Count;
	}

	// @brief Returns the capacity of the array
	uint64 GetCapacity() const
	{
		return Allocator.GetCapacity();
	}

	// @brief Clears the array.
	// @param bShrink If true, the array will free the memory. Otherwise it will stay allocated.
	void Empty(bool bShrink = false)
	{
		// destruct elements
		for (uint64 i = 0; i < Count; i++)
		{
			GetData()[i].~TElement();
		}
		Count = 0;

		if (bShrink)
		{
			Shrink();
		}
	}

	// @brief Checks if the array is empty
	// @return True if the array is empty
	bool IsEmpty() const
	{
		return Count == 0;
	}

	// @brief Shrinks the array to the current count
	void Shrink()
	{
		Allocator.Resize(Count);
	}

	// @brief Resizes the capacity of the array to the specified size. Does not allow shrinking.
	// @param NewCount The new count
	void Reserve(uint64 NewCapacity)
	{
		if (NewCapacity <= GetCapacity())
		{
			return;
		}
		Allocator.Resize(NewCapacity);
	}

	// @brief Resizes the capacity of the array to the specified size.
	// @param NewCount The new count
	void Resize(uint64 NewCapacity)
	{
		if (NewCapacity == Count)
		{
			return;
		}

		if (NewCapacity < Count)
		{
			// Destruct elements if shrinking
			for (uint64 i = NewCapacity; i < Count; i++)
			{
				GetData()[i].~TElement();
			}
		}

		Allocator.Resize(NewCapacity);
	}

	// @brief Checks if the array ends with the specified element
	// @param Element The element to check
	// @return True if the array ends with the element
	bool EndsWith(const TElement& Element) const
	{
		if (Count == 0)
		{
			return false;
		}

		return GetData()[Count - 1] == Element;
	}

	// @brief Checks if the array starts with the specified element
	// @param Element The element to check
	// @return True if the array starts with the element
	bool StartsWith(const TElement& Element) const
	{
		if (Count == 0)
		{
			return false;
		}

		return GetData()[0] == Element;
	}

	// @brief Returns the data ptr
	virtual TElement* GetData() override
	{
		return static_cast<TElement*>(Allocator.GetData());
	}

	// @brief Returns the data ptr
	virtual const TElement* GetData() const override
	{
		return static_cast<const TElement*>(Allocator.GetData());
	}

	// Copy operator
	FsBaseArrayImpl& operator=(const FsBaseArrayImpl& Other)
	{
		Empty();

		Reserve(Other.Count);

		for (uint64 i = 0; i < Other.Count; i++)
		{
			Add(Other[i]);
		}

		return *this;
	}

	// Move operator
	FsBaseArrayImpl& operator=(FsBaseArrayImpl&& Other) noexcept
	{
		// Empty this array to make sure the elements are destructed. Make sure it shrinks to free memory.
		Empty(true);

		fsCheck(Allocator.GetCapacity() == 0, "Capacity should be 0 after empty");

		FsDirectAllocatorAccessor::SetAllocatorData(&Allocator, Other.Allocator.GetData());
		FsDirectAllocatorAccessor::SetAllocatorCapacity(&Allocator, Other.Allocator.GetCapacity());
		FsDirectAllocatorAccessor::SetAllocatorData(&Other.Allocator, nullptr);
		FsDirectAllocatorAccessor::SetAllocatorCapacity(&Other.Allocator, 0);

		Count = Other.Count;
		Other.Count = 0;

		return *this;
	}

protected:
	TAllocator Allocator = TAllocator();
	uint64 Count = 0;
};

// @brief A dynamic array that can grow and shrink
// @tparam TElement The element type
template<typename TElement>
class FsArray : public FsBaseArrayImpl<TElement, FsDefaultArrayAllocator<TElement>>
{
public:
	using FsBaseArrayImpl<TElement, FsDefaultArrayAllocator<TElement>>::FsBaseArrayImpl;
};

// @brief A fixed array that has a fixed length, data allocated on the stack
// @tparam TElement The element type
// @tparam FixedLength The fixed length of the array
template<typename TElement, uint64 FixedLength>
class FsFixedLengthArray : public FsBaseArrayImpl<TElement, FsFixedLengthArrayAllocator<TElement, FixedLength>>
{
public:
	using Super = FsBaseArrayImpl<TElement, FsFixedLengthArrayAllocator<TElement, FixedLength>>;

	// Copy operator
	FsFixedLengthArray& operator=(const FsFixedLengthArray& Other)
	{
		Super::Empty();

		for (uint64 i = 0; i < Other.Count; i++)
		{
			Super::Add(Other[i]);
		}

		return *this;
	}

	// Move operator
	FsFixedLengthArray& operator=(FsFixedLengthArray&& Other)
	{
		// Empty this array to make sure the elements are destructed.
		Super::Empty();

		// Fixed allocator cannot be moved. So we need to move the memory instead, then clear the other array to simulate it being moved.
		// We specifically want to move the memory and not invoke and constructors or destructors.
		FsMemory::Move(Super::GetData(), Other.GetData(), Other.Count * sizeof(TElement));
		FsMemory::Zero(Other.GetData(), Other.Count * sizeof(TElement));

		Super::Count = Other.Count;
		Other.Count = 0;

		return *this;
	}
};

class FsBaseBitArray
{
};

// @brief An array that works on bits as well as bytes
template<typename TInternalArray>
class FsBaseBitArrayImpl : public FsBaseBitArray
{
public:
	FsBaseBitArrayImpl(){}

	template<typename TOther>
	FsBaseBitArrayImpl(const TOther& Other)
	{
		InternalArray = Other.GetInternalArray();
		BitCount = Other.BitLength();
	}

	// @brief Adds a bit to the array
	// @param bValue The bit to add
	void AddBit(bool bValue)
	{
		if (BitCount % 8 == 0)
		{
			const uint64 RequiredBytes = (BitCount / 8) + 1;
			if (RequiredBytes > InternalArray.Length())
			{
				InternalArray.AddZeroed(RequiredBytes - InternalArray.Length());
			}
		}

		if (bValue)
		{
			InternalArray[BitCount / 8] |= 1 << (BitCount % 8);
		}

		BitCount++;
	}

	// @brief Adds a byte to the array
	// @param Byte The byte to add
	void AddByte(uint8 Byte)
	{
		for (uint64 i = 0; i < 8; i++)
		{
			AddBit((Byte & (1 << i)) != 0);
		}
		BitCount += 8;
	}

	// @brief Returns the bit at the specified index
	// @param Index The index to check
	// @return True if the bit is set
	bool GetBit(uint64 Index) const
	{
		fsCheck(Index < BitCount, "Index out of bounds");
		return (InternalArray[Index / 8] & (1 << (Index % 8))) != 0;
	}

	// @brief Sets the bit at the specified index
	// @param Index The index to set
	// @param bValue The value to set
	void SetBit(uint64 Index, bool bValue)
	{
		fsCheck(Index < BitCount, "Index out of bounds");
		if (bValue)
		{
			InternalArray[Index / 8] |= 1 << (Index % 8);
		}
		else
		{
			InternalArray[Index / 8] &= ~(1 << (Index % 8));
		}
	}

	// @brief Returns the number of bits in the array
	uint64 BitLength() const
	{
		return BitCount;
	}

	// @brief Returns the number of bytes in the array
	uint64 ByteLength() const
	{
		return InternalArray.Length();
	}

	// @brief Gets the internal array
	// @return The internal array
	const TInternalArray& GetInternalArray() const
	{
		return InternalArray;
	}

	// @brief Gets the internal array
	// @return The internal array
	TInternalArray& GetInternalArray()
	{
		return InternalArray;
	}

	// @brief Fills the array with zeroed elements
	// @param NewCount The new count
	void FillZeroed(uint64 NewCount)
	{
		InternalArray.FillZeroed(NewCount);
		BitCount = NewCount * 8;
	}

	// @brief Adds the amount of zeroed elements to the array
	// @param Amount The amount of elements to add
	void AddZeroed(uint64 Amount)
	{
		InternalArray.AddZeroed(Amount);
		BitCount += Amount * 8;
	}

	void Empty()
	{
		InternalArray.Empty();
		BitCount = 0;
	}

	// copy operator
	FsBaseBitArrayImpl& operator=(const FsBaseBitArrayImpl& Other)
	{
		InternalArray = Other.GetInternalArray();
		BitCount = Other.BitLength();
		return *this;
	}

	// move operator
	FsBaseBitArrayImpl& operator=(FsBaseBitArrayImpl&& Other)
	{
		Empty();

		fsCheck(InternalArray.Allocator.GetCapacity() == 0, "Capacity should be 0 after empty");

		InternalArray = FsMove(Other.InternalArray);

		BitCount = Other.BitCount;
		Other.BitCount = 0;

		return *this;
	}

protected:
	TInternalArray InternalArray;
	uint64 BitCount = 0;
};

class FsBitArray : public FsBaseBitArrayImpl<FsArray<uint8>>
{
public:
	using Super = FsBaseBitArrayImpl;

	FsBitArray()
	{
	}

	FsBitArray(const FsBitArray& Other)
	{
		*this = Other;
	}

	FsBitArray& operator=(const FsBitArray& Other)
	{
		Super::Empty();

		for (uint64 i = 0; i < Other.BitCount; i++)
		{
			Super::AddBit(Other.GetBit(i));
		}

		return *this;
	}
};

template <uint64 FixedLength>
class FsFixedLengthBitArray : public FsBaseBitArrayImpl<FsFixedLengthArray<uint8, FixedLength>>
{
public:
	using Super = FsBitArray::FsBaseBitArrayImpl;

	// Copy operator
	FsFixedLengthBitArray& operator=(const FsFixedLengthBitArray& Other)
	{
		Super::Empty();

		for (uint64 i = 0; i < Other.Count; i++)
		{
			Super::Add(Other[i]);
		}

		return *this;
	}

	// Move operator
	FsFixedLengthBitArray& operator=(FsFixedLengthBitArray&& Other)
	{
		// Empty this array to make sure the elements are destructed.
		Super::Empty();

		// Fixed allocator cannot be moved. So we need to move the memory instead, then clear the other array to simulate it being moved.
		// We specifically want to move the memory and not invoke and constructors or destructors.
		FsMemory::Move(Super::GetData(), Other.GetData(), Other.Count);
		FsMemory::Zero(Other.GetData(), Other.Count);

		Super::Count = Other.Count;
		Other.Count = 0;

		return *this;
	}
};
