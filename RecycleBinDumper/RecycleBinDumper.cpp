// RecycleBinDumper.cpp
//
// Utility to dump information about files and folders (i.e. directories) in the Windows Recycle Bin.
//
// When a file or folder is deleted in Windows, it is actually moved to a special folder known
// as the Recycle Bin and additional information is also saved there so that it can be restored
// if it was deleted by mistake.
//
// Because the file or folder is simply moved to the Recycle Bin, there actually needs to be
// a Recycle Bin on each logical disk drive (i.e. for each drive letter).
// In fact each user has there own Recycle Bin on each logical disk drive.
//
// The Recycle Bin is a folder with a name in the form:
//     C:\$Recycle.Bin\S-1-5-21-1851798247-1933540348-1582327844-1001
//         where C: may be any drive letter on the machine
//         and where the string starting S-1-5... is the SID of the user.
//
// When a file or folder is moved to the Recycle Bin, it is renamed with a name of the form:
//     $RXXXXXX - where XXXXXXX is six random upper case letters or numbers.
//
// A separate file with the same name except starting with $I instead of $R is created
// alongside the original file.
//
// This file is a binary format file that contains the additional information about when the
// file or folder was deleted, the full path to the deleted file or folder's original location,
// and the size of the file or folder when it was deleted.
//
// There are two versions of this file format.
//
//   Version 1: Prior to Windows 10
//     uint64_t version;        // 8 bytes - 1 for this version
//     uint64_t size;    	    // 8 bytes - The size in bytes of the original file or folder
//     FILETIME deletedTime;    // 8 bytes - The date/time the file or folder was deleted
//     wchar_t fileName[260];   // 520 bytes - The full path of the original file or folder.
//
//   Version 2: Windows 10
//     uint64_t version;        // 8 bytes - 2 for this version
//     uint64_t size;    	    // 8 bytes - The size in bytes of the original file or folder
//     FILETIME deletedTime;    // 8 bytes - The date/time the file or folder was deleted
//     uint32_t fileNameLen;    // 4 bytes - The length of the fileName that follows.
//     wchar_t fileName[];      // variable length - The full path of the original file or folder.
//
// RecycleBinDumper dumps out information about all the files and folders located in the
// Recycle Bin passed as a command line argument.
//
// The output is to stdout and can be redirected to a file on the command line.
// The output is in csv (comma separated values) format and can be loaded directly
// into Excel for display and analysis.
// A single row is output for each file and folder in the recycle bin (recursively into
// each folder and subfolder so everything is listed).
//
// Because files and folders in deleted folders share the same information about when the folder
// was deleted, that information is repeated on each row for those files and folders.
//
// For deleted files, the value under "Deleted Size" should equal the value under "Original File Size".
// For a particular deleted folder, the sum of all the values under "Original File Size" should
// equal the value under "Deleted Size".

#include "windows.h"
#include "stdio.h"
#include "cstdint"
#include "strsafe.h"

// Helper class to buffer line output.
class CharBuffer
	{
	public:
		CharBuffer(size_t size)
			{
			this->buffer = new wchar_t[size];
			this->size = size;
			this->position = 0;
			}

		~CharBuffer()
			{
			delete this->buffer;
			}

		void PrintLine()
			{
			wprintf(L"%s\n", buffer);
			}

		size_t PrintF(const wchar_t* format...)
			{
			va_list args;
			va_start(args, format);

			this->position += vswprintf_s(this->buffer + this->position, this->size - this->position, format, args);

			return this->position;
			}

		size_t GetPosition()
			{
			return this->position;
			}

		void  SetPosition(size_t position)
			{
			this->position = position;
			}

		wchar_t* buffer;

	protected:
		size_t size;
		size_t position;
	};

typedef void (*EachFileHandler)(const wchar_t *szRoot, WIN32_FIND_DATA* pffd, CharBuffer *lineBuffer);
void ForeachFile(const wchar_t* szRoot, const wchar_t* szWild, EachFileHandler fn, CharBuffer *lineBuffer);

// PrintRecycledFileInfo is an EachFileHandler (i.e. called from ForeachFile())
void PrintRecycledFileInfo(const wchar_t* szRoot, WIN32_FIND_DATA* pffd, CharBuffer *lineBuffer);

