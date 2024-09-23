#pragma once
#include "FsTypes.h"
#include "FsArray.h"
#include "FsStrUtil.h"

class FsBaseString
{
public:
	virtual ~FsBaseString()
	{

	}

	FsBaseString()
	{

	}

	virtual const char* GetData() const = 0;
	virtual char* GetData() = 0;
	virtual uint64 Length() const = 0;
	virtual void Append(char InChar) = 0;
	virtual void Append(const char* InString) = 0;
	virtual void Append(const char* InString, uint64 Length) = 0;
	virtual void Empty() = 0;
};

static char FsToLower(char Character)
{
	if (Character >= 'A' && Character <= 'Z')
	{
		return Character + 32;
	}
	return Character;
}

static char FsToUpper(char Character)
{
	if (Character >= 'a' && Character <= 'z')
	{
		return Character - 32;
	}
	return Character;
}

template<typename TStringDataArray>
class FsBaseStringImpl : public FsBaseString
{
public:
	FsBaseStringImpl()
	{
		// Need a null terminator.
		Data.Add('\0');
	}

	FsBaseStringImpl(const char* InString)
	{
		Append(InString);
	}

	template<typename TOther = FsBaseStringImpl>
	FsBaseStringImpl(const TOther& InString)
	{
		Append(InString.GetData());
	}

	template<typename TOther = FsBaseStringImpl>
	FsBaseStringImpl(const TOther* InString)
	{
		Append(InString->GetData());
	}

	virtual ~FsBaseStringImpl() {}

	// @brief Appends a character to the current string
	// @param InString The string to append
	virtual void Append(const char* InString) override
	{
		// Remove the null terminator
		if (!Data.IsEmpty())
		{
			Data.RemoveAt(Data.Length() - 1);
		}

		for (uint64 i = 0; InString[i] != '\0'; i++)
		{
			Data.Add(InString[i]);
		}

		Data.Add('\0');
	}

	// @brief Appends a string to the current string
	// @param InString The string to append
	template<typename TOther = FsBaseStringImpl>
	void Append(const TOther& InString)
	{
		Append(InString.GetData());
	}

	// @brief Appends a string to the current string
	// @param InString The string to append
	// @param Length The length of the string to append
	virtual void Append(const char* InString, uint64 Length) override
	{
		// Remove the null terminator
		if (!Data.IsEmpty())
		{
			Data.RemoveAt(Data.Length() - 1);
		}

		for (uint64 i = 0; i < Length; i++)
		{
			Data.Add(InString[i]);
		}

		if (!Data.EndsWith('\0'))
		{
			Data.Add('\0');
		}
	}

	// @brief Appends a character to the current string
	// @param Character The character to append
	virtual void Append(char Character) override
	{
		// Remove the null terminator
		if (!Data.IsEmpty())
		{
			Data.RemoveAt(Data.Length() - 1);
		}

		Data.Add(Character);
		Data.Add('\0');
	}

	bool Contains(const char* InString, bool bCaseSensitive = false, uint64* FoundIndex = nullptr, uint64 StartIndex = 0) const
	{
		uint64 InStringLength = FsStrLen(InString);
		uint64 StringLength = Data.Length();

		if (InStringLength == 0 || InStringLength > StringLength)
		{
			return false;
		}

		for (uint64 i = StartIndex; i < StringLength; i++)
		{
			if (bCaseSensitive)
			{
				if (Data[i] != InString[0])
				{
					continue;
				}
			}
			else
			{
				if (FsToLower(Data[i]) != FsToLower(InString[0]))
				{
					continue;
				}
			}

			bool bFound = true;
			for (uint64 j = 1; j < InStringLength; j++)
			{
				if (bCaseSensitive)
				{
					if (Data[i + j] != InString[j])
					{
						bFound = false;
						break;
					}
				}
				else
				{
					if (FsToLower(Data[i + j]) != FsToLower(InString[j]))
					{
						bFound = false;
						break;
					}
				}
			}
			if (bFound)
			{
				if (FoundIndex)
				{
					*FoundIndex = i;
				}
				return true;
			}
		}
		return false;
	}

	// @brief Checks if the string contains another string
	// @param InString The string to check
	// @param FoundIndex The index where the string was found
	// @param StartIndex The index to start searching from
	// @return True if the string contains InString
	template <typename TOther = FsBaseString>
	bool Contains(const TOther& InString, bool bCaseSensitive = false, uint64* FoundIndex = nullptr, uint64 StartIndex = 0) const
	{
		return Contains(InString.GetData(), bCaseSensitive, FoundIndex, StartIndex);
	}

