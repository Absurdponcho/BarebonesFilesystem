#include "FsLib/FsNew.h"
#include <iostream>
#include "FilesystemImplementation.h"
#include "FsLib/FsArray.h"
#include "FsLib/FsLogger.h"
#include "FsLib/FsString.h"
#include "FsLib/FsBitStream.h"

void TestPaths()
{
	FsPath TestPath = "C:\\Users\\User\\Desktop\\Test.txt";

	FsLogger::LogFormat(FilesystemLogType::Info, "Original path: %s", TestPath.GetData());

	TestPath = TestPath.NormalizePath();

	FsLogger::LogFormat(FilesystemLogType::Info, "Normalized path: %s", TestPath.GetData());

	FsPath DirPath = "";
	TestPath.GetPathWithoutFileName(DirPath);

	FsLogger::LogFormat(FilesystemLogType::Info, "Directory path: %s", DirPath.GetData());
}

void TestBitStream()
{
	FsBitArray Buffer = FsBitArray();
	FsBitWriter BitWriter = FsBitWriter(Buffer);

	// Write a uint64, bool, uint8, char, and string to the buffer
	uint64 TestUint64 = 123456789;
	bool TestBool = true;
	uint8 TestUint8 = 255;
	char TestChar = 'A';
	FsString TestString = "Hello, World!";

	BitWriter << TestUint64;
	BitWriter << TestBool;
	BitWriter << TestUint8;
	BitWriter << TestChar;
	BitWriter << TestString;

	// Read them back in order into new variables
	FsBitReader BitReader = FsBitReader(Buffer);

	uint64 ReadUint64 = 0;
	bool ReadBool = false;
	uint8 ReadUint8 = 0;
	char ReadChar = 0;
	FsString ReadString = "";

	BitReader << ReadUint64;
	BitReader << ReadBool;
	BitReader << ReadUint8;
	BitReader << ReadChar;
	BitReader << ReadString;

	// Log the results, both original and read back
	FsLogger::LogFormat(FilesystemLogType::Info, "Original uint64: %u, Read uint64: %u", TestUint64, ReadUint64);
	FsLogger::LogFormat(FilesystemLogType::Info, "Original bool: %s, Read bool: %s", TestBool ? "true" : "false", ReadBool ? "true" : "false");
	FsLogger::LogFormat(FilesystemLogType::Info, "Original uint8: %u, Read uint8: %u", static_cast<uint64>(TestUint8), static_cast<uint64>(ReadUint8));
	FsLogger::LogFormat(FilesystemLogType::Info, "Original char: %c, Read char: %c", TestChar, ReadChar);
	FsLogger::LogFormat(FilesystemLogType::Info, "Original string: %s, Read string: %s", TestString.GetData(), ReadString.GetData());
}

int main()
{
    std::cout << "Hello World!\n";

	FsLoggerImpl Logger = FsLoggerImpl();
	FsMemoryAllocatorImpl Allocator = FsMemoryAllocatorImpl();

	// Make a test fs with a 1GB partition and 1KB block size
	FsFilesystemImpl FsFilesystem = FsFilesystemImpl(1024ull * 1024ull * 1024ull, 1024);

	FsFilesystem.Initialize();

	const char* CursedPath = "Foo/Bar/Baz\\a/b/\\d/test/welp\\\\dead/fart";

	FsDirectoryDescriptor ExistingDirectory;
	if (FsFilesystem.GetDirectory(CursedPath, ExistingDirectory))
	{
		FsLogger::LogFormat(FilesystemLogType::Info, "Directory %s exists!", CursedPath);
	}
	else
	{
		FsFilesystem.CreateDirectory(CursedPath);
	}

	return 0;

}