#include <iostream>
#include "FilesystemImplementation.h"
#include "FsLib/FsArray.h"
#include "FsLib/FsLogger.h"
#include "FsLib/FsString.h"
#include "FsLib/FsBitStream.h"
#include "FsLib/FsTests.h"

typedef _Return_type_success_(return >= 0) long NTSTATUS;
#include <dokan/dokan.h>
#include <dokan/fileinfo.h>

#ifdef CreateDirectory
#undef CreateDirectory
#endif

#ifdef CreateFile
#undef CreateFile
#endif

FsFilesystem* GlobalFilesystem = nullptr;

bool IgnoreFilePath(const FsString & InFilePath)
{
	return InFilePath.Contains("git") ||
		InFilePath.Contains("svn") ||
		InFilePath.EndsWith("HEAD") ||
		InFilePath.EndsWith("objects") ||
		InFilePath.EndsWith("refs") ||
		InFilePath.EndsWith("config");
}

// Function to convert LPCWSTR to FsString. It needs to be converted to multi-byte string to be logged.
FsString LPCWSTRToFsString(LPCWSTR InString)
{
	FsString OutString = FsString();
	if (!InString)
	{
		return OutString;
	}

	size_t Size = wcslen(InString) + 1;
	char* Buffer = new char[Size];
	size_t ConvertedChars = 0;
	wcstombs_s(&ConvertedChars, Buffer, Size, InString, _TRUNCATE);
	OutString.Append(const_cast<const char*>(Buffer));
	delete[] Buffer;
	return OutString;
}

// function to convert a multi-byte string into LPCWSTR
LPCWSTR FsStringToLPCWSTR(const FsString& InString)
{
	fsCheck(InString.Length() < MAX_PATH, "Stinky");
	static wchar_t Buffer[MAX_PATH];
	size_t ConvertedChars = 0;
	mbstowcs_s(&ConvertedChars, Buffer, MAX_PATH, InString.GetData(), _TRUNCATE);
	return Buffer;
}

NTSTATUS DOKAN_CALLBACK FsDokanCreateFile(LPCWSTR FileName,
	PDOKAN_IO_SECURITY_CONTEXT SecurityContext,
	ACCESS_MASK DesiredAccess,
	ULONG FileAttributes,
	ULONG ShareAccess,
	ULONG CreateDisposition,
	ULONG CreateOptions,
	PDOKAN_FILE_INFO DokanFileInfo)
{
	if (!GlobalFilesystem)
	{
		FsLogger::LogFormat(FilesystemLogType::Error, "FsDokanCreateFile: GlobalFilesystem is null");
		return STATUS_NOT_IMPLEMENTED;
	}

	const FsString FileNameString = LPCWSTRToFsString(FileName);
	if (IgnoreFilePath(FileNameString))
	{
		return STATUS_NO_SUCH_FILE;
	}

	ACCESS_MASK generic_desiredaccess;
	DWORD creation_disposition;
	DWORD file_attributes_and_flags;
	
	DokanMapKernelToUserCreateFileFlags(
		DesiredAccess, FileAttributes, CreateOptions, CreateDisposition,
		&generic_desiredaccess, &file_attributes_and_flags,
		&creation_disposition);
		
	const bool bIsRootDirectory = FileNameString == FsString("\\");
	const bool bHasExistingDirectory = bIsRootDirectory || GlobalFilesystem->DirectoryExists(FileNameString);
	const bool bHasExistingFile = GlobalFilesystem->FileExists(FileNameString);

	if (bHasExistingDirectory)
	{
		if (CreateOptions & FILE_NON_DIRECTORY_FILE)
		{
			return STATUS_FILE_IS_A_DIRECTORY;
		}

		DokanFileInfo->IsDirectory = true;
	}

	if (DokanFileInfo->IsDirectory)
	{
		if (creation_disposition == CREATE_NEW ||
			creation_disposition == OPEN_ALWAYS)
		{
			if (bHasExistingFile || bHasExistingDirectory)
			{
				return STATUS_OBJECT_NAME_COLLISION;
			}

			if (!GlobalFilesystem->CreateDirectory(FileNameString))
			{
				return STATUS_NO_SUCH_FILE;
			}
			return STATUS_SUCCESS;
		}

		if (bHasExistingFile && !bHasExistingDirectory)
		{
			return STATUS_NOT_A_DIRECTORY;
		}

		if (!bHasExistingFile && !bHasExistingDirectory)
		{
			return STATUS_OBJECT_NAME_NOT_FOUND;
		}
	}
	else
	{
		switch (creation_disposition)
		{
			case CREATE_ALWAYS:
			{
				if (bHasExistingFile)
				{
					return STATUS_OBJECT_NAME_COLLISION;
				}

				if (!GlobalFilesystem->CreateFile(FileNameString))
				{
					return STATUS_NO_SUCH_FILE;
				}
				break;
			}
			case CREATE_NEW:
			{
				if (bHasExistingFile)
				{
					return STATUS_OBJECT_NAME_COLLISION;
				}

				if (!GlobalFilesystem->CreateFile(FileNameString))
				{
					return STATUS_NO_SUCH_FILE;
				}
				break;
			}
			case OPEN_ALWAYS:
			{
				if (!bHasExistingFile)
				{
					if (!GlobalFilesystem->CreateFile(FileNameString))
					{
						return STATUS_NO_SUCH_FILE;
					}
				}
				break;
			}
			case OPEN_EXISTING:
			{
				if (!bHasExistingFile)
				{
					return STATUS_OBJECT_NAME_NOT_FOUND;
				}
				break;
			}
			case TRUNCATE_EXISTING:
			{
				if (!bHasExistingFile)
				{
					return STATUS_OBJECT_NAME_NOT_FOUND;
				}

				// TODO: Truncate
				break;
			}
			default:
			{
				fsCheck(false, "Invalid creation disposition");
				return STATUS_INVALID_PARAMETER;
			}
		}
	}

	if ((bHasExistingFile || bHasExistingDirectory) && (creation_disposition == CREATE_NEW || creation_disposition == OPEN_ALWAYS))
	{
		return STATUS_OBJECT_NAME_COLLISION;
	}

	return STATUS_SUCCESS;
}

