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

	while (Result.StartsWith(FsString("/")))
	{
		Result = Result.GetSubPath();
	}

	// Make lower case
	//Result = Result.ToLower<FsPath>();

	return Result;
}

FsPath FsPath::GetPathWithoutFileName() const
{
	if (!Contains("/"))
	{
		return FsPath(); // Empty path
	}

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
			FsLogger::LogFormat(FilesystemLogType::Verbose, "File %s already exists", FileName.GetData());
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

bool FsFilesystem::GetFileSize(const FsPath& InFileName, uint64& OutFileSize)
{
	FsFileDescriptor File{};
	if (!GetFile(InFileName, File))
	{
		return false;
	}
	OutFileSize = File.FileSize;
	return true;
}

bool FsFilesystem::FileExists(const FsPath& InFileName)
{
	FsFileDescriptor File{};
	return GetFile(InFileName, File);
}

bool FsFilesystem::GetFile(const FsPath& InFileName, FsFileDescriptor& OutFileDescriptor)
{
	FsPath NormalizedPath = InFileName.NormalizePath();

	if (!NormalizedPath.Contains("/"))
	{
		// Seeing if this file exists in the root directory
		for (const FsFileDescriptor& File : RootDirectory.Files)
		{
			if (!File.bIsDirectory && File.FileName == NormalizedPath)
			{
				FsLogger::LogFormat(FilesystemLogType::Verbose, "File %s exists", NormalizedPath.GetData());
				OutFileDescriptor = File;
				return true;
			}
		}

		return false;
	}

	const FsPath DirectoryPath = NormalizedPath.GetPathWithoutFileName();
	FsDirectoryDescriptor Directory{};
	if (!GetDirectory(DirectoryPath, Directory))
	{
		return false;
	}

	const FsPath FileName = NormalizedPath.GetLastPath();
	for (const FsFileDescriptor& File : Directory.Files)
	{
		if (!File.bIsDirectory && File.FileName == FileName)
		{
			FsLogger::LogFormat(FilesystemLogType::Verbose, "File %s exists", NormalizedPath.GetData());
			OutFileDescriptor = File;
			return true;
		}
	}

	return false;
}

void FsFilesystem::ValidateFileWrite(const FsPath& InPath, const uint8* Source, uint64 InOffset, uint64 InLength)
{
	const FsPath NormalizedPath = InPath.NormalizePath();

	if (!FileExists(NormalizedPath))
	{
		FsLogger::LogFormat(FilesystemLogType::Error, "ValidateFileWrite: File %s does not exist", NormalizedPath.GetData());
		fsCheck(false, "oh no");
		return;
	}

	// Read from the file into a temporary buffer and make sure the data is correct
	FsArray<uint8> ReadBuffer = FsArray<uint8>();
	ReadBuffer.FillUninitialized(InLength);

	uint64 BytesRead = 0;
	if (!ReadFromFile(NormalizedPath, InOffset, ReadBuffer.GetData(), InLength, &BytesRead))
	{
		FsLogger::LogFormat(FilesystemLogType::Error, "ValidateFileWrite: Failed to read file %s", NormalizedPath.GetData());
		fsCheck(false, "oh no");
		return;
	}

	if (BytesRead != InLength)
	{
		FsLogger::LogFormat(FilesystemLogType::Error, "ValidateFileWrite: Failed to read the correct amount of bytes from file %s", NormalizedPath.GetData());
		fsCheck(false, "oh no");
		return;
	}

	for (uint64 i = 0; i < InLength; i++)
	{
		if (ReadBuffer[i] != Source[i])
		{
			FsLogger::LogFormat(FilesystemLogType::Error, "ValidateFileWrite: File %s has incorrect data at byte %u", NormalizedPath.GetData(), i);
			fsCheck(false, "oh no");
			return;
		}
	}

	FsLogger::LogFormat(FilesystemLogType::Info, "Validated write on file %s", InPath.GetData());
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
		FsArray<FsFileChunkHeader> AllChunks = GetAllChunksForFile(NormalizedPath ,File);
		ClearCachedChunks(InPath);

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

			FsLogger::LogFormat(FilesystemLogType::Warning, "Allocating %u blocks for file %s", AdditionalBlocks, NormalizedPath.GetData());

			if (AllChunks.IsEmpty())
			{
				// This file is empty and has no blocks allocated.
				// We need to adjust the file offset to the new blocks
				File.FileOffset = BlockIndexToAbsoluteOffset(NewBlocks[0]);
			}
			else
			{
				// Update the last chunk to point to the new blocks
				FsFileChunkHeader& LastChunk = AllChunks[AllChunks.Length() - 1];
				LastChunk.NextBlockIndex = NewBlocks[0];

				// Save the last chunk
				const uint64 LastChunkOffset = AllChunks.Length() > 1 ? BlockIndexToAbsoluteOffset(AllChunks[AllChunks.Length() - 2].NextBlockIndex) : File.FileOffset;
				FsBitArray LastChunkBuffer = FsBitArray();
				FsBitWriter LastChunkWriter = FsBitWriter(LastChunkBuffer);
				const_cast<FsFileChunkHeader&>(LastChunk).Serialize(LastChunkWriter);

				const FilesystemWriteResult WriteResult = Write(LastChunkOffset, sizeof(FsFileChunkHeader), LastChunkBuffer.GetInternalArray().GetData());
				if (WriteResult != FilesystemWriteResult::Success)
				{
					FsLogger::LogFormat(FilesystemLogType::Error, "Failed to write chunk for file %s", NormalizedPath.GetData());
					return false;
				}
			}

			const uint64 PreviousChunksLength = AllChunks.Length();

			// Create the new chunk headers
			for (uint64 i = 0; i < NewBlocks.Length(); i++)
			{
				FsFileChunkHeader NewChunk = FsFileChunkHeader();
				NewChunk.NextBlockIndex = i + 1 < NewBlocks.Length() ? NewBlocks[i + 1] : 0;
				NewChunk.Blocks = 1;
				AllChunks.Add(NewChunk);
			}

			// Refresh the cache
			CacheChunks(NormalizedPath, AllChunks);
		}

		// Update the file size if we expanded the file
		if (MaxWriteLength > File.FileSize)
		{
			File.FileSize = MaxWriteLength;
		}

		// For each chunk, create a buffer the size of the chunk, read the chunk, update the buffer, write the chunk
		uint64 BytesWritten = 0;
		uint64 CurrentOffset = 0;
		uint64 CurrentAbsoluteOffset = File.FileOffset;
		for (const FsFileChunkHeader& Chunk : AllChunks)
		{
			const uint64 ChunkSize = Chunk.Blocks * BlockSize;
			const uint64 ChunkHeaderOffset = CurrentAbsoluteOffset;
			const uint64 ChunkHeaderLength = sizeof(FsFileChunkHeader);
			const uint64 ChunkContentLength = ChunkSize - ChunkHeaderLength;
			const uint64 ChunkContentOffset = CurrentAbsoluteOffset + ChunkHeaderLength;

			if (CurrentOffset + ChunkSize < InOffset)
			{
				// Skip this chunk
				CurrentOffset += ChunkContentLength;
				CurrentAbsoluteOffset = BlockIndexToAbsoluteOffset(Chunk.NextBlockIndex);
				continue;
			}

			ClearCachedRead(AbsoluteOffsetToBlockIndex(CurrentAbsoluteOffset));

			if (!Source)
			{
				// We have no source data, so we are just allocating space for the file. Write the chunk headers only.

				FsBitArray ChunkHeaderBuffer = FsBitArray();
				FsBitWriter ChunkHeaderWriter = FsBitWriter(ChunkHeaderBuffer);
				const_cast<FsFileChunkHeader&>(Chunk).Serialize(ChunkHeaderWriter);

				// Write the updated buffer back to the chunk
				const FilesystemWriteResult WriteResult = Write(CurrentAbsoluteOffset, ChunkHeaderLength, ChunkHeaderBuffer.GetInternalArray().GetData());
				if (WriteResult != FilesystemWriteResult::Success)
				{
					FsLogger::LogFormat(FilesystemLogType::Error, "Failed to write chunk for file %s", NormalizedPath.GetData());
					return false;
				}

				CurrentOffset += ChunkContentLength;
				BytesWritten += ChunkContentLength; 
			}
			else
			{
				FsArray<uint8> ChunkReadBuffer = FsArray<uint8>();
				ChunkReadBuffer.FillUninitialized(ChunkSize);

				// Read the whole content portion of the chunk
				const FilesystemReadResult Result = Read(ChunkContentOffset, ChunkContentLength, ChunkReadBuffer.GetData() + ChunkHeaderLength);
				if (Result != FilesystemReadResult::Success)
				{
					FsLogger::LogFormat(FilesystemLogType::Error, "Failed to read chunk for file %s", NormalizedPath.GetData());
					return false;
				}

				// Update the buffer with the new data
				for (uint64 ChunkByteIndex = sizeof(FsFileChunkHeader); ChunkByteIndex < ChunkSize; ChunkByteIndex++)
				{
					if (CurrentOffset < InOffset)
					{
						CurrentOffset++;
						continue;
					}
				
					ChunkReadBuffer[ChunkByteIndex] = Source[BytesWritten];
					BytesWritten++;
					CurrentOffset++;

					if (BytesWritten >= InLength)
					{
						break;
					}
				}

				// Serialize the chunk header and copy it in
				FsBitArray ChunkHeaderBuffer = FsBitArray();
				FsBitWriter ChunkHeaderWriter = FsBitWriter(ChunkHeaderBuffer);
				const_cast<FsFileChunkHeader&>(Chunk).Serialize(ChunkHeaderWriter);

				for (uint64 i = 0; i < sizeof(FsFileChunkHeader); i++)
				{
					ChunkReadBuffer[i] = ChunkHeaderBuffer.GetInternalArray()[i];
				}

				// Write the updated buffer back to the chunk
				const FilesystemWriteResult WriteResult = Write(CurrentAbsoluteOffset, ChunkSize, ChunkReadBuffer.GetData());
				if (WriteResult != FilesystemWriteResult::Success)
				{
					FsLogger::LogFormat(FilesystemLogType::Error, "Failed to write chunk for file %s", NormalizedPath.GetData());
					return false;
				}
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

		FsLogger::LogFormat(FilesystemLogType::Info, "Wrote to file %s with %u bytes. %u chunks total", NormalizedPath.GetData(), InLength, AllChunks.Length());

		// Clear the cache for this file
		const uint64 LoadedChunks = GetAllChunksForFile(NormalizedPath, File).Length();

		if (LoadedChunks != AllChunks.Length())
		{
			FsLogger::LogFormat(FilesystemLogType::Error, "Failed to write the correct amount of chunkies. %u have, %u expected", LoadedChunks, AllChunks.Length());
		}

		fsCheck(LoadedChunks == AllChunks.Length(), "Failed to write the correct amount of chunkies");

		if (Source)
		{
			ValidateFileWrite(NormalizedPath, Source, InOffset, InLength);
		}
		return true;
	}

	FsLogger::LogFormat(FilesystemLogType::Error, "Failed to write to file %s", NormalizedPath.GetData());
	return false;
}

bool FsFilesystem::ReadFromFile(const FsPath& InPath, uint64 Offset, uint8* Destination, uint64 Length, uint64* OutBytesRead)
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
			Length = File.FileSize - Offset;
		}

		if (Offset + Length > File.FileSize)
		{
			FsLogger::LogFormat(FilesystemLogType::Error, "Read is out of bounds for file %s", NormalizedPath.GetData());
			return false;
		}

		// Get all the chunks for the file up to the read length
		const uint64 MaxReadLength = Offset + Length;
		const FsArray<FsFileChunkHeader> AllChunks = GetAllChunksForFile(NormalizedPath, File);

		if (AllChunks.IsEmpty())
		{
			// This file is empty and has no blocks allocated for it.
			FsLogger::LogFormat(FilesystemLogType::Error, "File %s has no chunks allocated to it", NormalizedPath.GetData());
			return false;
		}

		//FsLogger::LogFormat(FilesystemLogType::Info, "Reading file %s with %u chunks", NormalizedPath.GetData(), AllChunks.Length());

		// Read the file data from the blocks
		uint64 BytesRead = 0;
		uint64 CurrentOffset = 0;
		uint64 CurrentAbsoluteOffset = File.FileOffset;
		uint64 CurrentChunkIndex = 0;

		while (BytesRead < Length && CurrentAbsoluteOffset != 0 && AllChunks.IsValidIndex(CurrentChunkIndex))
		{
			const FsFileChunkHeader& CurrentChunk = AllChunks[CurrentChunkIndex];
			CurrentChunkIndex++;

			const uint64 ChunkSize = CurrentChunk.Blocks * BlockSize;
			
			// See if we can skip this chunk
			if (CurrentOffset + ChunkSize < Offset)
			{
				CurrentOffset += ChunkSize - sizeof(FsFileChunkHeader);
				CurrentAbsoluteOffset = BlockIndexToAbsoluteOffset(CurrentChunk.NextBlockIndex);
				continue;
			}

			FsArray<uint8> ChunkBuffer = FsArray<uint8>();
			FsArray<uint8>* ChunkBufferPtr = GetCachedRead(AbsoluteOffsetToBlockIndex(CurrentAbsoluteOffset));
			if (!ChunkBufferPtr)
			{
				//ChunkBufferPtr = CacheRead(AbsoluteOffsetToBlockIndex(CurrentAbsoluteOffset));
				ChunkBufferPtr = &ChunkBuffer;

				ChunkBufferPtr->Empty(false);
				ChunkBufferPtr->FillUninitialized(ChunkSize);

				// Read the whole chunk
				const FilesystemReadResult Result = Read(CurrentAbsoluteOffset, ChunkSize, ChunkBufferPtr->GetData());
				if (Result != FilesystemReadResult::Success)
				{
					FsLogger::LogFormat(FilesystemLogType::Error, "Failed to read chunk %u for file %s", CurrentChunkIndex - 1, NormalizedPath.GetData());
					return false;
				}

				FsLogger::LogFormat(FilesystemLogType::Info, "Read chunk %u (size %u) for file %s", CurrentChunkIndex - 1, ChunkSize, NormalizedPath.GetData());
			}
			else
			{
				FsLogger::LogFormat(FilesystemLogType::Info, "Using cached chunk %u (size %u) for file %s", CurrentChunkIndex - 1, ChunkBufferPtr->Length(), NormalizedPath.GetData());
			}

			for (uint64 ChunkByteIndex = sizeof(FsFileChunkHeader); ChunkByteIndex < ChunkSize; ChunkByteIndex++)
			{
				if (CurrentOffset < Offset)
				{
					CurrentOffset ++;
					continue;
				}

				if (CurrentOffset >= Offset)
				{
					Destination[BytesRead] = (*ChunkBufferPtr)[ChunkByteIndex];
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
		fsCheck(BytesRead == Length, "Failed to read the correct amount of bytes from file");
		if (OutBytesRead)
		{
			*OutBytesRead = BytesRead;
		}
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

void FsFilesystem::CacheChunks(const FsPath& FileName, const FsArray<FsFileChunkHeader>& Chunks)
{
	ClearCachedChunks(FileName);

	FsCachedChunkList NewCachedChunks = FsCachedChunkList();
	NewCachedChunks.FileName = FileName;
	NewCachedChunks.Chunks = Chunks;

	CachedChunks.Add(NewCachedChunks);
}

void FsFilesystem::ClearCachedChunks(const FsPath& FileName)
{
	for (uint64 i = 0; i < CachedChunks.Length(); i++)
	{
		if (CachedChunks[i].FileName == FileName)
		{
			CachedChunks.RemoveAt(i);
			return;
		}
	}
}

bool FsFilesystem::GetCachedChunks(const FsPath& FileName, FsArray<FsFileChunkHeader>& OutChunks)
{
	for (const FsCachedChunkList& CachedChunks : CachedChunks)
	{
		if (CachedChunks.FileName == FileName)
		{
			OutChunks = CachedChunks.Chunks;
			return true;
		}
	}

	return false;
}

FsArray<FsFileChunkHeader> FsFilesystem::GetAllChunksForFile(const FsPath& InPath, const FsFileDescriptor& FileDescriptor, const uint64* OptionalFileLength)
{
	FsArray<FsFileChunkHeader> AllBlocks{};
	if (!OptionalFileLength && GetCachedChunks(InPath, AllBlocks))
	{
		//FsLogger::LogFormat(FilesystemLogType::Info, "Found cached chunks for file %s", InPath.GetData());
		return AllBlocks;
	}

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
		CacheChunks(InPath, AllBlocks);
		return AllBlocks;
	}

	FsBitReader FileReader = FsBitReader(FileBuffer);

	FsFileChunkHeader ChunkHeader = FsFileChunkHeader();
	ChunkHeader.Serialize(FileReader);

	// log chunk header
	//FsLogger::LogFormat(FilesystemLogType::Info, "[%u] Read chunk at %u with %u blocks pointing to next chunk at %u [%u]", 0, ReadOffset, ChunkHeader.Blocks, ChunkHeader.NextBlockIndex, BlockIndexToAbsoluteOffset(ChunkHeader.NextBlockIndex));

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
		//FsLogger::LogFormat(FilesystemLogType::Info, "[%u] Read chunk at %u with %u blocks pointing to next chunk at %u [%u]", AllBlocks.Length(), NextBlockOffset, NextChunkHeader.Blocks, NextChunkHeader.NextBlockIndex, BlockIndexToAbsoluteOffset(NextChunkHeader.NextBlockIndex));

		AllBlocks.Add(NextChunkHeader);
		NextBlockIndex = NextChunkHeader.NextBlockIndex;

		const uint64 ContentSize = BlockSize - sizeof(FsFileChunkHeader);
		CurrentBlockLength += ContentSize;
	}

	CacheChunks(InPath, AllBlocks);
	return AllBlocks;

}

uint64 FsFilesystem::GetFreeAllocatedSpaceInFileChunks(const FsPath& InPath, const FsFileDescriptor& FileDescriptor, const FsArray<FsFileChunkHeader>* ChunksToUse)
{
	FsArray<FsFileChunkHeader> AllChunks{};
	if (!ChunksToUse)
	{
		AllChunks = GetAllChunksForFile(InPath, FileDescriptor);
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
	RootDirectory.bDirectoryIsRoot = true;
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
		FilesystemHeader.RootDirectory.bDirectoryIsRoot = true;
		RootDirectory.bDirectoryIsRoot = true;

		ClearBlockBuffer();

		FsFileDescriptor RootDirectoryFile;
		RootDirectoryFile.FileName = "Root";
		RootDirectoryFile.bIsDirectory = true;

		// find a block for the root directory
		const FsBlockArray RootDirectoryBlocks = GetFreeBlocks(1);
		if (RootDirectoryBlocks.Length() == 0)
		{
			FsLogger::LogFormat(FilesystemLogType::Error, "Failed to find a block for the root directory");
			return;
		}

		SetBlocksInUse(RootDirectoryBlocks, true);

		const uint64 AbsoluteOffset = BlockIndexToAbsoluteOffset(RootDirectoryBlocks[0]);

		RootDirectoryFile.FileOffset = AbsoluteOffset;
		RootDirectoryFile.FileSize = 0;

		SaveFilesystemHeader(FilesystemHeader);

		FsLogger::LogFormat(FilesystemLogType::Verbose, "Filesystem header created successfully. Root directory located at %u bytes.", RootDirectoryFile.FileOffset);
		return;
	}

	RootDirectory = FilesystemHeader.RootDirectory;
	RootDirectory.bDirectoryIsRoot = true;
	FsLogger::LogFormat(FilesystemLogType::Verbose, "Filesystem header loaded successfully");
}

bool FsFilesystem::GetUsedBlocksCount(uint64& OutUsedBlocks)
{
	const FsBitArray BlockBuffer = ReadBlockBuffer();
	OutUsedBlocks = 0;
	for (uint64 i = 0; i < BlockBuffer.BitLength(); i++)
	{
		if (BlockBuffer.GetBit(i))
		{
			OutUsedBlocks++;
		}
	}

	return true;
}

void FsFilesystem::SetBlocksInUse(const FsBlockArray& BlockIndices, bool bInUse)
{
	fsCheck(BlockIndices.Length() > 0, "BlockIndices must have at least one element");

	uint64 UsedBlocks = 0;
	if (!GetUsedBlocksCount(UsedBlocks))
	{
		FsLogger::LogFormat(FilesystemLogType::Error, "Failed to get UsedBlocks");
		return;
	}

	const uint64 ExpectedUsedBlocks = bInUse ? UsedBlocks + BlockIndices.Length() : UsedBlocks - BlockIndices.Length();

	/*for (uint64 BlockIndex : BlockIndices)
	{
		FsLogger::LogFormat(FilesystemLogType::Verbose, "Setting block %u at %u in use: %s", BlockIndex, BlockIndexToAbsoluteOffset(BlockIndex), bInUse ? "true" : "false");
	}*/

	// Load the existing block buffer
	FsBitArray BlockBuffer = ReadBlockBuffer();

	for (uint64 BlockIndex : BlockIndices)
	{
		// Check the block is not what we are setting it to
		if (BlockBuffer.GetBit(BlockIndex) == bInUse)
		{
			FsLogger::LogFormat(FilesystemLogType::Warning, "Block %u is already %s", BlockIndex, bInUse ? "in use" : "free");
			continue;
		}

		// Set the block in use
		BlockBuffer.SetBit(BlockIndex, bInUse);
		fsCheck(BlockBuffer.GetBit(BlockIndex) == bInUse, "Failed to set block in use");

		ClearCachedRead(BlockIndex);
	}

	// Write the block back
	const FilesystemWriteResult WriteResult = Write(GetBlockBufferOffset(), GetBlockBufferSizeBytes(), BlockBuffer.GetInternalArray().GetData());
	if (WriteResult != FilesystemWriteResult::Success)
	{
		FsLogger::LogFormat(FilesystemLogType::Error, "Failed to write block buffer. Ensure `Write` is implemented correctly.");
	}

	if (!GetUsedBlocksCount(UsedBlocks))
	{
		FsLogger::LogFormat(FilesystemLogType::Error, "Failed to get UsedBlocks");
		return;
	}

	if (UsedBlocks != ExpectedUsedBlocks)
	{
		FsLogger::LogFormat(FilesystemLogType::Error, "Failed to correctly set blocks in use. Expected %u used blocks, got %u used blocks", ExpectedUsedBlocks, UsedBlocks);
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
	FsBitArray BlockBuffer = ReadBlockBuffer();
	
	// Calculate the minimum block index that we should skip to avoid the block buffer.
	const uint64 MinBlockIndex = GetContentStartOffset() / BlockSize;

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
	//if (InDirectoryName.Contains("."))
	{
		//return false;
	}

	FsPath NormalizedPath = InDirectoryName.NormalizePath();

	FsLogger::LogFormat(FilesystemLogType::Verbose, "Getting directory for %s", NormalizedPath.GetData());
	if (NormalizedPath.IsEmpty() || NormalizedPath == FsPath("/"))
	{
		// Root directory
		OutDirectoryDescriptor = RootDirectory;
		fsCheck(OutDirectoryDescriptor.bDirectoryIsRoot, "Root is not root");
		return true;
	}


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
			return false;
		}

		return bSubPathResult;
	}

	return false;
}

