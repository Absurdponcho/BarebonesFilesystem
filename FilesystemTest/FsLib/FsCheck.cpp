#include "FsCheck.h"
#include "FsLogger.h"
#if !defined(CMAKE)
#ifdef __clang__
#define HAS_BUILTIN_TRAP 1
#elif defined(_MSC_VER)
#define HAS_DEBUG_BREAK 1
#endif
#endif

void CheckImplementer::Check(const char* Message)
{
	// Output the dying message
	FsLogger::Log(FilesystemLogType::Fatal, Message);

	// Die

#if HAS_BUILTIN_TRAP
	__builtin_trap();
#else
	*(char*)0x00 = 3;
#endif
}