void DOKAN_CALLBACK FsCleanup(LPCWSTR FileName,
	PDOKAN_FILE_INFO DokanFileInfo)
{
	if (!GlobalFilesystem)
	{
		FsLogger::LogFormat(FilesystemLogType::Error, "GlobalFilesystem is null");
		return;
	}

	const FsString FileNameString = LPCWSTRToFsString(FileName);
	if (IgnoreFilePath(FileNameString))
	{
		return;
	}

	if (DokanFileInfo->DeleteOnClose)
	{
		FsLogger::LogFormat(FilesystemLogType::Info, "CloseFile %s Delete on close", FileNameString.GetData());
	}
}

void DOKAN_CALLBACK FsCloseFile(LPCWSTR FileName,
	PDOKAN_FILE_INFO DokanFileInfo)
{
	if (!GlobalFilesystem)
	{
		FsLogger::LogFormat(FilesystemLogType::Error, "GlobalFilesystem is null");
		return;
	}

	const FsString FileNameString = LPCWSTRToFsString(FileName);
	if (IgnoreFilePath(FileNameString))
	{
		return;
	}

	if (!DokanFileInfo->DeleteOnClose)
	{
		return;
	}

	FsLogger::LogFormat(FilesystemLogType::Info, "CloseFile %s Delete on close", FileNameString.GetData());

	if (DokanFileInfo->IsDirectory)
	{
		if (!GlobalFilesystem->FsDeleteDirectory(FileNameString))
		{
			FsLogger::LogFormat(FilesystemLogType::Error, "Failed to delete directory: %s", FileNameString.GetData());
		}
	}
	else
	{
		if (!GlobalFilesystem->FsDeleteFile(FileNameString))
		{
			FsLogger::LogFormat(FilesystemLogType::Error, "Failed to delete file: %s", FileNameString.GetData());
		}
	}
}

