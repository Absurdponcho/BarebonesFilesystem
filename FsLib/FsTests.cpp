#include "FsTests.h"
#include "Filesystem.h"
#include "FsLogger.h"
#include "FsString.h"
#include "FsBitStream.h"

void FsTests::RunTests(FsFilesystem& InFilesystem)
{
	RUN_TEST(BitStreamTest);
	RUN_TEST(LargeFileTest);
	RUN_TEST(MidFileWriteTest);

	FsLogger::LogFormat(FilesystemLogType::Info, "Tests complete");
}

bool FsTests::StartTest(const char* TestName, FsTestResult(*TestFunction)(FsFilesystem&), FsFilesystem& InFilesystem)
{
	FsLogger::LogFormat(FilesystemLogType::Info, "===== Starting Test %s =====", TestName);
	FsLogger::Log(FilesystemLogType::Info, "----------------------------------------");
	FsLogger::Log(FilesystemLogType::Info, "");

	FsTestResult Result = TestFunction(InFilesystem);

	FsLogger::Log(FilesystemLogType::Info, "");
	FsLogger::Log(FilesystemLogType::Info, "----------------------------------------");
	if (Result.bSucceeded)
	{
		FsLogger::LogFormat(FilesystemLogType::Info, "===== Test %s succeeded =====", TestName);
	}
	else
	{
		FsLogger::LogFormat(FilesystemLogType::Error, "Test %s failed: %s", TestName, Result.TestResult.GetData());
		return false;
	}
	FsLogger::LogFormat(FilesystemLogType::Info, "", TestName);
	return true;
}

FsTestResult FsTests::BitStreamTest(FsFilesystem& InFilesystem)
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

	const bool bSucceeded = TestUint64 == ReadUint64 && TestBool == ReadBool && TestUint8 == ReadUint8 && TestChar == ReadChar && TestString == ReadString;
	FsTestResult Result;
	Result.bSucceeded = bSucceeded;
	Result.TestResult = bSucceeded ? "BitStreamTest succeeded" : "BitStreamTest failed";
	return Result;
}

FsTestResult FsTests::LargeFileTest(FsFilesystem& InFilesystem)
{
	FsTestResult Result;

	const char* DirPath = "Foo/Bar/Baz";
	InFilesystem.CreateDirectory(DirPath);

	const char* TestFileName = "Foo/Bar/Baz/Test.txt";
	InFilesystem.CreateFile(TestFileName);

	FsString TestString;
	for (uint64 i = 0; i < 1000000; i++)
	{
		TestString.Append("123456789-");
	}

	const uint64 FileSize = TestString.Length() + 1;

	InFilesystem.WriteToFile(TestFileName, reinterpret_cast<const uint8*>(TestString.GetData()), 0, FileSize);

	FsString ReadString = FsString();
	ReadString.AddZeroed(FileSize - 1);
	InFilesystem.ReadFromFile(TestFileName, 0, reinterpret_cast<uint8*>(ReadString.GetData()), FileSize);

	// check if the read string is the same as the written string
	if (TestString != ReadString)
	{
		FsLogger::Log(FilesystemLogType::Error, "Strings do not match!");
		Result.bSucceeded = false;
		Result.TestResult = "Failed to match strings after writing and reading from a large file";
		return Result;
	}

	Result.bSucceeded = true;
	Result.TestResult = "LargeFileTest succeeded";
	return Result;
}

FsTestResult FsTests::MidFileWriteTest(FsFilesystem& InFilesystem)
{
	FsTestResult Result;

	const char* DirPath = "Foo/Bar/Baz";
	InFilesystem.CreateDirectory(DirPath);

	const char* TestFileName = "Foo/Bar/Baz/DestroyAllHumans2.txt";
	InFilesystem.CreateFile(TestFileName);

	FsString TestString = "Hello, World! Destroy All Humans! Hello, World!";
	const uint64 FileSize = TestString.Length() + 1;

	InFilesystem.WriteToFile(TestFileName, reinterpret_cast<const uint8*>(TestString.GetData()), 0, FileSize);

	FsString ReadString = FsString();
	ReadString.AddZeroed(FileSize - 1);
	InFilesystem.ReadFromFile(TestFileName, 0, reinterpret_cast<uint8*>(ReadString.GetData()), FileSize);

	// check if the read string is the same as the written string
	if (TestString != ReadString)
	{
		FsLogger::Log(FilesystemLogType::Error, "Strings do not match!");
		Result.bSucceeded = false;
		Result.TestResult = "Failed to match strings after writing and reading from a file at a fixed length";
		return Result;
	}

	// Log the original file contents
	FsLogger::LogFormat(FilesystemLogType::Info, "File contents: %s, File Length %s", ReadString.GetData(), InFilesystem.GetCompressedBytesString(FileSize));

	// Replace Destroy All Humans in the middle of the file with pumpkin pie humans
	FsString ReplaceString = "Pumpkin Pie Humans, Pumpkin Pie Humans, Pumpkin Pie Humans, Pumpkin Pie Humans";
	InFilesystem.WriteToFile(TestFileName, reinterpret_cast<const uint8*>(ReplaceString.GetData()), 14, ReplaceString.Length() + 1);

	// Read the whole file
	ReadString = FsString();

	uint64 NewFileSize = 0;
	if (!InFilesystem.GetFileSize(TestFileName, NewFileSize))
	{
		FsLogger::Log(FilesystemLogType::Error, "Failed to get file size!");
		Result.bSucceeded = false;
		Result.TestResult = "Failed to get the file size after writing to the file.";
		return Result;
	}

	ReadString.AddZeroed(NewFileSize - 1);
	InFilesystem.ReadFromFile(TestFileName, 0, reinterpret_cast<uint8*>(ReadString.GetData()), NewFileSize);

	// display the whole file
	FsLogger::LogFormat(FilesystemLogType::Info, "File contents: %s, File Length %s", ReadString.GetData(), InFilesystem.GetCompressedBytesString(NewFileSize));

	FsString ExpectedString = "Hello, World! Pumpkin Pie Humans, Pumpkin Pie Humans, Pumpkin Pie Humans, Pumpkin Pie Humans";
	if (ReadString == ExpectedString)
	{
		FsLogger::Log(FilesystemLogType::Info, "Strings match!");
		Result.bSucceeded = true;
		Result.TestResult = "MidFileWriteTest succeeded";
		return Result;
	}

	FsLogger::Log(FilesystemLogType::Error, "Strings do not match!");
	Result.bSucceeded = false;
	Result.TestResult = "Failed to match strings after writing and reading from a file after a";
	return Result;
}