void PrintRecycleInfo(CharBuffer *lineBuffer, const wchar_t* szFileName);
void PrintFileAttributes(CharBuffer *lineBuffer, const wchar_t* szFullPath, bool *pfFolder);
void PrintFileDetails(CharBuffer *lineBuffer, const wchar_t* szFileName, FILETIME* pFileTimeCreated, FILETIME* pFileTimeModified, FILETIME* pFileTimeAccessed);
void PrintFileTime(CharBuffer *lineBuffer, FILETIME* pFileTime, bool comma = true);

// Recursively print out the folder
void PrintFolder(const wchar_t* szFolder, CharBuffer *lineBuffer);

// PrintFileOrFolder is an EachFileHandler (i.e. called from ForeachFile())
void PrintFileOrFolder(const wchar_t * szRoot, WIN32_FIND_DATA* pffd, CharBuffer *lineBuffer);

wchar_t header[] =
	L"Original Full Path,"
	L"Deleted Date Time,"
	L"Deleted File Size,"
	L"Recycle Info File,"
	L"Recycle Info Created,"
	L"Recycle Info Last Modified,"
	L"Recycle Info Last Accessed,"
	L"Original File,"
	L"Original File Created,"
	L"Original File Last Modified,"
	L"Original File Last Accessed,"
	L"Original File Size,"
	;

int __cdecl wmain(int argc, const wchar_t** argv)
	{
	CharBuffer* lineBuffer = new CharBuffer(2 * 1024);

	for (int i = 1; i < argc; i++)
		{
		wprintf(L"%s\n", header);
		SetCurrentDirectory(argv[i]);

		// Look for the Recycle Bin information files.
		ForeachFile(L".", L"$I*", PrintRecycledFileInfo, lineBuffer);
		}

	delete lineBuffer;
	}

void ForeachFile(const wchar_t *szRoot, const wchar_t* szWild, EachFileHandler fn, CharBuffer *lineBuffer)
	{
	WIN32_FIND_DATA ffd;
	HANDLE hFind = INVALID_HANDLE_VALUE;
	CharBuffer* findPattern = new CharBuffer(MAX_PATH);

	findPattern->PrintF(L"%s\\%s", szRoot, szWild);

	size_t initialPosition = lineBuffer->GetPosition();
	hFind = FindFirstFile(findPattern->buffer, &ffd);

	if (hFind != INVALID_HANDLE_VALUE)
		{
		do
			{
			bool skip = false;

			if (ffd.cFileName[0] == L'.')
				{
				skip = (ffd.cFileName[1] == L'\0')
					|| ((ffd.cFileName[1] == L'.') && (ffd.cFileName[2] == L'\0'));
				}

			if (!skip)
				{
				lineBuffer->SetPosition(initialPosition);
				fn(szRoot, &ffd, lineBuffer);
				}
			else
				{

				}
			} while (FindNextFile(hFind, &ffd) != 0);
		FindClose(hFind);
		}
	}