NTSTATUS DOKAN_CALLBACK FsReadFile(LPCWSTR FileName,
	LPVOID Buffer,
	DWORD BufferLength,
	LPDWORD ReadLength,
	LONGLONG Offset,
	PDOKAN_FILE_INFO DokanFileInfo)
{
	const FsString FileNameString = LPCWSTRToFsString(FileName);
	if (IgnoreFilePath(FileNameString))
	{
		return STATUS_NO_SUCH_FILE;
	}
	//FsLogger::LogFormat(FilesystemLogType::Info, "ReadFile: %s", FileNameString.GetData());
	if (!GlobalFilesystem)
	{
		FsLogger::LogFormat(FilesystemLogType::Error, "GlobalFilesystem is null");
		return STATUS_NOT_IMPLEMENTED;
	}

	uint64 FileSize = 0;
	if (!GlobalFilesystem->GetFileSize(FileNameString, FileSize))
	{
		FsLogger::LogFormat(FilesystemLogType::Error, "Failed to get file size");
		return STATUS_NO_SUCH_FILE;
	}

	uint64 BytesRead = 0;
	if (!GlobalFilesystem->ReadFromFile(FileNameString, Offset, reinterpret_cast<uint8*>(Buffer), BufferLength, &BytesRead))
	{
		return STATUS_NO_SUCH_FILE;
	}

	*ReadLength = BytesRead;

	FsLogger::LogFormat(FilesystemLogType::Info, "ReadFile: %s, Offset %u, Bytes Read %u", FileNameString.GetData(), Offset, (uint64)BytesRead);

	return STATUS_SUCCESS;
}

NTSTATUS DOKAN_CALLBACK FsWriteFile(LPCWSTR FileName,
	LPCVOID Buffer,
	DWORD NumberOfBytesToWrite,
	LPDWORD NumberOfBytesWritten,
	LONGLONG Offset,
	PDOKAN_FILE_INFO DokanFileInfo)
{
	const FsString FileNameString = LPCWSTRToFsString(FileName);
	if (IgnoreFilePath(FileNameString))
	{
		return STATUS_NO_SUCH_FILE;
	}
	FsLogger::LogFormat(FilesystemLogType::Info, "WriteFile: %s", FileNameString.GetData());
	if (!GlobalFilesystem)
	{
		FsLogger::LogFormat(FilesystemLogType::Error, "GlobalFilesystem is null");
		return STATUS_NOT_IMPLEMENTED;
	}

	if (!GlobalFilesystem->WriteToFile(FileNameString, reinterpret_cast<const uint8*>(Buffer), Offset, NumberOfBytesToWrite))
	{
		return STATUS_NO_SUCH_FILE;
	}

	FsLogger::LogFormat(FilesystemLogType::Info, "WriteFile: %s, Offset %u, Bytes Written %u", FileNameString.GetData(), Offset, (uint64)NumberOfBytesToWrite);

	*NumberOfBytesWritten = NumberOfBytesToWrite;

	return STATUS_SUCCESS;
}

NTSTATUS DOKAN_CALLBACK FsFlushFileBuffers(LPCWSTR FileName,
	PDOKAN_FILE_INFO DokanFileInfo)
{
	FsLogger::LogFormat(FilesystemLogType::Info, "FlushFileBuffers: %s", FileName);
	if (!GlobalFilesystem)
	{
		FsLogger::LogFormat(FilesystemLogType::Error, "GlobalFilesystem is null");
		return STATUS_NOT_IMPLEMENTED;
	}
	return STATUS_SUCCESS;
}

