#include <iostream>
#include "FilesystemImplementation.h"
#include "FsLib/FsArray.h"
#include "FsLib/FsLogger.h"
#include "FsLib/FsString.h"
#include "FsLib/FsBitStream.h"
#include "FsLib/FsTests.h"

int main()
{
	FsLoggerImpl Logger = FsLoggerImpl();
	Logger.SetShouldLogVerbose(false);
	FsMemoryAllocatorImpl Allocator = FsMemoryAllocatorImpl();

	// Make a test fs with a 1GB partition and 1KB block size
	FsFilesystemImpl FsFilesystem = FsFilesystemImpl(1024ull * 1024ull * 1024ull, 1024);

	FsFilesystem.Initialize();

	FsTests::RunTests(FsFilesystem);

	FsFilesystem.LogAllFiles();

	return 0;

}