bool FsFilesystem::CreateDirectory(const FsPath& InDirectoryName)
{
	//if (InDirectoryName.Contains("."))
	{
		//FsLogger::LogFormat(FilesystemLogType::Error, "Cannot create directory with a file extension: %s", InDirectoryName.GetData());
		//return false;
	}

	FsPath NormalizedPath = InDirectoryName.NormalizePath();
	FsLogger::LogFormat(FilesystemLogType::Verbose, "Creating directory for %s", NormalizedPath.GetData());

	bool bNeedsResave = false;
	if (!CreateDirectory_Internal(NormalizedPath, RootDirectory, bNeedsResave))
	{
		FsLogger::LogFormat(FilesystemLogType::Verbose, "Failed to create directory %s", NormalizedPath.GetData());
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
	if (Directory.bDirectoryIsRoot)
	{
		FsLogger::LogFormat(FilesystemLogType::Verbose, "Saving root directory");
		FsFilesystemHeader Header = FsFilesystemHeader();
		Header.RootDirectory = Directory;
		SaveFilesystemHeader(Header);
		RootDirectory = Directory; // Update the root directory
		return true;
	}

	ClearCachedDirectory(AbsoluteOffset);
	CacheDirectory(AbsoluteOffset, Directory);

	FsLogger::LogFormat(FilesystemLogType::Verbose, "Saving directory at %u bytes", AbsoluteOffset);

	// Resave the current directory
	FsBitArray NewDirectoryBuffer = FsBitArray();
	FsBitWriter NewDirectoryWriter = FsBitWriter(NewDirectoryBuffer);

	// Dont forget the chunk header
	FsFileChunkHeader ChunkHeader = FsFileChunkHeader();
	ChunkHeader.NextBlockIndex = 0;
	ChunkHeader.Blocks = 1;

	ChunkHeader.Serialize(NewDirectoryWriter);

	// Allocate space for the directory size
	uint64 Zero = 0;
	NewDirectoryWriter << Zero;

	const_cast<FsDirectoryDescriptor&>(Directory).Serialize(NewDirectoryWriter);

	// Update the directory size
	*reinterpret_cast<uint64*>(&NewDirectoryBuffer.GetInternalArray()[sizeof(FsFileChunkHeader)]) = NewDirectoryBuffer.ByteLength() - sizeof(FsFileChunkHeader);

	if (!WriteSingleChunk(NewDirectoryBuffer, AbsoluteOffset))
	{
		FsLogger::LogFormat(FilesystemLogType::Error, "Failed to write new directory");
		return false;
	}

	return true;
}

FsDirectoryDescriptor FsFilesystem::ReadFileAsDirectory(const FsFileDescriptor& FileDescriptor)
{
	FsDirectoryDescriptor DirectoryDescriptor;
	if (GetCachedDirectory(FileDescriptor.FileOffset, DirectoryDescriptor))
	{
		return DirectoryDescriptor;
	}

	// Read the first block of the file
	const uint64 ReadOffset = FileDescriptor.FileOffset;
	const uint64 DirectoryChunkHeaderSize = sizeof(FsFileChunkHeader) + sizeof(uint64);

	FsBitArray FileBuffer = FsBitArray();
	FileBuffer.FillUninitialized(DirectoryChunkHeaderSize);

	const FilesystemReadResult ReadResult = Read(ReadOffset, DirectoryChunkHeaderSize, FileBuffer.GetInternalArray().GetData());
	if (ReadResult != FilesystemReadResult::Success)
	{
		FsLogger::LogFormat(FilesystemLogType::Error, "Failed to read file as directory");
		return FsDirectoryDescriptor();
	}

	FsBitReader FileReader = FsBitReader(FileBuffer);

	// Read the file chunk header
	FsFileChunkHeader FileChunkHeader;
	FileChunkHeader.Serialize(FileReader);

	uint64 DirectoryContentLength = 0; // TODO: Read more than 1 block
	FileReader << DirectoryContentLength;

	// See if we need to read more chunks
	// TODO read more chunks (currently supporting only 1 chunk for directories rn)
	fsCheck(DirectoryContentLength < BlockSize, "Currently only support 1 block size for a directory!");

	if (DirectoryContentLength == 0)
	{
		// Empty directory
		FsDirectoryDescriptor EmptyDirectory = FsDirectoryDescriptor();
		return EmptyDirectory;
	}

	FileBuffer.AddUninitialized(DirectoryContentLength);

	const FilesystemReadResult ReadContentResult = Read(ReadOffset + DirectoryChunkHeaderSize, DirectoryContentLength, FileBuffer.GetInternalArray().GetData() + DirectoryChunkHeaderSize);
	if (ReadContentResult != FilesystemReadResult::Success)
	{
		FsLogger::LogFormat(FilesystemLogType::Error, "Failed to read file as directory");
		return FsDirectoryDescriptor();
	}

	// Now we have the whole file in memory, we can read the directory descriptor
	DirectoryDescriptor.Serialize(FileReader);

	CacheDirectory(FileDescriptor.FileOffset, DirectoryDescriptor);

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

		if (File.bIsDirectory)
		{
			FsLogger::LogFormat(FilesystemLogType::Info, "%s%s", Indent.GetData(), File.FileName.GetData());
		}
		else
		{
			FsLogger::LogFormat(FilesystemLogType::Info, "%s%s (%s)", Indent.GetData(), File.FileName.GetData(), GetCompressedBytesString(File.FileSize));
		}

		// Load directory
		if (File.bIsDirectory)
		{
			const FsDirectoryDescriptor Directory = ReadFileAsDirectory(File);
			LogAllFiles_Internal(Directory, Depth + 1);
		}
	}
}

