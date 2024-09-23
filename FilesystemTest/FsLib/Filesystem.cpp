#include "Filesystem.h"
#include "FsBitStream.h"

FsMemoryAllocator* FsMemoryAllocator::Instance = nullptr;
FsMemoryAllocator::FsMemoryAllocator()
{
	if (Instance == nullptr)
	{
		Instance = this;
	}
	else
	{
		fsCheck(false, "Only one instance of FsMemoryAllocator can be created");
	}
}

uint64 FsFilesystem::NextFileHandleUID = 1;

FsPath FsPath::NormalizePath() const
{
	// Change slashes to forward slashes
	FsPath Result = this->Replace<FsPath>("\\", "/");
	
	// Remove duplicate slashes
	while (Result.Contains("//"))
	{
		Result = Result.Replace<FsPath>("//", "/");
	}

	// Remove trailing slashes
	while (Result.EndsWith("/"))
	{
		Result.RemoveAt(Result.Length() - 1);
	}

	// Make lower case
	Result = Result.ToLower<FsPath>();

	return Result;
}

bool FsPath::GetPathWithoutFileName(FsPath& OutPath) const
{
	if (EndsWith("/"))
	{
		OutPath = *this;
		return true;
	}

	// Find the last slash
	uint64 LastSlashIndex = 0;
	if (!FindLast("/", LastSlashIndex))
	{
		return false;
	}

	OutPath = Substring(0, LastSlashIndex);
	return true;
}

FsPath FsPath::GetLastPath() const
{
	// Find the last slash
	uint64 LastSlashIndex = 0;
	if (!FindLast("/", LastSlashIndex))
	{
		return *this;
	}

	return Substring(LastSlashIndex + 1, Length() - LastSlashIndex);
}

FsPath FsPath::GetFirstPath() const
{
	// Find the first slash
	uint64 FirstSlashIndex = 0;
	const bool bContainsSlash = Contains("/", false, &FirstSlashIndex);
	if (!bContainsSlash)
	{
		return *this;
	}

	return Substring(0, FirstSlashIndex);
}

FsPath FsPath::GetSubPath() const
{
	// Find the first slash
	uint64 FirstSlashIndex = 0;
	const bool bContainsSlash = Contains("/", false, &FirstSlashIndex);
	if (!bContainsSlash)
	{
		return *this;
	}

	return Substring(FirstSlashIndex + 1, Length() - FirstSlashIndex);
}

void FsFileHandle::ResetHandle()
{
	Close();

	UID = 0;
	Flags = EFileHandleFlags::None;
}

FsFileHandle::FsFileHandle(FsFilesystem* InFilesystem, uint64 InUID, EFileHandleFlags InFlags)
{
	OwningFilesystem = InFilesystem;

	UID = InUID;

	Flags = InFlags;
}

FsFileHandle::~FsFileHandle()
{
	Close();
}

void FsFileHandle::Close()
{
	fsCheck(OwningFilesystem, "Cannot close without an owning filesystem!");
	OwningFilesystem->FileHandleClosed(UID);
	ResetHandle();
}

bool FsFileHandle::IsOpen()
{
	return false;
}

void FsFilesystem::Initialize()
{
	LoadOrCreateFilesystemHeader();
}

void FsFilesystem::FileHandleClosed(uint64 HandleUID)
{
	fsCheck(HandleUID > 0, "Attempted to close an invalid file handle");

	// Find the file handle
	for (uint64 i = 0; i < OpenFileHandles.Length(); i++)
	{
		if (OpenFileHandles[i].UID == HandleUID)
		{
			FsLogger::LogFormat(FilesystemLogType::Verbose, "File handle %u closed. File %s", HandleUID, OpenFileHandles[i].FileName.GetData());
			OpenFileHandles.RemoveAt(i);
			return;
		}
	}

	fsCheck(false, "Failed to close a file handle");
}

bool FsFilesystem::CanOpenFile(const FsPath& FileName, EFileHandleFlags Flags)
{
	if (Flags == EFileHandleFlags::None)
	{
		FsLogger::LogFormat(FilesystemLogType::Error, "Cannot create file %s with no flags", *FileName.GetData());
		return false;
	}

	if (IsFileOpen(FileName, Flags))
	{
		FsLogger::LogFormat(FilesystemLogType::Error, "File %s is already open with these flags.", *FileName.GetData());
		return false;
	}

	return true;
}

