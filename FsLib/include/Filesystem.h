#pragma once
#include "FsLogger.h"
#include "FsString.h"

class FsBitStream;
class FsFilesystem;
struct FsDirectoryDescriptor;
struct FsFileDescriptor;

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

	virtual ~FsPath()
	{
		// Set count to zero to skip calling the destructor on all the characters
		Data.SetCount(0);
	}

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

	FsPath GetPathWithoutFileName() const;

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

	// equals operator
	bool operator==(const FsFileDescriptor& InFileDescriptor) const
	{
		return FileName == InFileDescriptor.FileName && FileOffset == InFileDescriptor.FileOffset && FileSize == InFileDescriptor.FileSize && bIsDirectory == InFileDescriptor.bIsDirectory;
	}

};

struct FsDirectoryDescriptor
{
	FsFileArray Files = FsFileArray();

	bool bDirectoryIsRoot = false;

	void Serialize(FsBitStream& BitStream);
};

struct FsFilesystemHeader
{
	uint64 MagicNumber = FS_MAGIC;
	FsFixedLengthString<32> FilesystemVersion = FS_VERSION;
	FsDirectoryDescriptor RootDirectory;

	void Serialize(FsBitStream& BitStream);
};

struct FsCachedChunkList
{
	FsPath FileName;
	FsArray<FsFileChunkHeader> Chunks;
};

struct FsCachedDirectory
{
	uint64 Offset;
	FsDirectoryDescriptor Directory;
};

struct FsReadCache
{
	uint64 BlockIndex = 0;
	FsArray<uint8> Data;
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
	bool CreateFile(const FsPath& FileName);
	bool FileExists(const FsPath& InFileName);
	bool CreateDirectory(const FsPath& InDirectoryName);
	bool WriteToFile(const FsPath& InPath, const uint8* Source, uint64 InOffset, uint64 InLength);
	bool ReadFromFile(const FsPath& InPath, uint64 Offset, uint8* Destination, uint64 Length, uint64* OutBytesRead = nullptr);
	bool FsDeleteDirectory(const FsPath& DirectoryName);
	bool FsIsDirectoryEmpty(const FsPath& DirectoryName);
	bool FsDeleteFile(const FsPath& FileName);
	bool FsMoveFile(const FsPath& SourceFileName, const FsPath& DestinationFileName);
	bool CopyFile(const FsPath& SourceFileName, const FsPath& DestinationFileName);
	bool GetDirectory(const FsPath& InDirectoryName, FsDirectoryDescriptor& OutDirectoryDescriptor, FsFileDescriptor* OutDirectoryFile = nullptr);
	bool DirectoryExists(const FsPath& InDirectoryName);
	bool GetFile(const FsPath& InFileName, FsFileDescriptor& OutFileDescriptor);
	bool GetFileSize(const FsPath& InFileName, uint64& OutFileSize);
	bool GetTotalAndFreeBytes(uint64& OutTotalBytes, uint64& OutFreeBytes);
	uint64 GetTotalUsableSpace()
	{
		return GetContentEndOffset() - GetContentStartOffset();
	}

	void LogAllFiles();

	// Formats the given bytes into a human readable string.
	// The value that is returned will not be valid after the next call to this function, so it must be copied if it needs to be kept.
	static const char* GetCompressedBytesString(uint64 Bytes);

	uint64 GetPartitionSize() const
	{
		return PartitionSize;
	}

	uint64 GetBlockSize() const
	{
		return BlockSize;
	}

protected:

	void ValidateFileWrite(const FsPath& InPath, const uint8* Source, uint64 InOffset, uint64 InLength);

	void LogAllFiles_Internal(const FsDirectoryDescriptor& CurrentDirectory, uint64 Depth);

	bool CreateDirectory_Internal(const FsPath& InDirectoryName, FsDirectoryDescriptor& CurrentDirectory, bool& bOutNeedsResave);
	bool GetDirectory_Internal(const FsPath& InDirectoryName, const FsDirectoryDescriptor& CurrentDirectory, FsDirectoryDescriptor& OutDirectory, FsFileDescriptor* OutDirectoryFile);
	bool CreateFile_Internal(const FsPath& FileName, FsDirectoryDescriptor& CurrentDirectory, bool& bOutNeedsResave);

	// Gets all the chunks for the given file, optionally only getting chunks up to a certain file length.
	FsArray<FsFileChunkHeader> GetAllChunksForFile(const FsPath& InPath, const FsFileDescriptor& FileDescriptor, const uint64* OptionalFileLength = nullptr);

	// Compares the file size to the amount of blocks allocated to the file.
	uint64 GetFreeAllocatedSpaceInFileChunks(const FsPath& InPath, const FsFileDescriptor& FileDescriptor, const FsArray<FsFileChunkHeader>* OptionalInChunks);