	// @brief Finds all indexes of a string inside this string
	// @param InString The string to find
	// @param bCaseSensitive True if the comparison should be case sensitive
	// @param OutIndexes The indexes where the string was found
	// @return The number of times the string was found
	template <typename TOther = FsBaseStringImpl, typename TArray = FsBaseArray<uint64>>
	uint64 FindAll(const TOther& InString, TArray& OutIndexes, bool bCaseSensitive = false) const
	{
		uint64 FoundIndex = 0;
		uint64 StartIndex = 0;
		uint64 InStringLength = InString.Length();
		uint64 FoundCount = 0;

		while (Contains(InString, bCaseSensitive, &FoundIndex, StartIndex))
		{
			if (FoundIndex >= StartIndex)
			{
				OutIndexes.Add(FoundIndex);
				StartIndex = FoundIndex + InStringLength;
				FoundIndex = StartIndex; // Update FoundIndex to continue the search from the new position
				FoundCount++;
			}
			else
			{
				break; // Prevent infinite loop if FoundIndex is not progressing
			}
		}

		return FoundCount;
	}

	// @brief Finds all indexes of a string inside this string
	// @param InString The string to find
	// @param bCaseSensitive True if the comparison should be case sensitive
	// @param OutIndexes The indexes where the string was found
	// @return The number of times the string was found
	template <typename TArray = FsBaseArray<uint64>>
	uint64 FindAll(const char* InString, TArray& OutIndexes, bool bCaseSensitive = false) const
	{
		uint64 FoundIndex = 0;
		uint64 StartIndex = 0;
		uint64 InStringLength = FsStrLen(InString);
		uint64 FoundCount = 0;

		while (Contains(InString, bCaseSensitive, &FoundIndex, StartIndex))
		{
			if (FoundIndex >= StartIndex)
			{
				OutIndexes.Add(FoundIndex);
				StartIndex = FoundIndex + InStringLength;
				FoundIndex = StartIndex; // Update FoundIndex to continue the search from the new position
				FoundCount++;
			}
			else
			{
				break; // Prevent infinite loop if FoundIndex is not progressing
			}
		}

		return FoundCount;
	}

	// @brief Finds the last index of a string inside this string
	// @param InString The string to find
	// @param bCaseSensitive True if the comparison should be case sensitive
	// @param OutIndex The index where the string was found
	// @return True if the string was found
	template <typename TOther = FsBaseStringImpl>
	bool FindLast(const TOther& InString, uint64& OutIndex, bool bCaseSensitive = false) const
	{
		FsArray<uint64> Indexes;
		uint64 FoundCount = FindAll(InString, Indexes, bCaseSensitive);
		if (FoundCount > 0)
		{
			OutIndex = Indexes[FoundCount - 1];
			return true;
		}
		return false;
	}

	// @brief Finds the last index of a string inside this string
	// @param InString The string to find
	// @param bCaseSensitive True if the comparison should be case sensitive
	// @param OutIndex The index where the string was found
	// @return True if the string was found
	bool FindLast(const char* InString, uint64& OutIndex, bool bCaseSensitive = false) const
	{
		FsArray<uint64> Indexes;
		uint64 FoundCount = FindAll(InString, Indexes, bCaseSensitive);
		if (FoundCount > 0)
		{
			OutIndex = Indexes[FoundCount - 1];
			return true;
		}
		return false;
	}

	// @brief Gets the length of the string (excluding the null terminator)
	virtual uint64 Length() const override
	{
		if (Data.IsEmpty())
		{
			return 0;
		}

		return Data.Length() - 1;
	}

	virtual const char* GetData() const override
	{
		return Data.GetData();
	}

	virtual char* GetData() override
	{
		return Data.GetData();
	}

	const char& operator[](uint64 Index) const
	{
		return Data[Index];
	}

	char& operator[](uint64 Index)
	{
		return Data[Index];
	}

	virtual void Empty() override
	{
		Data.Empty();
	}

	// @brief Checks if the string is empty, does not include the null terminator
	bool IsEmpty() const
	{
		return Length() == 0;
	}

	// @brief Checks if the string equals another string.
	// @param InString The string to compare
	// @param bCaseSensitive True if the comparison should be case sensitive
	// @return True if the strings are equal
	template <typename TOther = FsBaseStringImpl>
	NO_DISCARD bool Equals(const TOther& InString, bool bCaseSensitive = false) const
	{
		if (Length() != InString.Length())
		{
			return false;
		}

		for (uint64 Index = 0; Index < Length(); Index++)
		{
			if (bCaseSensitive)
			{
				if (Data[Index] != InString[Index])
				{
					return false;
				}
			}
			else
			{
				if (FsToLower(Data[Index]) != FsToLower(InString[Index]))
				{
					return false;
				}
			}
		}

		return true;
	}

