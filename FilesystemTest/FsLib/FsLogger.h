#pragma once
#include "FsTypes.h"
#include "FsFormat.h"
#include "FsCheck.h"

class FsBaseString;

class FsLogger
{
public:
	FsLogger();
	virtual ~FsLogger();

	static void Log(FilesystemLogType LogType, const char* String);

	static bool ShouldLogType(FilesystemLogType LogType);

	template<typename... Args>
	static void LogFormat(FilesystemLogType LogType, const char* Format, Args... Arguments)
	{
		if (!ShouldLogType(LogType))
		{
			return;
		}

		char Buffer[1024];
		FsFormatter::Format(Buffer, 1024, Format, Arguments...);
		Log(LogType, Buffer);
	}

	template<typename... Args, typename TString = FsBaseString>
	static void LogStr(FilesystemLogType LogType, const TString& InString)
	{
		Log(LogType, InString.GetData());
	}

	template<typename... Args, typename TString = FsBaseString>
	static void LogFormatStr(FilesystemLogType LogType, const TString& InFormatString, Args... Arguments)
	{
		LogFormat(LogType, InFormatString.GetData(), Arguments...);
	}

	virtual void OutputLog(const char* String, FilesystemLogType LogType) = 0;

protected:
	static FsLogger* Instance;

	bool bShouldLogVerbose = true;
};