bool FsFilesystem::CreateFile(const FsPath& FileName, EFileHandleFlags Flags, FsFileHandle& OutHandle)
{
	OutHandle.ResetHandle();
	if (!CanOpenFile(FileName, Flags))
	{
		return false;
	}

	if (OpenFile(FileName, Flags, OutHandle))
	{
		return true;
	}



	return true;
}

bool FsFilesystem::OpenFile(const FsPath& FileName, EFileHandleFlags Flags, FsFileHandle& OutHandle)
{
	OutHandle.ResetHandle();
	if (!CanOpenFile(FileName, Flags))
	{
		return false;
	}

	return true;
}

void FsFileChunkHeader::Serialize(FsBitStream& BitStream)
{
	BitStream << NextBlockIndex;
	BitStream << Blocks;
}

void FsFileDescriptor::Serialize(FsBitStream& BitStream)
{
	BitStream << FileName;
	BitStream << FileSize;
	BitStream << FileOffset;
	BitStream << bIsDirectory;
}

void FsDirectoryDescriptor::Serialize(FsBitStream& BitStream)
{
	uint64 NumFiles = Files.Length();
	BitStream << NumFiles;
	if (BitStream.IsReading())
	{
		Files.FillDefault(NumFiles);
	}

	for (uint64 i = 0; i < NumFiles; i++)
	{
		Files[i].Serialize(BitStream);
	}
}

void FsFilesystemHeader::Serialize(FsBitStream& BitStream)
{
	BitStream << MagicNumber;
	if (BitStream.IsReading())
	{
		// Check the magic number
		if (MagicNumber != FS_MAGIC)
		{
			FsLogger::LogFormat(FilesystemLogType::Warning, "Invalid magic number in filesystem header. Expected %u, got %u. Perhaps the filesystem is not set up.", FS_MAGIC, MagicNumber);
			return;
		}
	}

	BitStream << FilesystemVersion;
	FsLogger::LogFormat(FilesystemLogType::Verbose, "Serialized Filesystem version: %s", FilesystemVersion.GetData());

	RootDirectory.Serialize(BitStream);
}

void FsFilesystem::LoadOrCreateFilesystemHeader()
{
	FsLogger::LogFormat(FilesystemLogType::Verbose, "Loading or creating filesystem header");
	
	FsBitArray HeaderBuffer;
	HeaderBuffer.FillZeroed(FS_HEADER_MAXSIZE);

	// read the first bytes of the filesystem to find the file system header
	const FilesystemReadResult ReadResult = Read(0, FS_HEADER_MAXSIZE, HeaderBuffer.GetInternalArray().GetData());
	if (ReadResult != FilesystemReadResult::Success)
	{
		FsLogger::LogFormat(FilesystemLogType::Error, "Failed to read filesystem header. Ensure `Read` is implemented correctly.");
		return;
	}

	FsBitReader HeaderReader = FsBitReader(HeaderBuffer);

	FsFilesystemHeader FilesystemHeader;
	FilesystemHeader.Serialize(HeaderReader);

	if (FilesystemHeader.MagicNumber != FS_MAGIC)
	{
		FsLogger::LogFormat(FilesystemLogType::Warning, "Filesystem header not found. Creating a new one.");

		FilesystemHeader.MagicNumber = FS_MAGIC;
		FilesystemHeader.FilesystemVersion = FS_VERSION;
		FilesystemHeader.RootDirectory = FsDirectoryDescriptor();

		ClearBlockBuffer();

		FsFileDescriptor RootDirectory;
		RootDirectory.FileName = "Root";
		RootDirectory.bIsDirectory = true;

		// find a block for the root directory
		const FsBlockArray RootDirectoryBlocks = GetFreeBlocks(1);
		if (RootDirectoryBlocks.Length() == 0)
		{
			FsLogger::LogFormat(FilesystemLogType::Error, "Failed to find a block for the root directory");
			return;
		}

		SetBlocksInUse(RootDirectoryBlocks, true);

		const uint64 AbsoluteOffset = BlockIndexToAbsoluteOffset(RootDirectoryBlocks[0]);

		RootDirectory.FileOffset = AbsoluteOffset;
		RootDirectory.FileSize = 0;

		SaveFilesystemHeader(FilesystemHeader);

		FsLogger::LogFormat(FilesystemLogType::Verbose, "Filesystem header created successfully. Root directory located at %u bytes.", RootDirectory.FileOffset);
		return;
	}

	RootDirectory = FilesystemHeader.RootDirectory;
	FsLogger::LogFormat(FilesystemLogType::Verbose, "Filesystem header loaded successfully");

	// List the amount of root files and then list them in a comma separated list.
	FsLogger::LogFormat(FilesystemLogType::Verbose, "Root directory has %u files", FilesystemHeader.RootDirectory.Files.Length());
	FsString RootFilesList = "";
	for (const FsFileDescriptor& File : FilesystemHeader.RootDirectory.Files)
	{
		RootFilesList.Append(File.FileName);
		RootFilesList.Append(", ");
	}
	FsLogger::LogFormat(FilesystemLogType::Verbose, "Root directory files: %s", RootFilesList.GetData());
}

