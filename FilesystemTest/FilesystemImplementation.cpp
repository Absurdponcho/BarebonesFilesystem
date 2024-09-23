#include "FilesystemImplementation.h"
#include <fstream>
#include <iostream>
#include <Windows.h>

FsFilesystemImpl::FsFilesystemImpl(uint64 InPartitionSize, uint64 InBlockSize)
	: FsFilesystem(InPartitionSize, InBlockSize)
{
	// Use STL to create a file of 1GB size that we will use for the FsFilesystem implementation

	// If it already exists, don't create it again
	std::ifstream ExistingFile(VirtualFileName);
	if (ExistingFile.is_open())
	{
		ExistingFile.close();
		return;
	}

	std::ofstream File(VirtualFileName);
	if (!File.is_open())
	{
		FsLogger::LogFormat(FilesystemLogType::Error, "Failed to open file: %s", VirtualFileName);
		return;
	}

	File.seekp(InPartitionSize);
	if (File.fail())
	{
		FsLogger::LogFormat(FilesystemLogType::Error, "Failed to seek to 1GB");
		return;
	}
	
	File << '\0';
	if (File.fail())
	{
		FsLogger::LogFormat(FilesystemLogType::Error, "Failed to write 1GB");
		return;
	}

	File.close();
	if (File.fail())
	{
		FsLogger::LogFormat(FilesystemLogType::Error, "Failed to close file: %s", VirtualFileName);
		return;
	}

	//FsLogger::LogFormat(FilesystemLogType::Info, "Created 1GB file at full path: %s", );

	// Get full file path
    char FullPath[MAX_PATH];
    DWORD result = GetFullPathNameA(VirtualFileName, MAX_PATH-1, FullPath, nullptr);
    if (result == 0 || result > MAX_PATH) {
        FsLogger::LogFormat(FilesystemLogType::Error, "Failed to get full path for file: %s", VirtualFileName);
        return;
    }
    FullPath[result] = '\0'; // Ensure null-termination

    FsLogger::LogFormat(FilesystemLogType::Info, "Created 1GB file at full path: %s", FullPath);
}

FsFilesystemImpl::~FsFilesystemImpl()
{
}

FilesystemReadResult FsFilesystemImpl::Read(uint64 Offset, uint64 Length, uint8* Destination)
{
	// Use STL to read the file that we created in the Initialize FsFunction

	//FsLogger::LogFormat(FilesystemLogType::Info, "Reading %u bytes from %u", Length, Offset);

	std::ifstream File(VirtualFileName);
	
	if (!File.is_open())
	{
		FsLogger::LogFormat(FilesystemLogType::Error, "Failed to open file: %s, at offset %u and length %u", VirtualFileName, Offset, Length);
		return FilesystemReadResult::Failed;
	}

	File.seekg(Offset);
	if (File.fail())
	{
		FsLogger::LogFormat(FilesystemLogType::Error, "Failed to seek to offset %u", Offset);
		return FilesystemReadResult::Failed;
	}

	File.read(reinterpret_cast<char*>(Destination), Length);
	if (File.fail())
	{
		FsLogger::LogFormat(FilesystemLogType::Error, "Failed to read %u bytes", Length);
		return FilesystemReadResult::Failed;
	}

	File.close();

	return FilesystemReadResult::Success;
}

FilesystemWriteResult FsFilesystemImpl::Write(uint64 Offset, uint64 Length, const uint8* Source)
{
	// Use STL to write to the file that we created in the Initialize FsFunction. It needs to overwrite bytes in the file at the specified offset.
	// It should not change the file size.

	//FsLogger::LogFormat(FilesystemLogType::Info, "Writing %u bytes to %u", Length, Offset);

	std::fstream File(VirtualFileName, std::ios::in | std::ios::out | std::ios::binary);
	if (!File.is_open())
	{
		FsLogger::LogFormat(FilesystemLogType::Error, "Failed to open file: %s, at offset %u and length %u", VirtualFileName, Offset, Length);
		return FilesystemWriteResult::Failed;
	}

	File.seekp(Offset);
	if (File.fail())
	{
		FsLogger::LogFormat(FilesystemLogType::Error, "Failed to seek to offset %u", Offset);
		return FilesystemWriteResult::Failed;
	}

	File.write(reinterpret_cast<const char*>(Source), Length);
	if (File.fail())
	{
		FsLogger::LogFormat(FilesystemLogType::Error, "Failed to write %u bytes", Length);
		return FilesystemWriteResult::Failed;
	}

	File.close();
	if (File.fail())
	{
		FsLogger::LogFormat(FilesystemLogType::Error, "Failed to close file: %s", VirtualFileName);
		return FilesystemWriteResult::Failed;
	}

	return FilesystemWriteResult::Success;
}

void FsLoggerImpl::OutputLog(const char* String, FilesystemLogType LogType)
{
	const char* logTypeString = "Unknown";
	switch (LogType)
	{
	case FilesystemLogType::Info:
		logTypeString = "Info";
		break;
	case FilesystemLogType::Warning:
		logTypeString = "Warning";
		break;
	case FilesystemLogType::Error:
		logTypeString = "Error";
		break;
	}

	// Set the color of the console output based on the log type
	// Store the existing color
	const HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
	CONSOLE_SCREEN_BUFFER_INFO consoleInfo;
	GetConsoleScreenBufferInfo(hConsole, &consoleInfo);
	const WORD consoleColor = consoleInfo.wAttributes;

	// Set the color based on the log type
	switch (LogType)
	{
	case FilesystemLogType::Info:
		SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
		break;
	case FilesystemLogType::Warning:
		SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN);
		break;
	case FilesystemLogType::Error:
		SetConsoleTextAttribute(hConsole, FOREGROUND_RED);
		break;
	}

	std::cout << "VirtualFileImplementation: " << String << std::endl;

	// Restore the color
	SetConsoleTextAttribute(hConsole, consoleColor);
}

void* FsMemoryAllocatorImpl::Allocate(uint64 Size)
{
	// malloc
	return malloc(Size);
}

void FsMemoryAllocatorImpl::Free(void* Memory)
{
	// free
	free(Memory);
}