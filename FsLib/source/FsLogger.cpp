#include "FsLogger.h"
#include "Filesystem.h"

FsLogger* FsLogger::Instance = nullptr;

FsLogger::FsLogger()
{
	if (Instance == nullptr)
	{
		Instance = this;
	}
	else
	{
		fsCheck(false, "Only one instance of FsLogger can be created");
	}
}

FsLogger::~FsLogger()
{
	if (Instance == this)
	{
		Instance = nullptr;
	}
}

void FsLogger::Log(FilesystemLogType LogType, const char* String)
{
	// Output the log message
	Instance->OutputLog(String, LogType);
}

bool FsLogger::ShouldLogType(FilesystemLogType LogType)
{
	if (LogType == FilesystemLogType::Verbose && !FsLogger::Instance->bShouldLogVerbose)
	{
		return false;
	}

	return true;
}