bool FsFilesystem::FsDeleteDirectory(const FsPath& DirectoryName)
{
	const FsPath NormalizedPath = DirectoryName.NormalizePath();
	if (!FsIsDirectoryEmpty(DirectoryName))
	{
		FsLogger::LogFormat(FilesystemLogType::Error, "Cannot delete non-empty directory %s", NormalizedPath.GetData());
		return false;
	}

	const FsPath NormalizedDirectoryName = NormalizedPath.GetLastPath();
	const FsPath NormalizedParentDirectoryPath = NormalizedPath.GetPathWithoutFileName();

	FsDirectoryDescriptor ParentDirectory;
	FsFileDescriptor DirectoryFile;
	if (!GetDirectory(NormalizedParentDirectoryPath, ParentDirectory, &DirectoryFile))
	{
		FsLogger::LogFormat(FilesystemLogType::Error, "Failed to get parent directory %s", NormalizedParentDirectoryPath.GetData());
		return false;
	}

	uint64 DirectoryIndex = 0;
	bool bFoundDirectory = false;
	for (uint64 i = 0; i < ParentDirectory.Files.Length(); i++)
	{
		const FsFileDescriptor& File = ParentDirectory.Files[i];
		if (File.FileName == NormalizedDirectoryName)
		{
			DirectoryIndex = i;
			bFoundDirectory = true;
			break;
		}
	}

	if (!bFoundDirectory)
	{
		FsLogger::LogFormat(FilesystemLogType::Error, "Failed to find directory %s in parent directory %s", NormalizedDirectoryName.GetData(), NormalizedParentDirectoryPath.GetData());
		return false;
	}

	const FsFileDescriptor& DirectoryFileDescriptor = ParentDirectory.Files[DirectoryIndex];
	if (!DirectoryFileDescriptor.bIsDirectory)
	{
		FsLogger::LogFormat(FilesystemLogType::Error, "Cannot delete file %s using FsDeleteDirectory", NormalizedDirectoryName.GetData());
		return false;
	}

	// Get all chunks for the directory
	const FsArray<FsFileChunkHeader> AllChunks = GetAllChunksForFile(NormalizedPath, DirectoryFileDescriptor);

	if (!AllChunks.IsEmpty())
	{
		// Combine all blocks into an array for freeing
		FsBlockArray DirectoryBlocks = FsBlockArray();
		DirectoryBlocks.Add(AbsoluteOffsetToBlockIndex(DirectoryFileDescriptor.FileOffset));
		for (const FsFileChunkHeader& Chunk : AllChunks)
		{
			for (uint64 i = 0; i < Chunk.Blocks; i++)
			{
				if (Chunk.NextBlockIndex == 0)
				{
					// Last block
					break;
				}
				DirectoryBlocks.Add(Chunk.NextBlockIndex);
			}
		}

		// Free the blocks
		SetBlocksInUse(DirectoryBlocks, false);
	}

	// Remove the directory from the parent directory
	ParentDirectory.Files.RemoveAt(DirectoryIndex);

	// Resave the parent directory
	if (!SaveDirectory(ParentDirectory, DirectoryFile.FileOffset))
	{
		FsLogger::LogFormat(FilesystemLogType::Error, "Failed to save parent directory %s", NormalizedParentDirectoryPath.GetData());
		return false;
	}

	// Clear the cached chunks
	ClearCachedChunks(NormalizedPath);

	return true;
}