void PrintRecycledFileInfo(const wchar_t* szRoot, WIN32_FIND_DATA* pffd, CharBuffer *lineBuffer)
	{
	if (pffd->dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
		{
		}
	else
		{
		PrintRecycleInfo(lineBuffer, pffd->cFileName);
		PrintFileDetails(lineBuffer, pffd->cFileName, &(pffd->ftCreationTime), &(pffd->ftLastWriteTime), &(pffd->ftLastAccessTime));

		wchar_t szDataFile[MAX_PATH];

		// Data file is the same as the recycle info file except it starts with "$R" instead of "$I".
		StringCchCopy(szDataFile, MAX_PATH, pffd->cFileName);
		szDataFile[1] = L'R';

		bool isFolder = false;
		size_t pos = lineBuffer->GetPosition();
		PrintFileAttributes(lineBuffer, szDataFile, &isFolder);

		lineBuffer->PrintLine();

		if (isFolder)
			{
			// Everything before pos is repeated for all the files and folders under this folder.
			lineBuffer->SetPosition(pos);
			PrintFolder(szDataFile, lineBuffer);
			}
		}
	}

void PrintRecycleInfo(CharBuffer *lineBuffer, const wchar_t* szFileName)
	{
	FILE* pFile;
	errno_t err = _wfopen_s(&pFile, szFileName, L"rb");

	if (err == 0)
		{
		uint64_t version;
		size_t count = fread(&version, sizeof(version), 1, pFile);
		if (count == 1)
			{
			uint64_t fileSize;
			count = fread(&fileSize, sizeof(fileSize), 1, pFile);
			if (count == 1)
				{
				FILETIME timeStamp;

				count = fread(&timeStamp, sizeof(timeStamp), 1, pFile);
				if (count == 1)
					{
					uint32_t fileNameSize = 0;

					if (version == 1)
						{
						fileNameSize = 520 / sizeof(wchar_t);
						}
					else
						{
						count = fread(&fileNameSize, sizeof(fileNameSize), 1, pFile);
						if (count != 1)
							{
							fileNameSize = 0;
							}
						}

					if (fileNameSize > 0)
						{
						wchar_t* pOriginalFileName = new wchar_t[fileNameSize + 1];
						count = fread(pOriginalFileName, sizeof(wchar_t), fileNameSize, pFile);
						if (count == fileNameSize)
							{
							pOriginalFileName[fileNameSize] = L'\0';

							lineBuffer->PrintF(L"%s,", pOriginalFileName);
							PrintFileTime(lineBuffer, &timeStamp);
							lineBuffer->PrintF(L"%lld,", fileSize);
							}

						delete[] pOriginalFileName;
						}
					}
				}
			}

		fclose(pFile);
		}
	}

void PrintFileAttributes(CharBuffer *lineBuffer, const wchar_t* szFileName, bool *pIsFolder)
	{
	WIN32_FILE_ATTRIBUTE_DATA fileAttributeData;

	int err = GetFileAttributesEx(szFileName, GetFileExInfoStandard, &fileAttributeData);
	if (err == 0)
		{
		*pIsFolder = false;
		lineBuffer->PrintF(L"Missing,,,,,");
		return;
		}

	PrintFileDetails(lineBuffer, szFileName, &(fileAttributeData.ftCreationTime), &(fileAttributeData.ftLastWriteTime), &(fileAttributeData.ftLastAccessTime));
	uint64_t size = (((uint64_t)fileAttributeData.nFileSizeHigh) << 32) + fileAttributeData.nFileSizeLow;
	lineBuffer->PrintF(L"%lld,", size);

	*pIsFolder = (fileAttributeData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
	}

void PrintFileDetails(CharBuffer *lineBuffer, const wchar_t* szFileName, FILETIME* pFileTimeCreated, FILETIME* pFileTimeModified, FILETIME* pFileTimeAccessed)
	{
	lineBuffer->PrintF(L"%s,", szFileName);
	PrintFileTime(lineBuffer, pFileTimeCreated);
	PrintFileTime(lineBuffer, pFileTimeModified);
	PrintFileTime(lineBuffer, pFileTimeAccessed);
	}


void PrintFileTime(CharBuffer *lineBuffer, FILETIME *pFileTime, bool comma)
	{
	SYSTEMTIME utc;
	FileTimeToSystemTime(pFileTime, &utc);

	lineBuffer->PrintF(L"%4d-%02d-%02d %02d:%02d:%02d",
		utc.wYear, utc.wMonth, utc.wDay, utc.wHour, utc.wMinute, utc.wSecond);

	if (comma)
		{
		lineBuffer->PrintF(L",");
		}
	}

void PrintFolder(const wchar_t* szFolder, CharBuffer *lineBuffer)
	{
	ForeachFile(szFolder, L"*", PrintFileOrFolder, lineBuffer);
	}

void PrintFileOrFolder(const wchar_t * szRoot, WIN32_FIND_DATA* pffd, CharBuffer *lineBuffer)
	{
	size_t initialPosition = lineBuffer->GetPosition();

	CharBuffer* fileName = new CharBuffer(MAX_PATH);

	fileName->PrintF(L"%s\\%s", szRoot, pffd->cFileName);

	PrintFileDetails(lineBuffer, fileName->buffer, &(pffd->ftCreationTime), &(pffd->ftLastWriteTime), &(pffd->ftLastAccessTime));
	delete fileName;
	fileName = NULL;

	uint64_t size = (((uint64_t)pffd->nFileSizeHigh) << 32) + pffd->nFileSizeLow;
	lineBuffer->PrintF(L"%lld,", size);

	lineBuffer->PrintLine();

	if ((pffd->dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
		{
		lineBuffer->SetPosition(initialPosition);
		PrintFolder(fileName->buffer, lineBuffer);
		}
	}
