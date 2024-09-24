#pragma once
#include "FsTypes.h"
#include "FsArray.h"

typedef FsBaseBitArrayImpl<FsArray<uint8>> FsInternalBitArray;

// A bit stream that can be used to read and write bits to a buffer.
class FsBitStream
{
public:
	FsBitStream(FsInternalBitArray& InBuffer)
		: Buffer(&InBuffer)
	{}

	FsBitStream()
		: Buffer(nullptr)
	{}

	virtual void operator<<(uint64& Value) = 0;
	virtual void operator<<(bool& Value) = 0;
	virtual void operator<<(uint8& Value) = 0;
	virtual void operator<<(char& Value) = 0;

	template<typename TString = FsBaseString>
	void operator<<(TString& Value)
	{
		if (IsReading())
		{
			Value.Empty();
			uint64 Length = 0;
			*this << Length;
			for (uint64 i = 0; i < Length; i++)
			{
				char Char = 0;
				*this << Char;
				Value.Append(Char);
			}
		}
		else
		{
			uint64 Length = Value.Length();
			*this << Length;
			for (uint64 i = 0; i < Length; i++)
			{
				char Char = Value[i];
				*this << Char;
			}
		}
	}

	virtual bool IsReading() const = 0;
	virtual bool IsWriting() const = 0;

protected:
	FsInternalBitArray* Buffer;
};

class FsBitReader : public FsBitStream
{
public:
	using Super = FsBitStream;
	using Super::operator<<;

	FsBitReader(FsInternalBitArray& InBuffer)
		: Super(InBuffer)
	{}

	virtual void operator<<(uint64& Value) override
	{
		Value = 0;
		for (uint64 i = 0; i < 64; i++)
		{
			Value |= static_cast<uint64>(Buffer->GetBit(BitIndex + i)) << i;
		}
		BitIndex += 64;
	}

	virtual void operator<<(bool& Value) override
	{
		Value = Buffer->GetBit(BitIndex);
		BitIndex++;
	}

	virtual void operator<<(uint8& Value) override
	{
		for (uint64 i = 0; i < 8; i++)
		{
			Value |= Buffer->GetBit(BitIndex + i) << i;
		}
		BitIndex += 8;
	}

	virtual void operator<<(char& Value) override
	{
		for (uint64 i = 0; i < 8; i++)
		{
			Value |= Buffer->GetBit(BitIndex + i) << i;
		}
		BitIndex += 8;
	}

	virtual bool IsReading() const override
	{
		return true;
	}

	virtual bool IsWriting() const override
	{
		return false;
	}

protected:
	uint64 BitIndex = 0;
};

class FsBitWriter : public FsBitStream
{
public:
	using Super = FsBitStream;
	using Super::operator<<;

	FsBitWriter(FsInternalBitArray& InBuffer)
		: Super(InBuffer)
	{}

	virtual void operator<<(uint64& Value) override
	{
		for (uint64 i = 0; i < 64; i++)
		{
			Buffer->AddBit(Value & (1ull << i));
		}
	}

	virtual void operator<<(bool& Value) override
	{
		Buffer->AddBit(Value);
	}

	virtual void operator<<(uint8& Value) override
	{
		for (uint64 i = 0; i < 8; i++)
		{
			Buffer->AddBit(Value & (1 << i));
		}
	}

	virtual void operator<<(char& Value) override
	{
		for (uint64 i = 0; i < 8; i++)
		{
			Buffer->AddBit(Value & (1 << i));
		}
	}

	virtual bool IsReading() const override
	{
		return false;
	}

	virtual bool IsWriting() const override
	{
		return true;
	}
};