bool FsFilesystem::FsIsDirectoryEmpty(const FsPath& DirectoryName)
{
	const FsPath NormalizedPath = DirectoryName.NormalizePath();

	FsDirectoryDescriptor DirectoryDescriptor;
	if (!GetDirectory(NormalizedPath, DirectoryDescriptor))
	{
		FsLogger::LogFormat(FilesystemLogType::Error, "FsFindFiles: GlobalFilesystem is null");
		return false;
	}

	return DirectoryDescriptor.Files.IsEmpty();
}

bool FsFilesystem::FsDeleteFile(const FsPath& FileName)
{
	const FsPath NormalizedPath = FileName.NormalizePath();
	const FsPath NormalizedFileName = NormalizedPath.GetLastPath();
	const FsPath NormalizedDirectoryPath = NormalizedPath.GetPathWithoutFileName();

	FsDirectoryDescriptor Directory;
	FsFileDescriptor DirectoryFile;
	if (!GetDirectory(NormalizedDirectoryPath, Directory, &DirectoryFile))
	{
		FsLogger::LogFormat(FilesystemLogType::Error, "Failed to get directory %s", NormalizedDirectoryPath.GetData());
		return false;
	}

	uint64 FileIndex = 0;
	bool bFoundFile = false;
	for (uint64 i = 0; i < Directory.Files.Length(); i++)
	{
		const FsFileDescriptor& File = Directory.Files[i];
		if (File.FileName == NormalizedFileName)
		{
			FileIndex = i;
			bFoundFile = true;
			break;
		}
	}

	if (!bFoundFile)
	{
		FsLogger::LogFormat(FilesystemLogType::Error, "Failed to find file %s in directory %s", NormalizedFileName.GetData(), NormalizedDirectoryPath.GetData());
		return false;
	}

	const FsFileDescriptor& File = Directory.Files[FileIndex];
	if (File.bIsDirectory)
	{
		FsLogger::LogFormat(FilesystemLogType::Error, "Cannot delete directory %s using FsDeleteFile", NormalizedFileName.GetData());
		return false;
	}

	// Get all chunks for the file
	const FsArray<FsFileChunkHeader> AllChunks = GetAllChunksForFile(NormalizedPath, File);

	if (!AllChunks.IsEmpty())
	{
		// Combine all blocks into an array for freeing
		FsBlockArray FileBlocks = FsBlockArray();
		FileBlocks.Add(AbsoluteOffsetToBlockIndex(File.FileOffset));
		for (const FsFileChunkHeader& Chunk : AllChunks)
		{
			for (uint64 i = 0; i < Chunk.Blocks; i++)
			{
				if (Chunk.NextBlockIndex == 0)
				{
					// Last block
					break;
				}
				FileBlocks.Add(Chunk.NextBlockIndex);
			}
		}

		// Free the blocks
		SetBlocksInUse(FileBlocks, false);
	}

	// Remove the file from the directory
	Directory.Files.RemoveAt(FileIndex);

	// Resave the directory
	if (!SaveDirectory(Directory, DirectoryFile.FileOffset))
	{
		FsLogger::LogFormat(FilesystemLogType::Error, "Failed to save directory %s", NormalizedDirectoryPath.GetData());
		return false;
	}

	// Clear the cached chunks
	ClearCachedChunks(NormalizedPath);

	return true;
}

