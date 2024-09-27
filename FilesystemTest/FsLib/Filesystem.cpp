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

FsPath FsPath::GetPathWithoutFileName() const
{
	// Find the last slash
	uint64 LastSlashIndex = 0;
	if (!FindLast("/", LastSlashIndex))
	{
		return *this;
	}

	return Substring(0, LastSlashIndex);
}

FsPath FsPath::GetLastPath() const
{
	// Find the last slash
	uint64 LastSlashIndex = 0;
	if (!FindLast("/", LastSlashIndex))
	{
		return *this;
	}

	return Substring(LastSlashIndex + 1, Length() - LastSlashIndex - 1);
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

	return Substring(FirstSlashIndex + 1, Length() - FirstSlashIndex - 1);
}

void FsFilesystem::Initialize()
{
	LoadOrCreateFilesystemHeader();
}

bool FsFilesystem::CreateFile(const FsPath& InFileName)
{
	const FsPath NormalizedPath = InFileName.NormalizePath();
	FsLogger::LogFormat(FilesystemLogType::Verbose, "Creating file for %s", NormalizedPath.GetData());

	bool bNeedsResave = false;
	if (!CreateFile_Internal(NormalizedPath, RootDirectory, bNeedsResave))
	{
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

bool FsFilesystem::CreateFile_Internal(const FsPath& FileName, FsDirectoryDescriptor& CurrentDirectory, bool& bOutNeedsResave)
{
	const FsPath TopLevelPath = FileName.GetFirstPath();
	const FsPath SubPath = FileName.GetSubPath();

	// Check if the file already exists
	for (const FsFileDescriptor& SubDirectoryFile : CurrentDirectory.Files)
	{
		if (SubDirectoryFile.FileName != TopLevelPath)
		{
			continue;
		}

		if (!FileName.Contains("/"))
		{
			// Finished recursing, found a file with the same name
			FsLogger::LogFormat(FilesystemLogType::Error, "File %s already exists", FileName.GetData());
			return false;
		}
	}

	// The file does not exist in the current directory
	if (FileName.Contains("/"))
	{
		// We need to recurse into the next directory
		for (const FsFileDescriptor& SubDirectoryFile : CurrentDirectory.Files)
		{
			if (!SubDirectoryFile.bIsDirectory || SubDirectoryFile.FileName != TopLevelPath)
			{
				continue;
			}

			FsDirectoryDescriptor NextDirectory = ReadFileAsDirectory(SubDirectoryFile);

			bool bNeedsResave = false;
			if (!CreateFile_Internal(SubPath, NextDirectory, bNeedsResave))
			{
				return false;
			}

			if (bNeedsResave)
			{
				if (!SaveDirectory(NextDirectory, SubDirectoryFile.FileOffset))
				{
					FsLogger::LogFormat(FilesystemLogType::Error, "Failed to save directory %s", SubPath.GetData());
					return false;
				}
			}

			return true;
		}

		return false;
	}

	// The file does not exist in the current directory and we are at the end of the path
	FsFileDescriptor NewFile = FsFileDescriptor();
	NewFile.FileName = SubPath;
	NewFile.bIsDirectory = false;
	NewFile.FileSize = 0;

	// The file does not have any content yet, so we don't need to allocate any blocks for it.
	// When we write to the file, we will allocate blocks then.
	NewFile.FileOffset = 0;

	// Add the new file to the current directory and request a resave
	CurrentDirectory.Files.Add(NewFile);
	bOutNeedsResave = true;
	return true;
}

bool FsFilesystem::FileExists(const FsPath& InFileName)
{
	const FsPath NormalizedPath = InFileName.NormalizePath();

	if (!NormalizedPath.Contains("/"))
	{
		// Seeing if this file exists in the root directory
		for (const FsFileDescriptor& File : RootDirectory.Files)
		{
			if (File.FileName == NormalizedPath)
			{
				FsLogger::LogFormat(FilesystemLogType::Verbose, "File %s exists", NormalizedPath.GetData());
				return true;
			}
		}

		return false;
	}

	const FsPath DirectoryPath = NormalizedPath.GetPathWithoutFileName();
	FsDirectoryDescriptor Directory{};
	if (!GetDirectory(DirectoryPath, Directory))
	{
		FsLogger::LogFormat(FilesystemLogType::Error, "Failed to get directory for file %s", InFileName.GetData());
		return false;
	}

	const FsPath FileName = NormalizedPath.GetLastPath();
	for (const FsFileDescriptor& File : Directory.Files)
	{
		if (File.FileName == FileName)
		{
			FsLogger::LogFormat(FilesystemLogType::Verbose, "File %s exists", NormalizedPath.GetData());
			return true;
		}
	}

	return false;
}

bool FsFilesystem::WriteToFile(const FsPath& InPath, const uint8* Source, uint64 InOffset, uint64 InLength)
{
	const FsPath NormalizedPath = InPath.NormalizePath();

	if (!FileExists(NormalizedPath))
	{
		FsLogger::LogFormat(FilesystemLogType::Error, "File %s does not exist", NormalizedPath.GetData());
		return false;
	}

	// Get the files directory
	const FsPath DirectoryPath = NormalizedPath.GetPathWithoutFileName();
	FsDirectoryDescriptor Directory{};
	FsFileDescriptor DirectoryFile{};
	if (!GetDirectory(DirectoryPath, Directory, &DirectoryFile))
	{
		FsLogger::LogFormat(FilesystemLogType::Error, "Failed to get directory for file %s", NormalizedPath.GetData());
		return false;
	}

	const FsPath FileName = NormalizedPath.GetLastPath();
	for (FsFileDescriptor& File : Directory.Files)
	{
		if (File.FileName != FileName)
		{
			continue;
		}

		// Get all the chunks for the file
		FsArray<FsFileChunkHeader> AllChunks = GetAllChunksForFile(File);

		const uint64 MaxWriteLength = InOffset + InLength;
		const uint64 AllocatedSpace = GetAllocatedSpaceInFileChunks(AllChunks);

		if (MaxWriteLength > AllocatedSpace)
		{
			const uint64 ExtraSpaceNeeded = MaxWriteLength - AllocatedSpace;
			// We need to allocate more space for the file
			uint64 AdditionalBlocks = ExtraSpaceNeeded % BlockSize == 0 ? ExtraSpaceNeeded / BlockSize : ExtraSpaceNeeded / BlockSize + 1;

			// Consider that each block will have a chunk header, so allocate extra blocks to account for that.
			const uint64 ContentSize = BlockSize - sizeof(FsFileChunkHeader);
			while (AdditionalBlocks * ContentSize < MaxWriteLength)
			{
				AdditionalBlocks++;
			}

			FsBlockArray NewBlocks = GetFreeBlocks(AdditionalBlocks);
			if (NewBlocks.Length() != AdditionalBlocks)
			{
				FsLogger::LogFormat(FilesystemLogType::Error, "Failed to find %u free blocks for file %s", AdditionalBlocks, NormalizedPath.GetData());
				return false;
			}

			SetBlocksInUse(NewBlocks, true);

			if (AllChunks.IsEmpty())
			{
				// This file is empty and has no blocks allocated.
				// We need to adjust the file offset to the new blocks
				File.FileOffset = BlockIndexToAbsoluteOffset(NewBlocks[0]);
			}
			else
			{
				// Find the last chunk
				FsFileChunkHeader* LastChunk = nullptr;
				for (FsFileChunkHeader& Chunk : AllChunks)
				{
					if (Chunk.NextBlockIndex == 0)
					{
						LastChunk = &Chunk;
						break;
					}
				}

				if (LastChunk == nullptr)
				{
					FsLogger::LogFormat(FilesystemLogType::Error, "Failed to find last chunk for file %s", NormalizedPath.GetData());
					return false;
				}

				// Update the last chunk to point to the new blocks
				LastChunk->NextBlockIndex = NewBlocks[0];
			}

			// Create the new chunks
			for (uint64 i = 0; i < NewBlocks.Length(); i++)
			{
				FsFileChunkHeader NewChunk = FsFileChunkHeader();
				NewChunk.NextBlockIndex = i + 1 < NewBlocks.Length() ? NewBlocks[i + 1] : 0;
				NewChunk.Blocks = 1;
				AllChunks.Add(NewChunk);
			}
		}

		// Update the file size if we expanded the file
		if (MaxWriteLength > File.FileSize)
		{
			File.FileSize = MaxWriteLength;
		}

		// For each chunk, create a buffer the size of the chunk, read the chunk, update the buffer, write the chunk
		uint64 BytesWritten = 0;
		uint64 CurrentOffset = InOffset;
		uint64 CurrentAbsoluteOffset = File.FileOffset;
		for (const FsFileChunkHeader& Chunk : AllChunks)
		{
			// Just read and write one block at a time inside a chunk. The first block has the chunk header so need to skip the sizeof the chunk header.
			for (uint64 i = 0; i < Chunk.Blocks; i++)
			{
				const bool bHasChunkHeader = i == 0;
				const uint64 FirstChunkOffset = bHasChunkHeader ? sizeof(FsFileChunkHeader) : 0;
				const uint64 ReadableSize = BlockSize - FirstChunkOffset;
				const uint64 ReadOffset = CurrentAbsoluteOffset + FirstChunkOffset + (i * BlockSize);

				FsBitArray ChunkBuffer{};

				// serialize the chunk header
				if (bHasChunkHeader)
				{
					FsBitWriter ChunkWriter = FsBitWriter(ChunkBuffer);
					const_cast<FsFileChunkHeader&>(Chunk).Serialize(ChunkWriter);
					ChunkBuffer.AddZeroed(BlockSize - FirstChunkOffset);
				}
				else
				{
					ChunkBuffer.FillZeroed(BlockSize);
				}

				// Read the content part of the block
				const FilesystemReadResult ReadResult = Read(ReadOffset, ReadableSize, ChunkBuffer.GetInternalArray().GetData() + FirstChunkOffset);
				if (ReadResult != FilesystemReadResult::Success)
				{
					FsLogger::LogFormat(FilesystemLogType::Error, "Failed to read chunk for file %s", NormalizedPath.GetData());
					return false;
				}

				// Update the buffer
				for (uint64 j = 0; j < ReadableSize; j++)
				{
					ChunkBuffer.GetInternalArray().GetData()[j + FirstChunkOffset] = Source[BytesWritten];
					BytesWritten++;

					CurrentOffset++;
					if (BytesWritten >= InLength)
					{
						break;
					}

					if (CurrentOffset >= MaxWriteLength)
					{
						break;
					}
				}

				// Write the block
				const uint64 WriteOffset = CurrentAbsoluteOffset;
				const uint64 WriteSize = BlockSize;

				const FilesystemWriteResult WriteResult = Write(WriteOffset, WriteSize, ChunkBuffer.GetInternalArray().GetData());
				if (WriteResult != FilesystemWriteResult::Success)
				{
					FsLogger::LogFormat(FilesystemLogType::Error, "Failed to write chunk for file %s", NormalizedPath.GetData());
					return false;
				}

				// Log chunk header				
				//FsLogger::LogFormat(FilesystemLogType::Info, "Wrote chunk at %u with %u blocks pointing to next chunk at %u [%u]", WriteOffset, Chunk.Blocks, Chunk.NextBlockIndex, BlockIndexToAbsoluteOffset(Chunk.NextBlockIndex));
			}

			CurrentAbsoluteOffset = BlockIndexToAbsoluteOffset(Chunk.NextBlockIndex);
			if (CurrentAbsoluteOffset == 0 || BytesWritten >= InLength || CurrentOffset >= MaxWriteLength)
			{
				// done
				break;
			}
		}

		if (!SaveDirectory(Directory, DirectoryFile.FileOffset))
		{
			FsLogger::LogFormat(FilesystemLogType::Error, "Failed to save directory %s", DirectoryPath.GetData());
			return false;
		}
		return true;
	}

	FsLogger::LogFormat(FilesystemLogType::Error, "Failed to write to file %s", NormalizedPath.GetData());
	return false;
}

bool FsFilesystem::ReadFromFile(const FsPath& InPath, uint64 Offset, uint8* Destination, uint64 Length)
{
	const FsPath NormalizedPath = InPath.NormalizePath();

	if (!FileExists(NormalizedPath))
	{
		FsLogger::LogFormat(FilesystemLogType::Error, "File %s does not exist", NormalizedPath.GetData());
		return false;
	}

	// Get the files directory
	const FsPath DirectoryPath = NormalizedPath.GetPathWithoutFileName();
	FsDirectoryDescriptor Directory{};
	if (!GetDirectory(DirectoryPath, Directory))
	{
		FsLogger::LogFormat(FilesystemLogType::Error, "Failed to get directory for file %s", NormalizedPath.GetData());
		return false;
	}

	const FsPath FileName = NormalizedPath.GetLastPath();

	for (const FsFileDescriptor& File : Directory.Files)
	{
		if (File.FileName != FileName)
		{
			continue;
		}

		// Check the read is within the file length
		if (Offset + Length > File.FileSize)
		{
			FsLogger::LogFormat(FilesystemLogType::Error, "Read is out of bounds for file %s", NormalizedPath.GetData());
			return false;
		}

		// Get all the chunks for the file up to the read length
		const uint64 MaxReadLength = Offset + Length;
		const FsArray<FsFileChunkHeader> AllChunks = GetAllChunksForFile(File, &MaxReadLength);

		if (AllChunks.IsEmpty())
		{
			// This file is empty and has no blocks allocated for it.
			FsLogger::LogFormat(FilesystemLogType::Error, "File %s has no chunks allocated to it", NormalizedPath.GetData());
			return false;
		}

		// Read the file data from the blocks
		uint64 BytesRead = 0;
		uint64 CurrentOffset = Offset;
		uint64 CurrentAbsoluteOffset = File.FileOffset;
		uint64 CurrentChunkIndex = 0;

		while (BytesRead < Length && CurrentAbsoluteOffset != 0 && AllChunks.IsValidIndex(CurrentChunkIndex))
		{
			const FsFileChunkHeader& CurrentChunk = AllChunks[CurrentChunkIndex];

			// Log the chunk header
			//FsLogger::LogFormat(FilesystemLogType::Info, "Read chunk at %u with %u blocks pointing to next chunk at %u [%u]", CurrentAbsoluteOffset, CurrentChunk.Blocks, CurrentChunk.NextBlockIndex, BlockIndexToAbsoluteOffset(CurrentChunk.NextBlockIndex));

			CurrentChunkIndex++;
			
			const uint64 ChunkSize = CurrentChunk.Blocks * BlockSize;

			FsArray<uint8> ChunkBuffer = FsArray<uint8>();
			ChunkBuffer.FillZeroed(ChunkSize);

			// Read the whole chunk
			const FilesystemReadResult Result = Read(CurrentAbsoluteOffset, ChunkSize, ChunkBuffer.GetData());
			if (Result != FilesystemReadResult::Success)
			{
				FsLogger::LogFormat(FilesystemLogType::Error, "Failed to read chunk %u for file %s", CurrentChunkIndex, NormalizedPath.GetData());
				return false;
			}

			for (uint64 ChunkByteIndex = sizeof(FsFileChunkHeader); ChunkByteIndex < ChunkSize; ChunkByteIndex++)
			{
				if (CurrentOffset >= Offset)
				{
					Destination[BytesRead] = ChunkBuffer[ChunkByteIndex];
					BytesRead++;
				}

				CurrentOffset++;
				if (BytesRead >= Length)
				{
					break;
				}

				if (CurrentOffset >= MaxReadLength)
				{
					break;
				}
			}
			CurrentAbsoluteOffset = BlockIndexToAbsoluteOffset(CurrentChunk.NextBlockIndex);
		}
		fsCheck(BytesRead == Length, "Failed to read the correct amount of bytes from file %s", NormalizedPath.GetData());
		FsLogger::LogFormat(FilesystemLogType::Info, "Read %u bytes from file %s", BytesRead, NormalizedPath.GetData());
		return true;
	}

	// Could not find the file
	FsLogger::LogFormat(FilesystemLogType::Error, "Failed to read file %s", NormalizedPath.GetData());
	return false;
}

bool FsFilesystem::WriteEntireFile_Internal(FsFileDescriptor& FileDescriptor, const uint8* Source, uint64 Length)
{
	// Allocate enough blocks for the file, don't over allocate it
	const uint64 NumBlocks = Length % BlockSize == 0 ? (Length / BlockSize) : (Length / BlockSize) + 1;

	const FsBlockArray FileBlocks = GetFreeBlocks(NumBlocks);
	if (FileBlocks.Length() != NumBlocks)
	{
		FsLogger::LogFormat(FilesystemLogType::Error, "Failed to find %u free blocks for file %s", NumBlocks, FileDescriptor.FileName.GetData());
		return false;
	}

	FsLogger::LogFormat(FilesystemLogType::Verbose, "Allocating first block for new file at %u bytes", BlockIndexToAbsoluteOffset(FileBlocks[0]));

	SetBlocksInUse(FileBlocks, true);

	// Write the file data to the blocks (Don't forget their chunk headers)
	uint64 BytesWritten = 0;

	for (uint64 i = 0; i < NumBlocks; i++)
	{
		const uint64 BlockOffset = BlockIndexToAbsoluteOffset(FileBlocks[i]);
		
		FsFileChunkHeader ChunkHeader = FsFileChunkHeader();
		ChunkHeader.NextBlockIndex = i + 1 < NumBlocks ? FileBlocks[i + 1] : 0;
		ChunkHeader.Blocks = 1;

		FsBitArray BlockBuffer = FsBitArray();
		FsBitWriter BlockWriter = FsBitWriter(BlockBuffer);

		ChunkHeader.Serialize(BlockWriter);

		const uint64 WriteableSpace = BlockSize - BlockBuffer.ByteLength();
		const uint64 BytesToWrite = Length - BytesWritten > WriteableSpace ? WriteableSpace : Length - BytesWritten;

		BlockBuffer.AddZeroed(BytesToWrite);

		for (uint64 j = 0; j < BytesToWrite; j++)
		{
			BlockBuffer.GetInternalArray().GetData()[j + sizeof(FsFileChunkHeader)] = Source[BytesWritten + j];
		}

		const FilesystemWriteResult WriteResult = Write(BlockOffset, BlockBuffer.ByteLength(), BlockBuffer.GetInternalArray().GetData());
		if (WriteResult != FilesystemWriteResult::Success)
		{
			FsLogger::LogFormat(FilesystemLogType::Error, "Failed to write block %u for file %s", FileBlocks[i], FileDescriptor.FileName.GetData());
			return false;
		}

		BytesWritten += BytesToWrite;
	}

	FileDescriptor.FileOffset = BlockIndexToAbsoluteOffset(FileBlocks[0]);
	FileDescriptor.FileSize = Length;

	FsLogger::LogFormat(FilesystemLogType::Verbose, "Wrote entire file %s with %u bytes", FileDescriptor.FileName.GetData(), Length);

	return true;
}

FsArray<FsFileChunkHeader> FsFilesystem::GetAllChunksForFile(const FsFileDescriptor& FileDescriptor, const uint64* OptionalFileLength)
{
	FsArray<FsFileChunkHeader> AllBlocks{};

	if (FileDescriptor.FileOffset == 0)
	{
		// This file is empty and has no blocks allocated for it.
		return AllBlocks;
	}

	// Read the first block of the file
	const uint64 ReadOffset = FileDescriptor.FileOffset;
	const uint64 ReadLength = sizeof(FsFileChunkHeader);

	FsBitArray FileBuffer = FsBitArray();
	FileBuffer.FillZeroed(ReadLength);

	const FilesystemReadResult ReadResult = Read(ReadOffset, ReadLength, FileBuffer.GetInternalArray().GetData());
	if (ReadResult != FilesystemReadResult::Success)
	{
		FsLogger::LogFormat(FilesystemLogType::Error, "Failed to read file %s", FileDescriptor.FileName.GetData());
		return AllBlocks;
	}

	FsBitReader FileReader = FsBitReader(FileBuffer);

	FsFileChunkHeader ChunkHeader = FsFileChunkHeader();
	ChunkHeader.Serialize(FileReader);

	// log chunk header
	//FsLogger::LogFormat(FilesystemLogType::Info, "Read chunk at %u with %u blocks pointing to next chunk at %u [%u]", ReadOffset, ChunkHeader.Blocks, ChunkHeader.NextBlockIndex, BlockIndexToAbsoluteOffset(ChunkHeader.NextBlockIndex));

	// Read the blocks from the chunk header
	AllBlocks.Add(ChunkHeader);

	if (OptionalFileLength && *OptionalFileLength < BlockSize)
	{
		// We only need the first block
		return AllBlocks;
	}

	uint64 NextBlockIndex = ChunkHeader.NextBlockIndex;
	uint64 CurrentBlockLength = BlockSize;
	while (NextBlockIndex != 0 && (!OptionalFileLength || CurrentBlockLength < *OptionalFileLength))
	{
		const uint64 NextBlockOffset = BlockIndexToAbsoluteOffset(NextBlockIndex);

		FsBitArray NextBlockBuffer = FsBitArray();
		NextBlockBuffer.FillZeroed(ReadLength);

		const FilesystemReadResult NextBlockReadResult = Read(NextBlockOffset, ReadLength, NextBlockBuffer.GetInternalArray().GetData());
		if (NextBlockReadResult != FilesystemReadResult::Success)
		{
			FsLogger::LogFormat(FilesystemLogType::Error, "Failed to read file %s", FileDescriptor.FileName.GetData());
			return AllBlocks;
		}

		FsBitReader NextBlockReader = FsBitReader(NextBlockBuffer);

		FsFileChunkHeader NextChunkHeader = FsFileChunkHeader();
		NextChunkHeader.Serialize(NextBlockReader);

		// Log chunk header
		//FsLogger::LogFormat(FilesystemLogType::Info, "Read chunk at %u with %u blocks pointing to next chunk at %u [%u]", NextBlockOffset, NextChunkHeader.Blocks, NextChunkHeader.NextBlockIndex, BlockIndexToAbsoluteOffset(NextChunkHeader.NextBlockIndex));

		AllBlocks.Add(NextChunkHeader);
		NextBlockIndex = NextChunkHeader.NextBlockIndex;

		const uint64 ContentSize = BlockSize - sizeof(FsFileChunkHeader);
		CurrentBlockLength += ContentSize;
	}

	return AllBlocks;

}

uint64 FsFilesystem::GetFreeAllocatedSpaceInFileChunks(const FsFileDescriptor& FileDescriptor, const FsArray<FsFileChunkHeader>* ChunksToUse)
{
	FsArray<FsFileChunkHeader> AllChunks{};
	if (!ChunksToUse)
	{
		AllChunks = GetAllChunksForFile(FileDescriptor);
		ChunksToUse = &AllChunks;
	}

	if (ChunksToUse->IsEmpty())
	{
		fsCheck(FileDescriptor.FileSize == 0 && FileDescriptor.FileOffset == 0, "File has no chunks allocated to it but has either a file size or file offset.");
		// This file is empty and has no blocks allocated for it.
		return 0;
	}

	uint64 AllocatedSpace = 0;
	for (const FsFileChunkHeader& Chunk : *ChunksToUse)
	{
		AllocatedSpace += Chunk.Blocks * BlockSize;
	}

	return AllocatedSpace - FileDescriptor.FileSize;
}

uint64 FsFilesystem::GetAllocatedSpaceInFileChunks(const FsArray<FsFileChunkHeader>& InChunks)
{
	uint64 AllocatedSpace = 0;
	for (const FsFileChunkHeader& Chunk : InChunks)
	{
		AllocatedSpace += Chunk.Blocks * BlockSize;
	}
	return AllocatedSpace;
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

void FsFilesystem::SetBlocksInUse(const FsBlockArray& BlockIndices, bool bInUse)
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

bool FsFilesystem::GetDirectory(const FsPath& InDirectoryName, FsDirectoryDescriptor& OutDirectoryDescriptor, FsFileDescriptor* OutDirectoryFile)
{
	if (InDirectoryName.Contains("."))
	{
		FsLogger::LogFormat(FilesystemLogType::Error, "Cannot get directory with a file extension: %s", InDirectoryName.GetData());
		return false;
	}

	FsPath NormalizedPath = InDirectoryName.NormalizePath();
	FsLogger::LogFormat(FilesystemLogType::Verbose, "Getting directory for %s", NormalizedPath.GetData());

	return GetDirectory_Internal(NormalizedPath, RootDirectory, OutDirectoryDescriptor, OutDirectoryFile);
}

bool FsFilesystem::GetDirectory_Internal(const FsPath& DirectoryPath, const FsDirectoryDescriptor& CurrentDirectory, FsDirectoryDescriptor& OutDirectory, FsFileDescriptor* OutDirectoryFile)
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
			if (OutDirectoryFile)
			{
				*OutDirectoryFile = SubDirectoryFile;
			}
			return true;
		}

		// Load the found directory
		const FsDirectoryDescriptor NextDirectory = ReadFileAsDirectory(SubDirectoryFile);

		// Recurse into the next directory
		const bool bSubPathResult = GetDirectory_Internal(SubDirectory, NextDirectory, OutDirectory, OutDirectoryFile);
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

		if (!DirectoryName.Contains("/"))
		{
			// Finished recursing, found a directory with the same name
			return false;
		}

		// Load the found directory
		FsDirectoryDescriptor NextDirectory = ReadFileAsDirectory(SubDirectoryFile);

		// Recurse into the next directory
		bool bNeedsResave = false;
		if (!CreateDirectory_Internal(SubDirectory, NextDirectory, bNeedsResave))
		{
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

		return true;
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
	FsLogger::LogFormat(FilesystemLogType::Verbose, "Saving directory at %u bytes", AbsoluteOffset);

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

void FsFilesystem::LogAllFiles()
{
	for (const FsFileDescriptor& File : RootDirectory.Files)
	{
		FsLogger::LogFormat(FilesystemLogType::Info, "%s", File.FileName.GetData());
		
		// Load directory
		if (File.bIsDirectory)
		{
			const FsDirectoryDescriptor Directory = ReadFileAsDirectory(File);
			LogAllFiles_Internal(Directory, 1);
		}
	}
}

void FsFilesystem::LogAllFiles_Internal(const FsDirectoryDescriptor& CurrentDirectory, uint64 Depth)
{
	for (const FsFileDescriptor& File : CurrentDirectory.Files)
	{
		FsString Indent = "  ";
		for (uint64 i = 0; i < Depth; i++)
		{
			if (i == Depth - 1)
			{
				Indent.Append("|--");
				continue;
			}
			Indent.Append("  ");
		}

		FsLogger::LogFormat(FilesystemLogType::Info, "%s%s", Indent.GetData(), File.FileName.GetData());

		// Load directory
		if (File.bIsDirectory)
		{
			const FsDirectoryDescriptor Directory = ReadFileAsDirectory(File);
			LogAllFiles_Internal(Directory, Depth + 1);
		}
	}
}

bool FsFilesystem::DeleteDirectory(const FsPath& DirectoryName)
{
	return false;
}

bool FsFilesystem::DeleteFile(const FsPath& FileName)
{
	return false;
}

bool FsFilesystem::MoveFile(const FsPath& SourceFileName, const FsPath& DestinationFileName)
{
	return false;
}

bool FsFilesystem::CopyFile(const FsPath& SourceFileName, const FsPath& DestinationFileName)
{
	return false;
}