	// @brief Transforms all characters to lowercase
	// @return The string with all characters in lowercase
	template<typename TResult = FsBaseStringImpl>
	NO_DISCARD TResult ToLower() const
	{
		TResult Result;
		for (uint64 i = 0; i < Length(); i++)
		{
			Result.Append(FsToLower(Data[i]));
		}
		return Result;
	}

	// @brief Transforms all characters to uppercase
	// @return The string with all characters in uppercase
	template<typename TResult = FsBaseStringImpl>
	NO_DISCARD TResult ToUpper() const
	{
		TResult Result;
		for (uint64 i = 0; i < Length(); i++)
		{
			Result.Append(FsToUpper(Data[i]));
		}
		return Result;
	}

	// @brief Finds and replaces all instances of a string with another string
	// @param InString The string to find
	// @param Replacement The string to replace with
	// @param bCaseSensitive True if the comparison should be case sensitive
	// @return The string with all instances of InString replaced with Replacement
	template<typename TResult = FsBaseStringImpl, typename TInString = FsBaseStringImpl, typename TReplaceString = FsBaseStringImpl>
	NO_DISCARD TResult Replace(const TInString& InString, const TReplaceString& Replacement, bool bCaseSensitive = false) const
	{
		return Replace(InString.GetData(), Replacement.GetData(), bCaseSensitive);
	}

	// @brief Finds and replaces all instances of a string with another string
	// @param InString The string to find
	// @param Replacement The string to replace with
	// @param bCaseSensitive True if the comparison should be case sensitive
	// @return The string with all instances of InString replaced with Replacement
	template<typename TResult = FsBaseStringImpl>
	NO_DISCARD TResult Replace(const char* InString, const char* Replacement, bool bCaseSensitive = false) const
	{
		TResult Result;
		uint64 FoundIndex = 0;
		uint64 StartIndex = 0;
		uint64 InStringLength = FsStrLen(InString);

		while (Contains(InString, bCaseSensitive, &FoundIndex, StartIndex))
		{
			if (FoundIndex >= StartIndex)
			{
				Result.Append(Data.GetData() + StartIndex, FoundIndex - StartIndex);
				Result.Append(Replacement);
				StartIndex = FoundIndex + InStringLength;
				FoundIndex = StartIndex; // Update FoundIndex to continue the search from the new position
			}
			else
			{
				break; // Prevent infinite loop if FoundIndex is not progressing
			}
		}

		Result.Append(Data.GetData() + StartIndex);
		return Result;
	}

	// @brief Checks if the string ends with a character
	// @param Character The character to check
	// @return True if the string ends with the character
	bool EndsWith(char Character) const
	{
		if (IsEmpty())
		{
			return false;
		}
		return Data[Length() - 1] == Character;
	}

	// @brief Checks if the string starts with a character
	// @param Character The character to check
	// @return True if the string starts with the character
	bool StartsWith(char Character) const
	{
		if (IsEmpty())
		{
			return false;
		}
		return Data[0] == Character;
	}

	// @brief Checks if the string ends with a string
	// @param InString The string to check
	// @return True if the string ends with InString
	template<typename TOther = FsBaseStringImpl>
	bool EndsWith(const TOther& InString) const
	{
		if (Length() < InString.Length())
		{
			return false;
		}

		for (uint64 i = 0; i < InString.Length(); i++)
		{
			if (Data[Length() - InString.Length() + i] != InString[i])
			{
				return false;
			}
		}

		return true;
	}

	// @brief Checks if the string ends with a string
	// @param InString The string to check
	// @return True if the string ends with InString
	bool EndsWith(const char* InString) const
	{
		uint64 InStringLength = FsStrLen(InString);
		if (Data.Length() < InStringLength)
		{
			return false;
		}

		for (uint64 i = 0; i < InStringLength; i++)
		{
			if (Data[Length() - InStringLength + i] != InString[i])
			{
				return false;
			}
		}

		return true;
	}

	// @brief Checks if the string starts with a string
	// @param InString The string to check
	// @return True if the string starts with InString
	template<typename TOther = FsBaseStringImpl>
	bool StartsWith(const TOther& InString) const
	{
		if (Data.Length() < InString.Length())
		{
			return false;
		}

		for (uint64 i = 0; i < InString.Length(); i++)
		{
			if (Data[i] != InString[i])
			{
				return false;
			}
		}

		return true;
	}