void FsFilesystem::SetBlocksInUse(const FsBaseArray<uint64>& BlockIndices, bool bInUse)
{
	fsCheck(BlockIndices.Length() > 0, "BlockIndices must have at least one element");

	for (uint64 BlockIndex : BlockIndices)
	{
		FsLogger::LogFormat(FilesystemLogType::Verbose, "Setting block %u at %u in use: %s", BlockIndex, BlockIndexToAbsoluteOffset(BlockIndex), bInUse ? "true" : "false");
	}

	// Load the existing block buffer
	FsBitArray BlockBuffer;
	BlockBuffer.FillZeroed(GetBlockBufferSizeBytes());

	const FilesystemReadResult ReadResult = Read(GetBlockBufferOffset(), GetBlockBufferSizeBytes(), BlockBuffer.GetInternalArray().GetData());
	if (ReadResult != FilesystemReadResult::Success)
	{
		FsLogger::LogFormat(FilesystemLogType::Error, "Failed to read block buffer. Ensure `Read` is implemented correctly.");
		return;
	}

	for (uint64 BlockIndex : BlockIndices)
	{
		// Set the block in use
		BlockBuffer.SetBit(BlockIndex, bInUse);
	}

	// Write the block back
	const FilesystemWriteResult WriteResult = Write(GetBlockBufferOffset(), GetBlockBufferSizeBytes(), BlockBuffer.GetInternalArray().GetData());
	if (WriteResult != FilesystemWriteResult::Success)
	{
		FsLogger::LogFormat(FilesystemLogType::Error, "Failed to write block buffer. Ensure `Write` is implemented correctly.");
	}
}

void FsFilesystem::ClearBlockBuffer()
{
	FsArray<uint8> ZeroBuffer = FsArray<uint8>();
	ZeroBuffer.FillZeroed(GetBlockBufferSizeBytes());

	const FilesystemWriteResult WriteResult = Write(GetBlockBufferOffset(), GetBlockBufferSizeBytes(), ZeroBuffer.GetData());
	if (WriteResult != FilesystemWriteResult::Success)
	{
		FsLogger::LogFormat(FilesystemLogType::Error, "Failed to clear block buffer. Ensure `Write` is implemented correctly.");
	}

	FsLogger::LogFormat(FilesystemLogType::Verbose, "Block buffer cleared");
}

FsBitArray FsFilesystem::ReadBlockBuffer()
{
	FsBitArray BlockBuffer;
	BlockBuffer.FillZeroed(GetBlockBufferSizeBytes());

	const FilesystemReadResult ReadResult = Read(GetBlockBufferOffset(), GetBlockBufferSizeBytes(), BlockBuffer.GetInternalArray().GetData());
	if (ReadResult != FilesystemReadResult::Success)
	{
		FsLogger::LogFormat(FilesystemLogType::Error, "Failed to read block buffer. Ensure `Read` is implemented correctly.");
	}

	return BlockBuffer;
}

