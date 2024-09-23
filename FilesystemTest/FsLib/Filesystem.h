#pragma once
#include "FsLogger.h"
#include "FsString.h"

class FsBitStream;
class FsFilesystem;
struct FsDirectoryDescriptor;
struct FsOpenFileHandle;
struct FsFileDescriptor;

typedef FsArray<FsOpenFileHandle> FsFileHandleArray;
typedef FsArray<FsFileDescriptor> FsFileArray;
typedef FsString FsFileNameString;
typedef FsArray<uint64> FsBlockArray;

#define FS_MAGIC 0x1234567890ABCDEF
#define FS_VERSION "Version 1"
#define FS_HEADER_MAXSIZE 4096

struct FsPath : public FsFileNameString
{
public:
	using FsFileNameString::FsFileNameString;

	FsPath(const char* InString) : FsFileNameString(InString)
	{}

	FsPath(const FsFileNameString& InString) : FsFileNameString(InString)
	{}

	FsPath(const FsPath& InPath) : FsFileNameString(InPath)
	{}

	// copy assignment
	FsPath& operator=(const FsPath& InPath)
	{
		FsFileNameString::operator=(InPath);
		return *this;
	}

	NO_DISCARD FsPath NormalizePath() const;

	bool GetPathWithoutFileName(FsPath& OutPath) const;

	FsPath GetSubPath() const;

	FsPath GetLastPath() const;

	FsPath GetFirstPath() const;
};

struct FsFileChunkHeader
{
	// The block index of the next block in the file.
	uint64 NextBlockIndex = 0;

	// The amount of blocks taken by this chunk of the file
	uint64 Blocks = 0;

	void Serialize(FsBitStream& BitStream);
};

struct FsFileDescriptor
{
	FsPath FileName{};

	// The offset of the first file chunk
	uint64 FileOffset = 0;

	// The total size of the file in bytes
	uint64 FileSize = 0;

	// If this file descriptor is a directory
	bool bIsDirectory = false;

	void Serialize(FsBitStream& BitStream);

	// copy assignment
	FsFileDescriptor& operator=(const FsFileDescriptor& InFileDescriptor)
	{
		FileName = InFileDescriptor.FileName;
		FileOffset = InFileDescriptor.FileOffset;
		FileSize = InFileDescriptor.FileSize;
		bIsDirectory = InFileDescriptor.bIsDirectory;
		return *this;
	}
};

struct FsDirectoryDescriptor
{
	FsFileArray Files = FsFileArray();

	void Serialize(FsBitStream& BitStream);
};

struct FsFilesystemHeader
{
	uint64 MagicNumber = FS_MAGIC;
	FsFixedLengthString<32> FilesystemVersion = FS_VERSION;
	FsDirectoryDescriptor RootDirectory;

	void Serialize(FsBitStream& BitStream);
};

class FsFileHandle
{
public:
	FsFileHandle(FsFilesystem* InFilesystem, uint64 InUID, EFileHandleFlags InFlags);

	~FsFileHandle();

	bool Read(uint8* Destination, uint64 Length);
	bool Write(const uint8* Source, uint64 Length);
	uint64 Tell();
	bool Seek(uint64 Offset);
	bool SeekFromEnd(uint64 Offset);
	bool Flush();
	void Close();
	uint64 Size();
	bool IsOpen();

protected:

	friend class FsFilesystem;
	FsFilesystem* OwningFilesystem;

	// Closes the file handle and resets the handle.
	void ResetHandle();

	uint64 UID;
	EFileHandleFlags Flags;
};

struct FsOpenFileHandle
{
public:	
	uint64 UID = 0;
	FsPath FileName;
	EFileHandleFlags Flags = EFileHandleFlags::None;
};

class FsFilesystem
{
public:
	FsFilesystem() = delete;
	FsFilesystem(uint64 InPartitionSize, uint64 InBlockSize)
		: PartitionSize(InPartitionSize), BlockSize(InBlockSize)
	{}
	~FsFilesystem() {}