NTSTATUS DOKAN_CALLBACK FsGetFileInformation(LPCWSTR FileName,
	LPBY_HANDLE_FILE_INFORMATION HandleFileInformation,
	PDOKAN_FILE_INFO DokanFileInfo)
{
	const FsString FileNameString = LPCWSTRToFsString(FileName);
	if (IgnoreFilePath(FileNameString))
	{
		return STATUS_NO_SUCH_FILE;
	}	
	if (FileNameString == FsString("\\"))
	{
		HandleFileInformation->dwFileAttributes = FILE_ATTRIBUTE_DIRECTORY; 
		HandleFileInformation->nFileSizeHigh = 0;
		HandleFileInformation->nFileSizeLow = 0;
		return STATUS_SUCCESS;
	}


	if (!GlobalFilesystem)
	{
		FsLogger::LogFormat(FilesystemLogType::Error, "FsGetFileInformation: GlobalFilesystem is null");
		return STATUS_NOT_IMPLEMENTED;
	}

	if (DokanFileInfo->IsDirectory)
	{
		FsDirectoryDescriptor DirectoryDescriptor;
		if (!GlobalFilesystem->GetDirectory(FileNameString, DirectoryDescriptor))
		{
			FsLogger::LogFormat(FilesystemLogType::Error, "FsGetFileInformation: Failed to get directory: %s", FileNameString.GetData());
			return STATUS_OBJECT_NAME_NOT_FOUND;
		}

		HandleFileInformation->dwFileAttributes = FILE_ATTRIBUTE_DIRECTORY;
		HandleFileInformation->nFileSizeHigh = 0;
		HandleFileInformation->nFileSizeLow = 0;
		HandleFileInformation->dwVolumeSerialNumber = 0x19831116;
		HandleFileInformation->nNumberOfLinks = 1;

		// Fake creation time, last access time and last write time to the current time
		SYSTEMTIME SystemTime;
		GetSystemTime(&SystemTime);
		SystemTimeToFileTime(&SystemTime, &HandleFileInformation->ftCreationTime);
		HandleFileInformation->ftLastAccessTime = HandleFileInformation->ftCreationTime;
		HandleFileInformation->ftLastWriteTime = HandleFileInformation->ftCreationTime;

		// Set time to 0
		HandleFileInformation->ftCreationTime = { 0 };
		HandleFileInformation->ftLastAccessTime = { 0 };
		HandleFileInformation->ftLastWriteTime = { 0 };
	}
	else
	{
		FsFileDescriptor FileDescriptor;
		if (!GlobalFilesystem->GetFile(FileNameString, FileDescriptor))
		{
			FsLogger::LogFormat(FilesystemLogType::Error, "FsGetFileInformation: Failed to get file: %s", FileNameString.GetData());
			return STATUS_OBJECT_NAME_NOT_FOUND;
		}

		HandleFileInformation->dwFileAttributes = FILE_ATTRIBUTE_NORMAL;
		const uint64 FileSize = FileDescriptor.FileSize;
		HandleFileInformation->nFileSizeHigh = (FileSize >> 32) & 0xFFFFFFFF;
		HandleFileInformation->nFileSizeLow = FileSize & 0xFFFFFFFF;
		HandleFileInformation->dwVolumeSerialNumber = 0x19831116;

		// Fake creation time, last access time and last write time to the current time
		SYSTEMTIME SystemTime;
		GetSystemTime(&SystemTime);
		SystemTimeToFileTime(&SystemTime, &HandleFileInformation->ftCreationTime);
		HandleFileInformation->ftLastAccessTime = HandleFileInformation->ftCreationTime;
		HandleFileInformation->ftLastWriteTime = HandleFileInformation->ftCreationTime;

		HandleFileInformation->ftCreationTime = { 0 };
		HandleFileInformation->ftLastAccessTime = { 0 };
		HandleFileInformation->ftLastWriteTime = { 0 };
	}

	return STATUS_SUCCESS;
}

NTSTATUS DOKAN_CALLBACK FsFindFiles(LPCWSTR FileName,
	PFillFindData FillFindData, // function pointer
	PDOKAN_FILE_INFO DokanFileInfo)
{
	const FsString FileNameString = LPCWSTRToFsString(FileName);
	if (IgnoreFilePath(FileNameString))
	{
		return STATUS_NO_SUCH_FILE;
	}
	//FsLogger::LogFormat(FilesystemLogType::Info, "FindFiles: %s", FileNameString.GetData());

	FsDirectoryDescriptor DirectoryDescriptor;
	if (!GlobalFilesystem->GetDirectory(FileNameString, DirectoryDescriptor))
	{
		FsLogger::LogFormat(FilesystemLogType::Error, "FsFindFiles: GlobalFilesystem is null");
		return STATUS_OBJECT_NAME_NOT_FOUND;
	}

	for (const FsFileDescriptor& FileDescriptor : DirectoryDescriptor.Files)
	{
		WIN32_FIND_DATAW FindData;
		ZeroMemory(&FindData, sizeof(FindData));
		FindData.dwFileAttributes = FileDescriptor.bIsDirectory ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
		FindData.nFileSizeHigh = (FileDescriptor.FileSize >> 32) & 0xFFFFFFFF;
		FindData.nFileSizeLow = FileDescriptor.FileSize & 0xFFFFFFFF;
		FindData.ftCreationTime = { 0 };
		FindData.ftLastAccessTime = { 0 };
		FindData.ftLastWriteTime = { 0 };
		wcscpy_s(FindData.cFileName, FsStringToLPCWSTR(FileDescriptor.FileName.GetData()));

		DOKAN_FILE_INFO NewDokanFileInfo;
		ZeroMemory(&NewDokanFileInfo, sizeof(NewDokanFileInfo));
		NewDokanFileInfo.IsDirectory = FileDescriptor.bIsDirectory;

		FillFindData(&FindData, DokanFileInfo);
	}

	if (!GlobalFilesystem)
	{
		FsLogger::LogFormat(FilesystemLogType::Error, "GlobalFilesystem is null");
		return STATUS_NOT_IMPLEMENTED;
	}
	return STATUS_SUCCESS;
}

