# BarebonesFilesystem

BarebonesFilesystem is a lightweight filesystem library made in c++, designed to provide a basic implementation of a custom file systems. It uses ZERO external libraries and dependencies, including the c++ standard libary. This makes it perfect for usage in any environment where the availability of these external libraries may be limited.
Setting up the filesystem in your own project requires overriding only 5 virtual functions.

## Project Structure

- **FsLib/**: Contains the core source code of the filesystem library. This code is intended to be compiled into a static library (`FsLib.lib`) for use in other programs.
  
- **WindowsImpl/**: Contains the Windows-specific implementation of the filesystem library. This uses the Dokan library to interact with the Windows filesystem and mount a virtual filesystem.

## Getting Started

### Prerequisites

- **CMake**: Used for generating the necessary project files.

### Optional: 
- **Visual Studio**: The windows implementation of this project is set up to work with Visual Studio.
- **Dokan Library**: The windows implementation of this project relies on the Dokan library for Windows filesystem operations. You can download and install it from [here](https://dokan-dev.github.io/).

### Building the Windows Implementation

1. **Clone the repository:**
```bash
   git clone https://github.com/Absurdponcho/BarebonesFilesystem.git
   cd BarebonesFilesystem
```
2. Generate project files using CMake:

In the root directory, run the following command:

```bash
cmake CMakeLists.txt
```
This will generate the project files for Visual Studio.

In Visual Studio, right-click on `FilesystemTest` and select `Set as Startup Project`

3. Build and run:

Once the startup project is set, simply press the Play button in Visual Studio.
This will build the FsLib static library and link it with the FilesystemTest project, which will then mount a virtual filesystem using the Dokan library.

4. Usage

Once built, the FilesystemTest application will mount a virtual filesystem on Windows, allowing you to interact with it just like any other filesystem. The behavior and structure of the virtual filesystem can be customized by modifying the FsLib library.
View the mount point in Windows Explorer. It should be usable just like any other mounted filesystem.

## Implementing in your own code
The filesystem can be implemented by overriding 5 virtual functions. See the Windows Implementation for reference.

The 5 virtual functions are across 3 classes:

`virtual FilesystemReadResult FsFilesystem::Read(uint64 Offset, uint64 Length, uint8* Destination)` <br>
  Should be implemented to read from your storage device at the specified absolute offset and byte length. The bytes should be moved into the `Destination` buffer. Not Optional.

`virtual FilesystemWriteResult FsFilesystem::Write(uint64 Offset, uint64 Length, const uint8* Source)`  <br>
  Should be implemented to write to your storage device at the specified absolute offset and byte length. The bytes should be copied from the `Source` buffer. Not Optional.

`virtual void FsLogger::OutputLog(const char* String, FilesystemLogType LogType)` <br>
  Can be implemented to display logging from the filesystem into your desired output, such as on to the screen or into a buffer. It is optional.

`virtual void* FsMemoryAllocator::Allocate(uint64 Size)`  <br>
  Should be implemented to provide a chunk of memory of `Size` bytes. Identical to `malloc`. Not Optional.

`virtual void FsMemoryAllocator::Free(void* Memory)`  <br>
  Should be implemented to free a chunk of memory allocated by `FsMemoryAllocator::Allocate`. Identical to `free`. Not Optional.


Once these functions are overridden implemented in derived classes, create instances of these derived classes. 
Example:
```cpp
int main()
{
  FsLoggerImpl Logger = FsLoggerImpl(); // Create your custom FsLogger derived class
  FsMemoryAllocatorImpl Allocator = FsMemoryAllocatorImpl(); // Create your custom FsMemoryAllocator derived class
  
  // Example implementation with a 4GB partition and 128KB block size.
  // In your own implementation, you can do additional setup in this class such as telling it which storage device it must access.
  // If you have a storage device that is 500GB in size, then create a partition of size 500GB.
  // The block size can be any multiple of 1024. It's recommended to be between 8KB and 128KB at the moment for performance.
  // You cannot change the partition size and block size once the filesystem has been initialized. If this is done, the filesystem will become corrupted and will need reformatting.
  // You can have multiple instances of FsFilesystem for different storage devices, if desired.
  FsFilesystemImpl FsFilesystem = FsFilesystemImpl(1024ull * 1024ull * 1024ull * 4ull, 1024 * 128);
  
  // ... Do custom initialization code for your derived class here
  
  // Call Initialize to start the filesystem.
  FsFilesystem.Initialize();
  
  // Do filesystem things
  FsFilesystem.CreateFile("OhWow.txt");
}
```

### Filesystem Functions
Once your filesystem is initialized, you are given many functions to operate the filesystem. These are found in `Filesystem.h`
```cpp
// Creates a file with a given name. Requires the parent directories to already exist. Eg: /Foo/Bar/Test.txt
bool CreateFile(const FsPath& FileName);

// Checks if the file with the given name exists
bool FileExists(const FsPath& InFileName); 

// Creates a directory with a given name. Eg: /Foo/Bar/Baz
bool CreateDirectory(const FsPath& InDirectoryName);

// Writes to the file at the given path, at the given offset and length. If the offset and length are beyond the length of the file, the file will be extended.
bool WriteToFile(const FsPath& InPath, const uint8* Source, uint64 InOffset, uint64 InLength);

// Reads from the file at the given path, at the given Offset and Length. It will fail if trying to read beyond the length of the file, so check file size first.
bool ReadFromFile(const FsPath& InPath, uint64 Offset, uint8* Destination, uint64 Length, uint64* OutBytesRead = nullptr);

// Deletes the directory at the given path. The directory must be empty
bool FsDeleteDirectory(const FsPath& DirectoryName);

// Checks if the directory at the given path is empty. The directory must exist at the path.
bool FsIsDirectoryEmpty(const FsPath& DirectoryName);

// Deletes a file at the given path.
bool FsDeleteFile(const FsPath& FileName);

// Moves a file from the given path to a destination path. There must be no file or directory existing at the destination.
bool FsMoveFile(const FsPath& SourceFileName, const FsPath& DestinationFileName);

// Not implemented
bool CopyFile(const FsPath& SourceFileName, const FsPath& DestinationFileName);

// Gets the directory descriptor at the given path. Can be used to iterate its files.
bool GetDirectory(const FsPath& InDirectoryName, FsDirectoryDescriptor& OutDirectoryDescriptor, FsFileDescriptor* OutDirectoryFile = nullptr);

// Checks if a directory exists at a given path.
bool DirectoryExists(const FsPath& InDirectoryName);

// Gets a file descriptor at a given path. Can be used to check file size and absolute offset of the file.
bool GetFile(const FsPath& InFileName, FsFileDescriptor& OutFileDescriptor);

// Gets the file size of a file at the given path.
bool GetFileSize(const FsPath& InFileName, uint64& OutFileSize);

// Gets the total and free bytes of the whole partition this filesystem implementation was assigned to.
bool GetTotalAndFreeBytes(uint64& OutTotalBytes, uint64& OutFreeBytes);
```

### License
This project is licensed under the MIT License. See the LICENSE file for details.

### Contributing
Contributions are welcome! Feel free to submit issues or pull requests.

### Acknowledgments
Dokan Library for making it easier to work with filesystems on Windows.