bool FsFilesystem::FsMoveFile(const FsPath& SourceFileName, const FsPath& DestinationFileName)
{
	const FsPath NormalizedSourcePath = SourceFileName.NormalizePath();
	const FsPath NormalizedSourceFileName = NormalizedSourcePath.GetLastPath();
	const FsPath NormalizedSourceDirectoryPath = NormalizedSourcePath.GetPathWithoutFileName();
	const FsPath NormalizedDestinationPath = DestinationFileName.NormalizePath();
	const FsPath NormalizedDestinationFileName = NormalizedDestinationPath.GetLastPath();
	const FsPath NormalizedDestinationDirectoryPath = NormalizedDestinationPath.GetPathWithoutFileName();

	const bool bSameDirectory = NormalizedSourceDirectoryPath == NormalizedDestinationDirectoryPath;

	FsDirectoryDescriptor DestinationDirectory;
	FsFileDescriptor DestinationDirectoryFile;
	if (!GetDirectory(NormalizedDestinationDirectoryPath, DestinationDirectory, &DestinationDirectoryFile))
	{
		FsLogger::LogFormat(FilesystemLogType::Error, "Failed to get destination directory %s", NormalizedDestinationDirectoryPath.GetData());
		return false;
	}

	FsDirectoryDescriptor SourceDirectory;
	FsFileDescriptor SourceDirectoryFile;
	if (bSameDirectory)
	{
		SourceDirectory = DestinationDirectory;
		SourceDirectoryFile = DestinationDirectoryFile;
	}
	else
	{
		if (!GetDirectory(NormalizedSourceDirectoryPath, SourceDirectory, &SourceDirectoryFile))
		{
			FsLogger::LogFormat(FilesystemLogType::Error, "Failed to get source directory %s", NormalizedSourceDirectoryPath.GetData());
			return false;
		}
	}

	FsFileDescriptor SourceFile;
	uint64 SourceFileIndex = 0;
	bool bFoundSourceFile = false;
	for (uint64 SourceIndex = 0; SourceIndex < SourceDirectory.Files.Length(); SourceIndex++)
	{
		const FsFileDescriptor& File = SourceDirectory.Files[SourceIndex];
		if (File.FileName == NormalizedSourceFileName)
		{
			SourceFile = File;
			bFoundSourceFile = true;
			SourceFileIndex = SourceIndex;
			break;
		}
	}

	if (!bFoundSourceFile)
	{
		FsLogger::LogFormat(FilesystemLogType::Error, "Failed to find source file %s in directory %s", NormalizedSourceFileName.GetData(), NormalizedSourceDirectoryPath.GetData());
		return false;
	}

	for (const FsFileDescriptor& File : DestinationDirectory.Files)
	{
		if (File.FileName == NormalizedDestinationFileName)
		{
			FsLogger::LogFormat(FilesystemLogType::Error, "Destination file %s already exists in directory %s", NormalizedDestinationFileName.GetData(), NormalizedDestinationDirectoryPath.GetData());
			return false;
		}
	}

	// Move the file
	SourceFile.FileName = NormalizedDestinationFileName;
	if (bSameDirectory)
	{
		DestinationDirectory.Files.RemoveAt(SourceFileIndex);
	}
	else
	{
		SourceDirectory.Files.RemoveAt(SourceFileIndex);
	}
	DestinationDirectory.Files.Add(SourceFile);

	// Save the directories
	if (!SaveDirectory(DestinationDirectory, DestinationDirectoryFile.FileOffset))
	{
		FsLogger::LogFormat(FilesystemLogType::Error, "Failed to save destination directory %s", NormalizedDestinationDirectoryPath.GetData());
		return false;
	}

	if (!bSameDirectory) // If its the same directory then we already saved it.
	{
		if (!SaveDirectory(SourceDirectory, SourceDirectoryFile.FileOffset))
		{
			FsLogger::LogFormat(FilesystemLogType::Error, "Failed to save source directory %s", NormalizedSourceDirectoryPath.GetData());
			return false;
		}
	}

	ClearCachedChunks(NormalizedSourcePath);
	ClearCachedChunks(NormalizedDestinationPath);

	return true;
}

