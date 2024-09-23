#include "FsCheck.h"
#include "FsLogger.h"

void CheckImplementer::Check(const char* Message)
{
	// Output the dying message
	FsLogger::Log(FilesystemLogType::Fatal, Message);

	// Die
	*(char*)0x00 = 3;
}