FsBlockArray FsFilesystem::GetFreeBlocks(uint64 NumBlocks)
{
	FsBitArray BlockBuffer;
	BlockBuffer.FillZeroed(GetBlockBufferSizeBytes());

	const FilesystemReadResult ReadResult = Read(GetBlockBufferOffset(), GetBlockBufferSizeBytes(), BlockBuffer.GetInternalArray().GetData());
	if (ReadResult != FilesystemReadResult::Success)
	{
		FsLogger::LogFormat(FilesystemLogType::Error, "Failed to read block buffer. Ensure `Read` is implemented correctly.");
		return FsBlockArray();
	}
	
	// Calculate the minimum block index that we should skip to avoid the block buffer.
	const uint64 MinBlockIndex = GetBlockBufferSizeBytes() / BlockSize;

	FsBlockArray FreeBlocks = FsBlockArray();
	uint64 NumFreeBlocks = 0;
	for (uint64 i = MinBlockIndex; i < BlockBuffer.BitLength(); i++)
	{
		if (!BlockBuffer.GetBit(i))
		{
			FreeBlocks.Add(i);
			NumFreeBlocks++;
		}

		if (NumFreeBlocks >= NumBlocks)
		{
			break;
		}
	}

	if (NumFreeBlocks < NumBlocks)
	{
		FsLogger::LogFormat(FilesystemLogType::Error, "Failed to find %u free blocks. Only %u available.", NumBlocks, NumFreeBlocks);
		return FsBlockArray();
	}

	return FreeBlocks;
}

void FsFilesystem::SaveFilesystemHeader(const FsFilesystemHeader& InHeader)
{
	FsLogger::LogFormat(FilesystemLogType::Verbose, "Writing filesystem header");

	FsBitArray HeaderBuffer = FsBitArray();
	FsBitWriter HeaderWriter = FsBitWriter(HeaderBuffer);

	const_cast<FsFilesystemHeader&>(InHeader).Serialize(HeaderWriter);

	const FilesystemWriteResult WriteResult = Write(0, HeaderBuffer.ByteLength(), HeaderBuffer.GetInternalArray().GetData());
	if (WriteResult != FilesystemWriteResult::Success)
	{
		FsLogger::LogFormat(FilesystemLogType::Error, "Failed to write filesystem header. Ensure `Write` is implemented correctly.");
	}

	FsLogger::LogFormat(FilesystemLogType::Verbose, "Filesystem header written successfully");
}

bool FsFilesystem::DirectoryExists(const FsPath& InDirectoryName)
{
	FsDirectoryDescriptor DirectoryDescriptor;
	return GetDirectory(InDirectoryName, DirectoryDescriptor);
}

bool FsFilesystem::GetDirectory(const FsPath& InDirectoryName, FsDirectoryDescriptor& OutDirectoryDescriptor)
{
	if (InDirectoryName.Contains("."))
	{
		FsLogger::LogFormat(FilesystemLogType::Error, "Cannot get directory with a file extension: %s", InDirectoryName.GetData());
		return false;
	}

	FsPath NormalizedPath = InDirectoryName.NormalizePath();
	FsLogger::LogFormat(FilesystemLogType::Verbose, "Getting directory for %s", NormalizedPath.GetData());

	return GetDirectory_Internal(NormalizedPath, RootDirectory, OutDirectoryDescriptor);
}

bool FsFilesystem::GetDirectory_Internal(const FsPath& DirectoryPath, const FsDirectoryDescriptor& CurrentDirectory, FsDirectoryDescriptor& OutDirectory)
{
	const FsPath TopLevelDirectory = DirectoryPath.GetFirstPath();
	const FsPath SubDirectory = DirectoryPath.GetSubPath();

	// Check if the top level directory exists
	for (const FsFileDescriptor& SubDirectoryFile : CurrentDirectory.Files)
	{
		if (!SubDirectoryFile.bIsDirectory || SubDirectoryFile.FileName != TopLevelDirectory)
		{
			continue;
		}

		if (!DirectoryPath.Contains("/"))
		{
			// Finished recursing, found a directory with the same name
			FsLogger::LogFormat(FilesystemLogType::Verbose, "Found Directory %s", SubDirectoryFile.FileName.GetData());
			OutDirectory = ReadFileAsDirectory(SubDirectoryFile);
			return true;
		}

		FsLogger::LogFormat(FilesystemLogType::Verbose, "Found Directory %s, recursing deeper.", SubDirectoryFile.FileName.GetData());

		// Load the found directory
		const FsDirectoryDescriptor NextDirectory = ReadFileAsDirectory(SubDirectoryFile);

		// Recurse into the next directory
		const bool bSubPathResult = GetDirectory_Internal(SubDirectory, NextDirectory, OutDirectory);
		if (!bSubPathResult)
		{
			FsLogger::LogFormat(FilesystemLogType::Error, "Failed to get directory %s", SubDirectory.GetData());
			return false;
		}

		return bSubPathResult;
	}

	return false;
}