bool FsFilesystem::CopyFile(const FsPath& SourceFileName, const FsPath& DestinationFileName)
{
	return false;
}

const char* FsFilesystem::GetCompressedBytesString(uint64 Bytes)
{
	static char Buffer[256];
	if (Bytes < 1024)
	{
		FsFormatter::Format(Buffer, 256, "%uB", Bytes);
	}
	else if (Bytes < 1024 * 1024)
	{
		uint64 WholePart = Bytes / 1024;
		uint64 DecimalPart = (Bytes % 1024) * 100 / 1024;
		FsFormatter::Format(Buffer, 256, "%u.%uKB", WholePart, DecimalPart);
	}
	else if (Bytes < 1024 * 1024 * 1024)
	{
		uint64 WholePart = Bytes / (1024 * 1024);
		uint64 DecimalPart = (Bytes % (1024 * 1024)) * 100 / (1024 * 1024);
		FsFormatter::Format(Buffer, 256, "%u.%uMB", WholePart, DecimalPart);
	}
	else
	{
		uint64 WholePart = Bytes / (1024 * 1024 * 1024);
		uint64 DecimalPart = (Bytes % (1024 * 1024 * 1024)) * 100 / (1024 * 1024 * 1024);
		FsFormatter::Format(Buffer, 256, "%u.%uGB", WholePart, DecimalPart);
	}

	return Buffer;
}

