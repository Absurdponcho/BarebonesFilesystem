#pragma once

#include "Filesystem.h"
#include "FsMemory.h"
#include "FsLogger.h"
#include <fstream>

class FsLoggerImpl : public FsLogger
{
public:
	virtual void OutputLog(const char* String, FilesystemLogType LogType) override;
};

class FsMemoryAllocatorImpl : public FsMemoryAllocator
{
protected:
	virtual void* Allocate(uint64 Size) override;
	virtual void Free(void* Memory) override;
};

class FsFilesystemImpl : public FsFilesystem
{
public:
	FsFilesystemImpl(uint64 InPartitionSize, uint64 InBlockSize);
	~FsFilesystemImpl();

protected:
	virtual FilesystemReadResult Read(uint64 Offset, uint64 Length, uint8* Destination) override;
	virtual FilesystemWriteResult Write(uint64 Offset, uint64 Length, const uint8* Source) override;

	void CreateVirtualFile(uint64 InPartitionSize);

	// The name of the file that we will use for the FsFilesystem implementation
	const char* VirtualFileName = "VirtualFileSystem.dat";

	std::fstream File;

};