bool FsFilesystem::CreateDirectory(const FsPath& InDirectoryName)
{
	if (InDirectoryName.Contains("."))
	{
		FsLogger::LogFormat(FilesystemLogType::Error, "Cannot create directory with a file extension: %s", InDirectoryName.GetData());
		return false;
	}

	FsPath NormalizedPath = InDirectoryName.NormalizePath();
	FsLogger::LogFormat(FilesystemLogType::Verbose, "Creating directory for %s", NormalizedPath.GetData());

	bool bNeedsResave = false;
	if (!CreateDirectory_Internal(NormalizedPath, RootDirectory, bNeedsResave))
	{
		FsLogger::LogFormat(FilesystemLogType::Error, "Failed to create directory %s", NormalizedPath.GetData());
		return false;
	}

	if (bNeedsResave)
	{
		// Resave the root directory, since it's saved with the filesystem header we will need to save that too.
		FsFilesystemHeader Header = FsFilesystemHeader();
		Header.RootDirectory = RootDirectory;
		SaveFilesystemHeader(Header);
	}

	return true;
}

bool FsFilesystem::CreateDirectory_Internal(const FsPath& DirectoryName, FsDirectoryDescriptor& CurrentDirectory, bool& bOutNeedsResave)
{
	const FsPath TopLevelDirectory = DirectoryName.GetFirstPath();
	const FsPath SubDirectory = DirectoryName.GetSubPath();

	// Check if the directory already exists
	for (const FsFileDescriptor& SubDirectoryFile : CurrentDirectory.Files)
	{
		if (!SubDirectoryFile.bIsDirectory || SubDirectoryFile.FileName != TopLevelDirectory)
		{
			continue;
		}

		if (!SubDirectory.Contains("/"))
		{
			// Finished recursing, found a directory with the same name
			FsLogger::LogFormat(FilesystemLogType::Error, "Directory %s already exists", DirectoryName.GetData());
			return false;
		}

		FsLogger::LogFormat(FilesystemLogType::Verbose, "Found Directory %s, recursing deeper.", SubDirectoryFile.FileName.GetData());

		// Load the found directory
		FsDirectoryDescriptor NextDirectory = ReadFileAsDirectory(SubDirectoryFile);

		// Recurse into the next directory
		bool bNeedsResave = false;
		if (!CreateDirectory_Internal(SubDirectory, NextDirectory, bNeedsResave))
		{
			FsLogger::LogFormat(FilesystemLogType::Error, "Failed to create directory %s", SubDirectory.GetData());
			return false;
		}

		// If the directory was modified by the recursed call adding a new file, we need will to resave it
		if (bNeedsResave)
		{
			if (!SaveDirectory(NextDirectory, SubDirectoryFile.FileOffset))
			{
				FsLogger::LogFormat(FilesystemLogType::Error, "Failed to save directory %s", SubDirectory.GetData());
				return false;
			}
			FsLogger::LogFormat(FilesystemLogType::Verbose, "Added file directory %s to directory %s at offset %u", SubDirectory.GetData(), SubDirectoryFile.FileName.GetData(), SubDirectoryFile.FileOffset);
		}
	}

	// The directory does not exist, so we need to create it, along with a new file.
	FsDirectoryDescriptor NewDirectory = FsDirectoryDescriptor();

	FsLogger::LogFormat(FilesystemLogType::Verbose, "Creating directory %s", TopLevelDirectory.GetData());
	
	// Recurse into the new directory if we have subdirectories, so we can populate its files before we save it.
	if (DirectoryName.Contains("/"))
	{
		FsLogger::LogFormat(FilesystemLogType::Verbose, "Creating subdirectory %s", SubDirectory.GetData());

		bool bNeedsResave = false;
		if (!CreateDirectory_Internal(SubDirectory, NewDirectory, bNeedsResave))
		{
			FsLogger::LogFormat(FilesystemLogType::Error, "Failed to create subdirectory %s", SubDirectory.GetData());
			return false;
		}
	}

	// Allocate a block on the filesystem for the new directory file.
	const FsBlockArray NewDirectoryBlocks = GetFreeBlocks(1);
	if (NewDirectoryBlocks.Length() == 0)
	{
		FsLogger::LogFormat(FilesystemLogType::Error, "Failed to find a block for the new directory");
		return false;
	}

	SetBlocksInUse(NewDirectoryBlocks, true);

	const uint64 AbsoluteOffset = BlockIndexToAbsoluteOffset(NewDirectoryBlocks[0]);

	// Save the new directory
	if (!SaveDirectory(NewDirectory, AbsoluteOffset))
	{
		FsLogger::LogFormat(FilesystemLogType::Error, "Failed to write new directory");
		return false;
	}

	FsFileDescriptor NewDirectoryFile;
	NewDirectoryFile.FileName = TopLevelDirectory;
	NewDirectoryFile.bIsDirectory = true;
	NewDirectoryFile.FileOffset = AbsoluteOffset;
	NewDirectoryFile.FileSize = 0;

	// Add the new file to the current directory and request a resave
	CurrentDirectory.Files.Add(NewDirectoryFile);
	bOutNeedsResave = true;
	return true;
}