	void Initialize();

	// Creates a file or opens it if it already exists.
	// Returns true if the file was created or opened.
	// Returns false if the file could not be created or opened, such as if the file is locked.
	bool CreateFile(const FsPath& FileName, EFileHandleFlags Flags, FsFileHandle& OutHandle);
	bool OpenFile(const FsPath& FileName, EFileHandleFlags Flags, FsFileHandle& OutHandle);
	bool FileExists(const FsPath& FileName);
	bool CreateDirectory(const FsPath& DirectoryName);
	void DeleteDirectory(const FsPath& DirectoryName);
	void DeleteFile(const FsPath& FileName);
	void MoveFile(const FsPath& SourceFileName, const FsPath& DestinationFileName);
	void CopyFile(const FsPath& SourceFileName, const FsPath& DestinationFileName);

	// @brief Checks if a file can be opened at the file name and flags
	// @param FileName The name of the file to check.
	// @param Flags The flags to check for.
	// @return True if the file can be opened with the specified flags.
	bool CanOpenFile(const FsPath& FileName, EFileHandleFlags Flags);

	// @brief Checks if the file is already opened for reading or writing.
	// @param FileName The name of the file to check.
	// @param Flags The flags to check for.
	// @return True if the file is already opened with the specified flags.
	bool IsFileOpen(const FsPath& FileName, EFileHandleFlags Flags);

protected:

	bool CreateDirectory_Internal(const FsPath& DirectoryName, const FsDirectoryDescriptor& CurrentDirectory, bool& bOutHasNewFile, FsFileDescriptor& OutNewFile);

	virtual FilesystemReadResult Read(uint64 Offset, uint64 Length, uint8* Destination) = 0;
	virtual FilesystemWriteResult Write(uint64 Offset, uint64 Length, const uint8* Source) = 0;

	friend class FsFileHandle;
	friend class CheckImplementer;
	friend class FsLogger;
	friend class FsMemory;

	bool GetDirectory(const FsPath& DirectoryPath, FsDirectoryDescriptor& OutDirectoryDescriptor);

	void FileHandleClosed(uint64 HandleUID);
	void LoadOrCreateFilesystemHeader();
	void SaveFilesystemHeader(const FsFilesystemHeader& InHeader);
	void SetBlocksInUse(const FsBaseArray<uint64>& BlockIndices, bool bInUse);
	void ClearBlockBuffer();
	FsBitArray ReadBlockBuffer();
	FsBlockArray GetFreeBlocks(uint64 NumBlocks);

	FsDirectoryDescriptor ReadFileAsDirectory(const FsFileDescriptor& FileDescriptor);

	static uint64 NextFileHandleUID;
	
	FsFileHandleArray OpenFileHandles;

	FsDirectoryDescriptor RootDirectory{};

	uint64 PartitionSize;
	uint64 BlockSize;

	// The size of the buffer needed to store 1 bit per block in the partition.
	// This is used to track which blocks are free or in use.
	uint64 GetBlockBufferSizeBytes() const
	{
		const uint64 UsablePartitionSize = PartitionSize - FS_HEADER_MAXSIZE;
		const uint64 TotalSize = UsablePartitionSize / BlockSize / 8;
		// round up to the nearest BlockSize
		return TotalSize + (BlockSize - (TotalSize % BlockSize));
	}

	uint64 GetBlockBufferOffset() const
	{
		return FS_HEADER_MAXSIZE;
	}

	uint64 GetUsablePartitionOffset() const
	{
		return GetBlockBufferOffset() + GetBlockBufferSizeBytes();
	}

	uint64 BlockIndexToAbsoluteOffset(uint64 BlockIndex) const
	{
		const uint64 Result = GetBlockBufferOffset() + BlockIndex * BlockSize;
		// Sanity check its aligned to block size
		fsCheck(Result % BlockSize == 0, "Block index is not aligned to block size");
		return Result;
	}
};