	// @brief Checks if the string starts with a string
	// @param InString The string to check
	// @return True if the string starts with InString
	bool StartsWith(const char* InString) const
	{
		uint64 InStringLength = FsStrLen(InString);
		if (Length() < InStringLength)
		{
			return false;
		}

		for (uint64 i = 0; i < InStringLength; i++)
		{
			if (Data[i] != InString[i])
			{
				return false;
			}
		}

		return true;
	}

	// @brief Substring of the string
	// @param StartIndex The start index of the substring
	// @param Length The length of the substring
	// @return The substring
	template<typename TResult = FsBaseStringImpl>
	NO_DISCARD TResult Substring(uint64 StartIndex, uint64 Length) const
	{
		TResult Result;
		for (uint64 i = StartIndex; i < StartIndex + Length; i++)
		{
			Result.Append(Data[i]);
		}
		return Result;
	}

	// @brief Removes characters from the string at the specified index and length
	// @param StartIndex The start index of the characters to remove
	// @param Length The number of characters to remove
	void RemoveAt(uint64 StartIndex, uint64 Length = 1)
	{
		Data.RemoveAt(StartIndex, Length);
	}

	// @brief Copy operator
	// @param InString The string to copy
	// @return The copied string
	template<typename TOther = FsBaseStringImpl>
	FsBaseStringImpl& operator=(const TOther& InString)
	{
		Empty();
		Append(InString);
		return *this;
	}

	// @brief Copy operator
	// @param InString The string to copy
	// @return The copied string
	FsBaseStringImpl& operator=(const char* InString)
	{
		Empty();
		Append(InString);
		return *this;
	}

	// @brief Move operator
	// @param InString The string to move
	// @return The moved string
	template<typename TOther = FsBaseStringImpl>
	FsBaseStringImpl& operator=(TOther&& InString)
	{
		Data = FsMove(InString.Data);
		return *this;
	}

	// @brief == operator
	// @param InString The string to compare
	// @return True if the strings are equal
	template<typename TOther = FsBaseStringImpl>
	NO_DISCARD bool operator==(const TOther& InString) const
	{
		return Equals(InString);
	}

	// @brief != operator
	// @param InString The string to compare
	// @return True if the strings are not equal
	template<typename TOther = FsBaseStringImpl>
	NO_DISCARD bool operator!=(const TOther& InString) const
	{
		return !Equals(InString);
	}

	// @brief Add the amount of zero characters to the string
	// @param Amount The amount of zero characters to add
	void AddZeroed(uint64 Amount)
	{
		for (uint64 i = 0; i < Amount; i++)
		{
			Data.Add('\0');
		}
	}

protected:
	TStringDataArray Data{};
};

class FsString : public FsBaseStringImpl<FsArray<char>>
{
public:
	using FsBaseStringImpl<FsArray<char>>::FsBaseStringImpl;
	using FsBaseStringImpl<FsArray<char>>::operator=;

	FsString(const char* InString)
		: FsBaseStringImpl<FsArray<char>>(InString)
	{}

	template<typename TOther = FsBaseStringImpl>
	FsString(const TOther& InString)
		: FsBaseStringImpl<FsArray<char>>(InString)
	{}

	template<typename TOther = FsBaseStringImpl>
	FsString(const TOther* InString)
		: FsBaseStringImpl<FsArray<char>>(InString)
	{}

	FsString(const FsString& InString)
		: FsBaseStringImpl<FsArray<char>>(InString)
	{}
};

template<uint64 FixedLength>
class FsFixedLengthString : public FsBaseStringImpl<FsFixedLengthArray<char, FixedLength>>
{
public:
	using FsBaseStringImpl<FsFixedLengthArray<char, FixedLength>>::FsBaseStringImpl;
	using FsBaseStringImpl<FsFixedLengthArray<char, FixedLength>>::operator=;

	FsFixedLengthString(const char* InString)
		: FsBaseStringImpl<FsFixedLengthArray<char, FixedLength>>(InString)
	{}

	template<typename TOther>
	FsFixedLengthString(const TOther& InString)
		: FsBaseStringImpl<FsFixedLengthArray<char, FixedLength>>(InString)
	{}

	template<typename TOther>
	FsFixedLengthString(const TOther* InString)
		: FsBaseStringImpl<FsFixedLengthArray<char, FixedLength>>(InString)
	{}

	FsFixedLengthString(const FsFixedLengthString& InString)
		: FsBaseStringImpl<FsFixedLengthArray<char, FixedLength>>(InString)
	{}

};
