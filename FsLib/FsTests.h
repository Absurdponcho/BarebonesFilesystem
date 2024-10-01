#pragma once
#include "FsString.h"

class FsFilesystem;

struct FsTestResult
{
	bool bSucceeded = false;
	FsFixedLengthString<1024> TestResult;
};

class FsTests
{
public:
	static void RunTests(FsFilesystem& InFilesystem);

protected:

#define RUN_TEST_IMPL(TestFunction) StartTest(#TestFunction, TestFunction, InFilesystem)

#define RUN_TEST(TestFunction) if (!RUN_TEST_IMPL(TestFunction)) { FsLogger::LogFormat(FilesystemLogType::Error, "Test %s failed", #TestFunction); return; }

	static bool StartTest(const char* TestName, FsTestResult(*TestFunction)(FsFilesystem&), FsFilesystem& InFilesystem);

	static FsTestResult BitStreamTest(FsFilesystem& InFilesystem);
	static FsTestResult LargeFileTest(FsFilesystem& InFilesystem);
	static FsTestResult MidFileWriteTest(FsFilesystem& InFilesystem);
};