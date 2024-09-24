#include "FilesystemImplementation.h"
#include <fstream>
#include <iostream>
#include <climits>
#include <cstring>

#ifdef CMAKE
#include "config.h"
#elif defined(_WIN32)
#define HAS_STRERROR_S 1
#include <windows.h>
#endif

void FsFilesystemImpl::CreateVirtualFile(uint64 InPartitionSize)
{
	// If it already exists, don't create it again
	std::ifstream ExistingFile(VirtualFileName);
	if (ExistingFile.is_open())
	{
		ExistingFile.close();
		return;
	}

	std::ofstream VFile(VirtualFileName);
	if (!VFile.is_open())
	{
		FsLogger::LogFormat(FilesystemLogType::Error, "Failed to open file: %s", VirtualFileName);
		return;
	}

	VFile.seekp(InPartitionSize);
	if (VFile.fail())
	{
		FsLogger::LogFormat(FilesystemLogType::Error, "Failed to seek to 1GB");
		return;
	}

	VFile << '\0';
	if (VFile.fail())
	{
		FsLogger::LogFormat(FilesystemLogType::Error, "Failed to write 1GB");
		return;
	}

	VFile.close();
	if (VFile.fail())
	{
		FsLogger::LogFormat(FilesystemLogType::Error, "Failed to close file: %s", VirtualFileName);
		return;
	}

	//FsLogger::LogFormat(FilesystemLogType::Info, "Created 1GB file at full path: %s", );

	// Get full file path
	#ifdef _WIN32
	char FullPath[MAX_PATH];
	DWORD result = GetFullPathNameA(VirtualFileName, MAX_PATH - 1, FullPath, nullptr);
	if (result == 0 || result > MAX_PATH) {
		FsLogger::LogFormat(FilesystemLogType::Error, "Failed to get full path for file: %s", VirtualFileName);
		return;
	}
	FullPath[result] = '\0'; // Ensure null-termination
	#else
	
	char FullPath[PATH_MAX];
	if(!realpath(VirtualFileName, FullPath)) {
		FsLogger::LogFormat(FilesystemLogType::Error, "Failed to get full path for file: %s", VirtualFileName);
		return;
	}

	#endif

	FsLogger::LogFormat(FilesystemLogType::Info, "Created 1GB file at full path: %s", FullPath);

}

FsFilesystemImpl::FsFilesystemImpl(uint64 InPartitionSize, uint64 InBlockSize)
	: FsFilesystem(InPartitionSize, InBlockSize)
{
	// Use STL to create a file of 1GB size that we will use for the FsFilesystem implementation

	CreateVirtualFile(InPartitionSize);

	File.open(VirtualFileName, std::ios::in | std::ios::out | std::ios::binary);
	if (!File.is_open())
	{
		FsLogger::LogFormat(FilesystemLogType::Error, "Failed to open file: %s", VirtualFileName);
		return;
	}
}

FsFilesystemImpl::~FsFilesystemImpl()
{
	if (File.is_open())
	{
		File.close();
	}
}

FilesystemReadResult FsFilesystemImpl::Read(uint64 Offset, uint64 Length, uint8* Destination)
{
	//FsLogger::LogFormat(FilesystemLogType::Info, "Reading %u bytes from %u", Length, Offset);
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
		char ErrorBuffer[256];
		#if HAS_STRERROR_S
		strerror_s(ErrorBuffer, 256, errno);
		#elif HAS_STRERROR_R
		strerror_r(errno, ErrorBuffer, 256);
		#else
		ErrorBuffer[0] = 0;
		#warning "No acceptable strerror found!"
		#endif
		FsLogger::LogFormat(FilesystemLogType::Error, "Failed to read %u bytes at offset %u. Reason %s", Length, Offset, &ErrorBuffer);
		return FilesystemReadResult::Failed;
	}

	return FilesystemReadResult::Success;
}

FilesystemWriteResult FsFilesystemImpl::Write(uint64 Offset, uint64 Length, const uint8* Source)
{
	//FsLogger::LogFormat(FilesystemLogType::Info, "Writing %u bytes to %u", Length, Offset);

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

	return FilesystemWriteResult::Success;
}

void FsLoggerImpl::OutputLog(const char* String, FilesystemLogType LogType)
{
	const char* logTypeString = nullptr;
	switch (LogType)
	{
	case FilesystemLogType::Info:
		logTypeString = "   Info";
		break;
	case FilesystemLogType::Warning:
		logTypeString = "Warning";
		break;
	case FilesystemLogType::Error:
		logTypeString = "  Error";
		break;
	case FilesystemLogType::Verbose:
		logTypeString = "Verbose";
		break;
	case FilesystemLogType::Fatal:
		logTypeString = "  Fatal";
		break;
	default:
		logTypeString = "Unknown";
		break;
	}

#ifdef WIN32
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
	case FilesystemLogType::Verbose:
		SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
		break;
	case FilesystemLogType::Warning:
		SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN);
		break;
	case FilesystemLogType::Error:
	case FilesystemLogType::Fatal:
		SetConsoleTextAttribute(hConsole, FOREGROUND_RED);
		break;
	}

	std::cout << "VFImpl: " << logTypeString << ": " << String << std::endl;

	// Restore the color
	SetConsoleTextAttribute(hConsole, consoleColor);
#else
	switch(LogType)
	{
	case FilesystemLogType::Info:
	case FilesystemLogType::Verbose:
		std::cout << "\033[37m";
		break;
	case FilesystemLogType::Warning:
		std::cout << "\033[33m";
		break;
	case FilesystemLogType::Error:
	case FilesystemLogType::Fatal:
		std::cout << "\033[31m";
		break;
	}

	std::cout << "VFImpl: " << logTypeString << ": " << String << "\033[0m" << std::endl;
#endif
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