	uint64 GetAllocatedSpaceInFileChunks(const FsArray<FsFileChunkHeader>& InChunks);

	bool WriteEntireFile_Internal(FsFileDescriptor& FileDescriptor, const uint8* Source, uint64 Length);

	virtual FilesystemReadResult Read(uint64 Offset, uint64 Length, uint8* Destination) = 0;
	virtual FilesystemWriteResult Write(uint64 Offset, uint64 Length, const uint8* Source) = 0;

	friend class CheckImplementer;
	friend class FsLogger;
	friend class FsMemory;

	void LoadOrCreateFilesystemHeader();
	void SaveFilesystemHeader(const FsFilesystemHeader& InHeader);
	void SetBlocksInUse(const FsBlockArray& BlockIndices, bool bInUse);
	void ClearBlockBuffer();
	FsBitArray ReadBlockBuffer();
	FsBlockArray GetFreeBlocks(uint64 NumBlocks);
	bool GetUsedBlocksCount(uint64& OutUsedBlocks);

	FsDirectoryDescriptor ReadFileAsDirectory(const FsFileDescriptor& FileDescriptor);
	bool SaveDirectory(const FsDirectoryDescriptor& Directory, uint64 AbsoluteOffset);

	FsDirectoryDescriptor RootDirectory{};

	uint64 PartitionSize;
	uint64 BlockSize;

	// The size of the buffer needed to store 1 bit per block in the partition.
	// This is used to track which blocks are free or in use.
	uint64 GetBlockBufferSizeBytes() const
	{
		const uint64 BitAmount = GetBlockBufferSizeBits();
		const uint64 ByteAmount = BitAmount % 8 == 0 ? BitAmount / 8 : BitAmount / 8 + 1;
		return ByteAmount;
	}

	uint64 GetContentStartOffset() const
	{
		const uint64 BlockBufferOffset = GetBlockBufferOffset();
		uint64 BlockBufferByteSize = GetBlockBufferSizeBytes();
		// pad to the next block
		if (BlockBufferByteSize % BlockSize != 0)
		{
			BlockBufferByteSize += BlockSize - (BlockBufferByteSize % BlockSize);
		}
		return BlockBufferOffset + BlockBufferByteSize;
	}

	uint64 GetContentEndOffset() const
	{
		// The partition size might not be aligned to the block size, so we need to align it DOWNWARDS to the block size.
		if (PartitionSize % BlockSize != 0)
		{
			return PartitionSize - (PartitionSize % BlockSize);
		}
		return PartitionSize;
	}

	uint64 GetBlockBufferSizeBits() const
	{
		const uint64 UsablePartitionSize = PartitionSize - GetBlockBufferOffset();
		const uint64 BitAmount = UsablePartitionSize / BlockSize;
		return BitAmount;
	}

	uint64 GetBlockBufferOffset() const
	{
		if (BlockSize > FS_HEADER_MAXSIZE)
		{
			return BlockSize;
		}
		return FS_HEADER_MAXSIZE;
	}

	uint64 BlockIndexToAbsoluteOffset(uint64 BlockIndex) const
	{
		const uint64 Result = GetBlockBufferOffset() + BlockIndex * BlockSize;
		// Sanity check its aligned to block size
		fsCheck(Result % BlockSize == 0, "Block index is not aligned to block size");
		return Result;
	}

	uint64 AbsoluteOffsetToBlockIndex(uint64 AbsoluteOffset) const
	{
		fsCheck(AbsoluteOffset >= GetBlockBufferOffset(), "Absolute offset is before the block buffer");
		fsCheck(AbsoluteOffset % BlockSize == 0, "Absolute offset must be aligned to the block size");
		const uint64 Result = (AbsoluteOffset - GetBlockBufferOffset()) / BlockSize;
		return Result;
	}

	bool WriteSingleChunk(const FsBitArray& ChunkData, uint64 AbsoluteOffset);

	void CacheChunks(const FsPath& FileName, const FsArray<FsFileChunkHeader>& Chunks);
	void ClearCachedChunks(const FsPath& FileName);
	bool GetCachedChunks(const FsPath& FileName, FsArray<FsFileChunkHeader>& OutChunks);
	FsArray<FsCachedChunkList> CachedChunks;

	void CacheDirectory(uint64 Offset, const FsDirectoryDescriptor& Directory);
	void ClearCachedDirectory(uint64 Offset);
	bool GetCachedDirectory(uint64 Offset, FsDirectoryDescriptor& OutDirectory);
	FsArray<FsCachedDirectory> CachedDirectories;

	FsArray<uint8>* CacheRead(uint64 BlockIndex);
	void ClearCachedRead(uint64 BlockIndex);
	FsArray<uint8>* GetCachedRead(uint64 BlockIndex);
	FsArray<FsReadCache> CachedReads;
};