bool FsFilesystem::GetTotalAndFreeBytes(uint64& OutTotalBytes, uint64& OutFreeBytes)
{
	OutTotalBytes = GetPartitionSize();
	OutFreeBytes = 0;
	FsBitArray BlockBuffer;
	BlockBuffer.FillZeroed(GetBlockBufferSizeBytes());

	const FilesystemReadResult ReadResult = Read(GetBlockBufferOffset(), GetBlockBufferSizeBytes(), BlockBuffer.GetInternalArray().GetData());
	if (ReadResult != FilesystemReadResult::Success)
	{
		FsLogger::LogFormat(FilesystemLogType::Error, "GetTotalAndUsedBytes: Failed to read block buffer. Ensure `Read` is implemented correctly.");
		return false;
	}

	// Calculate the minimum block index that we should skip to avoid the block buffer.
	const uint64 MinBlockIndex = GetContentStartOffset() / BlockSize;

	for (uint64 i = MinBlockIndex; i < BlockBuffer.BitLength(); i++)
	{
		if (!BlockBuffer.GetBit(i))
		{
			OutFreeBytes += BlockSize;
		}
	}

	return true;
}

void FsFilesystem::CacheDirectory(uint64 Offset, const FsDirectoryDescriptor& Directory)
{
	ClearCachedDirectory(Offset);
	CachedDirectories.Add(FsCachedDirectory(Offset, Directory));
}