NTSTATUS DOKAN_CALLBACK FsFindFilesWithPattern(LPCWSTR FileName,
	LPCWSTR SearchPattern,
	PFillFindData FillFindData,
	PDOKAN_FILE_INFO DokanFileInfo)
{
	//FsLogger::LogFormat(FilesystemLogType::Info, "FindFilesWithPattern: %s", FileName);
	if (!GlobalFilesystem)
	{
		FsLogger::LogFormat(FilesystemLogType::Error, "GlobalFilesystem is null");
		return STATUS_NOT_IMPLEMENTED;
	}
	return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS DOKAN_CALLBACK FsSetFileAttributes(LPCWSTR FileName,
	DWORD FileAttributes,
	PDOKAN_FILE_INFO DokanFileInfo)
{
	FsLogger::LogFormat(FilesystemLogType::Info, "SetFileAttributes: %s", FileName);
	if (!GlobalFilesystem)
	{
		FsLogger::LogFormat(FilesystemLogType::Error, "GlobalFilesystem is null");
		return STATUS_NOT_IMPLEMENTED;
	}
	return STATUS_SUCCESS;
}

NTSTATUS DOKAN_CALLBACK FsSetFileTime(LPCWSTR FileName,
	CONST FILETIME* CreationTime,
	CONST FILETIME* LastAccessTime,
	CONST FILETIME* LastWriteTime,
	PDOKAN_FILE_INFO DokanFileInfo)
{
	FsLogger::LogFormat(FilesystemLogType::Info, "SetFileTime: %s", FileName);
	if (!GlobalFilesystem)
	{
		FsLogger::LogFormat(FilesystemLogType::Error, "GlobalFilesystem is null");
		return STATUS_NOT_IMPLEMENTED;
	}
	return STATUS_SUCCESS;
}

NTSTATUS DOKAN_CALLBACK FsDeleteFile(LPCWSTR FileName,
	PDOKAN_FILE_INFO DokanFileInfo)
{
	if (!GlobalFilesystem)
	{
		FsLogger::LogFormat(FilesystemLogType::Error, "GlobalFilesystem is null");
		return STATUS_NOT_IMPLEMENTED;
	}

	const FsString FileNameString = LPCWSTRToFsString(FileName);
	if (IgnoreFilePath(FileNameString))
	{
		return STATUS_NO_SUCH_FILE;
	}

	FsLogger::LogFormat(FilesystemLogType::Info, "DeleteFile: %s", FileNameString.GetData());
	if (!GlobalFilesystem->FsDeleteFile(FileNameString))
	{
		return STATUS_NO_SUCH_FILE;
	}

	return STATUS_SUCCESS;
}

NTSTATUS DOKAN_CALLBACK FsDeleteDirectory(LPCWSTR FileName,
	PDOKAN_FILE_INFO DokanFileInfo)
{
	if (!GlobalFilesystem)
	{
		FsLogger::LogFormat(FilesystemLogType::Error, "GlobalFilesystem is null");
		return STATUS_NOT_IMPLEMENTED;
	}

	const FsString FileNameString = LPCWSTRToFsString(FileName);
	if (IgnoreFilePath(FileNameString))
	{
		return STATUS_NO_SUCH_FILE;
	}

	FsLogger::LogFormat(FilesystemLogType::Info, "DeleteDirectory: %s", FileNameString.GetData());	
	if (!GlobalFilesystem->FsDeleteDirectory(FileNameString))
	{
		return STATUS_NO_SUCH_FILE;
	}

	return STATUS_SUCCESS;
}

NTSTATUS DOKAN_CALLBACK FsMoveFile(LPCWSTR FileName, // existing file name
	LPCWSTR NewFileName, 
	BOOL ReplaceIfExisting,
	PDOKAN_FILE_INFO DokanFileInfo)
{
	if (!GlobalFilesystem)
	{
		FsLogger::LogFormat(FilesystemLogType::Error, "GlobalFilesystem is null");
		return STATUS_NOT_IMPLEMENTED;
	}

	const FsString FromFileNameString = LPCWSTRToFsString(FileName);
	if (IgnoreFilePath(FromFileNameString))
	{
		return STATUS_NO_SUCH_FILE;
	}
	const FsString ToFileNameString = LPCWSTRToFsString(NewFileName);
	if (IgnoreFilePath(ToFileNameString))
	{
		return STATUS_NO_SUCH_FILE;
	}

	const bool bDestinationExists = GlobalFilesystem->FileExists(ToFileNameString) || GlobalFilesystem->DirectoryExists(ToFileNameString);
	if (bDestinationExists)
	{
		return STATUS_ACCESS_DENIED;
	}

	if (!GlobalFilesystem->FsMoveFile(FromFileNameString, ToFileNameString))
	{
		return STATUS_NO_SUCH_FILE;
	}

	return STATUS_SUCCESS;
}

NTSTATUS DOKAN_CALLBACK FsSetEndOfFile(LPCWSTR FileName,
	LONGLONG ByteOffset,
	PDOKAN_FILE_INFO DokanFileInfo)
{
	FsLogger::LogFormat(FilesystemLogType::Info, "SetEndOfFile: %s", FileName);
	if (!GlobalFilesystem)
	{
		FsLogger::LogFormat(FilesystemLogType::Error, "GlobalFilesystem is null");
		return STATUS_NOT_IMPLEMENTED;
	}
	return STATUS_SUCCESS;
}

NTSTATUS DOKAN_CALLBACK FsSetAllocationSize(LPCWSTR FileName,
	LONGLONG AllocSize,
	PDOKAN_FILE_INFO DokanFileInfo)
{
	FsLogger::LogFormat(FilesystemLogType::Info, "SetAllocationSize: %s", FileName);
	if (!GlobalFilesystem)
	{
		FsLogger::LogFormat(FilesystemLogType::Error, "GlobalFilesystem is null");
		return STATUS_NOT_IMPLEMENTED;
	}
	return STATUS_SUCCESS;
}

NTSTATUS DOKAN_CALLBACK FsLockFile(LPCWSTR FileName,
	LONGLONG ByteOffset,
	LONGLONG Length,
	PDOKAN_FILE_INFO DokanFileInfo)
{
	FsLogger::LogFormat(FilesystemLogType::Info, "LockFile: %s", FileName);
	if (!GlobalFilesystem)
	{
		FsLogger::LogFormat(FilesystemLogType::Error, "GlobalFilesystem is null");
		return STATUS_NOT_IMPLEMENTED;
	}
	return STATUS_SUCCESS;
}

NTSTATUS DOKAN_CALLBACK FsUnlockFile(LPCWSTR FileName,
	LONGLONG ByteOffset,
	LONGLONG Length,
	PDOKAN_FILE_INFO DokanFileInfo)
{
	FsLogger::LogFormat(FilesystemLogType::Info, "UnlockFile: %s", FileName);
	if (!GlobalFilesystem)
	{
		FsLogger::LogFormat(FilesystemLogType::Error, "GlobalFilesystem is null");
		return STATUS_NOT_IMPLEMENTED;
	}
	return STATUS_SUCCESS;
}

NTSTATUS DOKAN_CALLBACK FsGetDiskFreeSpace(PULONGLONG FreeBytesAvailable,
	PULONGLONG TotalNumberOfBytes,
	PULONGLONG TotalNumberOfFreeBytes,
	PDOKAN_FILE_INFO DokanFileInfo)
{
	FsLogger::LogFormat(FilesystemLogType::Info, "GetDiskFreeSpace");
	if (!GlobalFilesystem)
	{
		FsLogger::LogFormat(FilesystemLogType::Error, "GlobalFilesystem is null");
		return STATUS_NOT_IMPLEMENTED;
	}

	GlobalFilesystem->GetTotalAndFreeBytes(*TotalNumberOfBytes, *TotalNumberOfFreeBytes);
	*FreeBytesAvailable = *TotalNumberOfFreeBytes;

	return STATUS_SUCCESS;
}

NTSTATUS DOKAN_CALLBACK FsGetVolumeInformation(LPWSTR VolumeNameBuffer,
	DWORD VolumeNameSize,
	LPDWORD VolumeSerialNumber,
	LPDWORD MaximumComponentLength,
	LPDWORD FileSystemFlags,
	LPWSTR FileSystemNameBuffer,
	DWORD FileSystemNameSize,
	PDOKAN_FILE_INFO DokanFileInfo)
{
	//FsLogger::LogFormat(FilesystemLogType::Info, "GetVolumeInformation");
	if (!GlobalFilesystem)
	{
		FsLogger::LogFormat(FilesystemLogType::Error, "GlobalFilesystem is null");
		return STATUS_NOT_IMPLEMENTED;
	}

	const wchar_t* VolumeName = L"FsTest";
	const wchar_t* FileSystemName = L"FsTest";

	if (VolumeNameBuffer)
	{
		wcscpy_s(VolumeNameBuffer, VolumeNameSize, VolumeName);
	}

	if (FileSystemNameBuffer)
	{
		wcscpy_s(FileSystemNameBuffer, FileSystemNameSize, FileSystemName);
	}

	if (VolumeSerialNumber)
	{
		*VolumeSerialNumber = 0x19831116;
	}

	if (MaximumComponentLength)
	{
		*MaximumComponentLength = 255;
	}

	if (FileSystemFlags)
	{
		// None atm
	}



	return STATUS_SUCCESS;
}

NTSTATUS DOKAN_CALLBACK FsMounted(LPCWSTR MountPoint, PDOKAN_FILE_INFO DokanFileInfo)
{
	FsLogger::LogFormat(FilesystemLogType::Info, "Mounted");
	if (!GlobalFilesystem)
	{
		FsLogger::LogFormat(FilesystemLogType::Error, "GlobalFilesystem is null");
		return STATUS_NOT_IMPLEMENTED;
	}
	return STATUS_SUCCESS;
}

NTSTATUS DOKAN_CALLBACK FsUnmounted(PDOKAN_FILE_INFO DokanFileInfo)
{
	FsLogger::LogFormat(FilesystemLogType::Info, "Unmounted");
	if (!GlobalFilesystem)
	{
		FsLogger::LogFormat(FilesystemLogType::Error, "GlobalFilesystem is null");
		return STATUS_NOT_IMPLEMENTED;
	}
	return STATUS_SUCCESS;
}

NTSTATUS DOKAN_CALLBACK FsGetFileSecurity(LPCWSTR FileName,
	PSECURITY_INFORMATION SecurityInformation,
	PSECURITY_DESCRIPTOR SecurityDescriptor,
	ULONG BufferLength,
	PULONG LengthNeeded,
	PDOKAN_FILE_INFO DokanFileInfo)
{
	FsLogger::LogFormat(FilesystemLogType::Info, "GetFileSecurity: %s", FileName);
	if (!GlobalFilesystem)
	{
		FsLogger::LogFormat(FilesystemLogType::Error, "GlobalFilesystem is null");
		return STATUS_NOT_IMPLEMENTED;
	}
	return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS DOKAN_CALLBACK FsSetFileSecurity(LPCWSTR FileName,
	PSECURITY_INFORMATION SecurityInformation,
	PSECURITY_DESCRIPTOR SecurityDescriptor,
	ULONG SecurityDescriptorLength,
	PDOKAN_FILE_INFO DokanFileInfo)
{
	FsLogger::LogFormat(FilesystemLogType::Info, "SetFileSecurity: %s", FileName);
	if (!GlobalFilesystem)
	{
		FsLogger::LogFormat(FilesystemLogType::Error, "GlobalFilesystem is null");
		return STATUS_NOT_IMPLEMENTED;
	}
	return STATUS_SUCCESS;
}

NTSTATUS DOKAN_CALLBACK FsFindStreams(LPCWSTR FileName,
	PFillFindStreamData FillFindStreamData,
	PVOID FindStreamContext,
	PDOKAN_FILE_INFO DokanFileInfo)
{
	FsLogger::LogFormat(FilesystemLogType::Info, "FindStreams: %s", FileName);
	if (!GlobalFilesystem)
	{
		FsLogger::LogFormat(FilesystemLogType::Error, "GlobalFilesystem is null");
		return STATUS_NOT_IMPLEMENTED;
	}
	return STATUS_SUCCESS;
}

DOKAN_HANDLE DokanInstance = nullptr;

int main()
{
	FsLoggerImpl Logger = FsLoggerImpl();
	Logger.SetShouldLogVerbose(false);
	FsMemoryAllocatorImpl Allocator = FsMemoryAllocatorImpl();

	// Make a test fs with a 4GB partition and 128KB block size
	FsFilesystemImpl FsFilesystem = FsFilesystemImpl(1024ull * 1024ull * 1024ull * 4ull, 1024 * 128);
	GlobalFilesystem = &FsFilesystem;

	FsFilesystem.Initialize();

	//FsTests::RunTests(FsFilesystem);

	FsFilesystem.LogAllFiles();

	DOKAN_OPTIONS dokan_options;
	ZeroMemory(&dokan_options, sizeof(DOKAN_OPTIONS));

	dokan_options.Version = DOKAN_VERSION;
	dokan_options.SingleThread = true;
	dokan_options.MountPoint = L"Y:";
	dokan_options.Timeout = 5000;

	DOKAN_OPERATIONS dokan_operations;
	ZeroMemory(&dokan_operations, sizeof(DOKAN_OPERATIONS));

	dokan_operations.ZwCreateFile = FsDokanCreateFile;
	dokan_operations.Cleanup = FsCleanup;
	dokan_operations.CloseFile = FsCloseFile;
	dokan_operations.ReadFile = FsReadFile;
	dokan_operations.WriteFile = FsWriteFile;
	dokan_operations.FlushFileBuffers = FsFlushFileBuffers;
	dokan_operations.GetFileInformation = FsGetFileInformation;
	dokan_operations.FindFiles = FsFindFiles;
	dokan_operations.FindFilesWithPattern = FsFindFilesWithPattern;
	dokan_operations.SetFileAttributes = FsSetFileAttributes;
	dokan_operations.SetFileTime = FsSetFileTime;
	dokan_operations.DeleteFile = FsDeleteFile;
	dokan_operations.DeleteDirectory = FsDeleteDirectory;
	dokan_operations.MoveFile = FsMoveFile;
	dokan_operations.SetEndOfFile = FsSetEndOfFile;
	dokan_operations.SetAllocationSize = FsSetAllocationSize;
	dokan_operations.LockFile = FsLockFile;
	dokan_operations.UnlockFile = FsUnlockFile;
	dokan_operations.GetDiskFreeSpace = FsGetDiskFreeSpace;
	dokan_operations.GetVolumeInformation = FsGetVolumeInformation;
	dokan_operations.Mounted = FsMounted;
	dokan_operations.Unmounted = FsUnmounted;
	dokan_operations.GetFileSecurity = FsGetFileSecurity;
	dokan_operations.SetFileSecurity = FsSetFileSecurity;
	dokan_operations.FindStreams = FsFindStreams;


	DokanInit();

	int status = DokanCreateFileSystem(&dokan_options, &dokan_operations, &DokanInstance);

	switch (status) {
	case DOKAN_SUCCESS:
		break;
	case DOKAN_ERROR:
		FsLogger::LogFormat(FilesystemLogType::Error, "DokanMain failed with %u", status);
		return -1;
	case DOKAN_DRIVE_LETTER_ERROR:
		FsLogger::LogFormat(FilesystemLogType::Error, "Bad Drive letter");
		return -1;
	case DOKAN_DRIVER_INSTALL_ERROR:
		FsLogger::LogFormat(FilesystemLogType::Error, "Can't install driver");
		return -1;
	case DOKAN_START_ERROR:
		FsLogger::LogFormat(FilesystemLogType::Error, "Driver something wrong");
		return -1;
	case DOKAN_MOUNT_ERROR:
		FsLogger::LogFormat(FilesystemLogType::Error, "Can't assign a drive letter");
		return -1;
	case DOKAN_MOUNT_POINT_ERROR:
		FsLogger::LogFormat(FilesystemLogType::Error, "Mount point error");
		return -1;
	case DOKAN_VERSION_ERROR:
		FsLogger::LogFormat(FilesystemLogType::Error, "Version error");
		return -1;
	default:
		FsLogger::LogFormat(FilesystemLogType::Error, "Unknown error: %u", status);
		return -1;
	}

	DokanWaitForFileSystemClosed(DokanInstance, INFINITE);

	DokanCloseHandle(DokanInstance);

	DokanRemoveMountPoint(L"Y:");

	DokanShutdown();

	return 0;

}