bool FsFilesystem::SaveDirectory(const FsDirectoryDescriptor& Directory, uint64 AbsoluteOffset)
{
	// Resave the current directory
	FsBitArray NewDirectoryBuffer = FsBitArray();
	FsBitWriter NewDirectoryWriter = FsBitWriter(NewDirectoryBuffer);

	// Dont forget the chunk header
	FsFileChunkHeader ChunkHeader = FsFileChunkHeader();
	ChunkHeader.NextBlockIndex = 0;
	ChunkHeader.Blocks = 1;

	ChunkHeader.Serialize(NewDirectoryWriter);

	const_cast<FsDirectoryDescriptor&>(Directory).Serialize(NewDirectoryWriter);

	if (!WriteSingleChunk(NewDirectoryBuffer, AbsoluteOffset))
	{
		FsLogger::LogFormat(FilesystemLogType::Error, "Failed to write new directory");
		return false;
	}

	return true;
}

FsDirectoryDescriptor FsFilesystem::ReadFileAsDirectory(const FsFileDescriptor& FileDescriptor)
{
	// Read the first block of the file
	const uint64 ReadOffset = FileDescriptor.FileOffset;
	const uint64 ReadLength = BlockSize;

	FsBitArray FileBuffer = FsBitArray();
	FileBuffer.FillZeroed(ReadLength);

	const FilesystemReadResult ReadResult = Read(ReadOffset, ReadLength, FileBuffer.GetInternalArray().GetData());
	if (ReadResult != FilesystemReadResult::Success)
	{
		FsLogger::LogFormat(FilesystemLogType::Error, "Failed to read file as directory");
		return FsDirectoryDescriptor();
	}

	FsBitReader FileReader = FsBitReader(FileBuffer);

	// Read the file chunk header
	FsFileChunkHeader FileChunkHeader;
	FileChunkHeader.Serialize(FileReader);

	// See if we need to read more chunks
	// TODO read more chunks (currently supporting only 1 chunk for directories rn)

	// Now we have the whole file in memory, we can read the directory descriptor
	FsDirectoryDescriptor DirectoryDescriptor;
	DirectoryDescriptor.Serialize(FileReader);

	return DirectoryDescriptor;
}

bool FsFilesystem::IsFileOpen(const FsPath& FileName, EFileHandleFlags Flags)
{
	const FsPath NormalizedPath = FileName.NormalizePath();

	return OpenFileHandles.ContainsByPredicate([&](const FsOpenFileHandle& Handle)
		{
			// Check if the file name matches and at least one bit in the flags matches
			const bool bSameFileName = Handle.FileName == NormalizedPath;
			const bool bAnyFlagsMatch = (Handle.Flags & Flags) != EFileHandleFlags::None;
			return bSameFileName && bAnyFlagsMatch;
		});
}

bool FsFilesystem::WriteSingleChunk(const FsBitArray& ChunkData, uint64 AbsoluteOffset)
{
	fsCheck(ChunkData.ByteLength() <= BlockSize, "Tried to write too much data to a single chunk!");
	if (ChunkData.ByteLength() > BlockSize)
	{
		FsLogger::LogFormat(FilesystemLogType::Error, "Tried to write too much data to a single chunk!");
		return false;
	}

	const FilesystemWriteResult WriteResult = Write(AbsoluteOffset, ChunkData.ByteLength(), ChunkData.GetInternalArray().GetData());
	if (WriteResult != FilesystemWriteResult::Success)
	{
		FsLogger::LogFormat(FilesystemLogType::Error, "Failed to write to %u", AbsoluteOffset);
		return false;
	}

	return true;
}