void FsFilesystem::ClearCachedDirectory(uint64 Offset)
{
	for (uint64 i = 0; i < CachedDirectories.Length(); i++)
	{
		if (CachedDirectories[i].Offset == Offset)
		{
			CachedDirectories.RemoveAt(i);
			return;
		}
	}
}

bool FsFilesystem::GetCachedDirectory(uint64 Offset, FsDirectoryDescriptor& OutDirectory)
{
	for (const FsCachedDirectory& CachedDirectory : CachedDirectories)
	{
		if (CachedDirectory.Offset == Offset)
		{
			OutDirectory = CachedDirectory.Directory;
			return true;
		}
	}
	return false;
}

FsArray<uint8>* FsFilesystem::CacheRead(uint64 BlockIndex)
{
	fsCheck(false, "Not Implemented (WIP)");
	return nullptr;

	ClearCachedRead(BlockIndex);
	CachedReads.Add({BlockIndex, FsArray<uint8>()});
	return &CachedReads[CachedReads.Length() - 1].Data;
}

void FsFilesystem::ClearCachedRead(uint64 BlockIndex)
{
	return;

	for (uint64 i = 0; i < CachedReads.Length(); i++)
	{
		if (CachedReads[i].BlockIndex == BlockIndex)
		{
			CachedReads.RemoveAt(i);
			return;
		}
	}
}

FsArray<uint8>* FsFilesystem::GetCachedRead(uint64 BlockIndex)
{
	return nullptr;

	for (FsReadCache& CachedRead : CachedReads)
	{
		if (CachedRead.BlockIndex == BlockIndex)
		{
			return &CachedRead.Data;
		}
	}

	return nullptr;
}