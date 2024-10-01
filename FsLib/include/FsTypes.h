#pragma once

#define MAX_FILE_NAME_LENGTH 256
#define MAX_FILE_HANDLES 4096

#define NO_DISCARD [[nodiscard]]

#define ENUM_OPERATORS(EnumType, UnderlyingType) \
inline EnumType operator|(EnumType A, EnumType B) { return static_cast<EnumType>(static_cast<UnderlyingType>(A) | static_cast<UnderlyingType>(B)); } \
inline EnumType operator&(EnumType A, EnumType B) { return static_cast<EnumType>(static_cast<UnderlyingType>(A) & static_cast<UnderlyingType>(B)); } \
inline EnumType operator^(EnumType A, EnumType B) { return static_cast<EnumType>(static_cast<UnderlyingType>(A) ^ static_cast<UnderlyingType>(B)); } \
inline EnumType operator~(EnumType A) { return static_cast<EnumType>(~static_cast<UnderlyingType>(A)); } \
inline EnumType& operator|=(EnumType& A, EnumType B) { A = A | B; return A; } \
inline EnumType& operator&=(EnumType& A, EnumType B) { A = A & B; return A; } \
inline EnumType& operator^=(EnumType& A, EnumType B) { A = A ^ B; return A; } \
inline bool operator!(EnumType A) { return static_cast<UnderlyingType>(A) == 0; } \
inline bool operator&&(EnumType A, EnumType B) { return static_cast<UnderlyingType>(A) != 0 && static_cast<UnderlyingType>(B) != 0; } \
inline bool operator||(EnumType A, EnumType B) { return static_cast<UnderlyingType>(A) != 0 || static_cast<UnderlyingType>(B) != 0; } \
inline bool operator==(EnumType A, EnumType B) { return static_cast<UnderlyingType>(A) == static_cast<UnderlyingType>(B); } \
inline bool operator!=(EnumType A, EnumType B) { return static_cast<UnderlyingType>(A) != static_cast<UnderlyingType>(B); } \
inline bool operator<(EnumType A, EnumType B) { return static_cast<UnderlyingType>(A) < static_cast<UnderlyingType>(B); } \
inline bool operator>(EnumType A, EnumType B) { return static_cast<UnderlyingType>(A) > static_cast<UnderlyingType>(B); } \
inline bool operator<=(EnumType A, EnumType B) { return static_cast<UnderlyingType>(A) <= static_cast<UnderlyingType>(B); } \
inline bool operator>=(EnumType A, EnumType B) { return static_cast<UnderlyingType>(A) >= static_cast<UnderlyingType>(B); }

typedef unsigned long long uint64;
typedef long long int64;
typedef unsigned char uint8;

enum class FilesystemReadResult : uint8
{
	Success,
	Failed
};

enum class FilesystemWriteResult : uint8
{
	Success,
	Failed
};

enum class FilesystemLogType : uint8
{
	Verbose,
	Info,
	Warning,
	Error,
	Fatal
};

enum class EFileHandleFlags : uint64
{
	None = 0,
	Read = 1 << 0, // 1
	Write = 1 << 1 // 2
};
ENUM_OPERATORS(EFileHandleFlags, uint64)