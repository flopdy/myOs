#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/UefiLib.h>
#include <Library/UefiApplicationEntryPoint.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/MemoryAllocationLib.h>

#include <Protocol/GraphicsOutput.h>
#include <Protocol/LoadedImage.h>
#include <Protocol/SimpleFileSystem.h>

#include <Guid/FileInfo.h>


// to build:
/*
cd ~/repos/boot/edk2
source edksetup.sh
build
*/

// the output of the build goes to ~/repos/boot/edk2/Build/MdeModule/DEBUG_GCC/X64/MdeModulePkg/Application/myOs/myOs/OUTPUT
// look for 'myBootLoader.efi'

// ~/repos/boot/run.sh
// running the above bash script will copy the efi to:
// ~/repos/boot/USB/EFI/BOOT/BOOTX64.EFI
// and run qemu, booting from that 'pretend' USB

VOID* readFile(EFI_FILE_PROTOCOL* Root, CHAR16* location, UINTN* size)
{
    // open file
    EFI_FILE_PROTOCOL* File;
    EFI_STATUS Status = Root->Open(
        Root, &File,
        location,
        EFI_FILE_MODE_READ,
        0
    );
    if (EFI_ERROR(Status))
    {
        return NULL;
    }

    // first get all of the info about the file
    // get infoSize
    UINTN FileInfoSize = 0;
    Status = File->GetInfo(
        File,
        &gEfiFileInfoGuid,
        &FileInfoSize,
        NULL
    );
    if (Status != EFI_BUFFER_TOO_SMALL)
    {
        File->Close(File);
        return NULL;
    }

    // allocate memory for info
    EFI_FILE_INFO* FileInfo;
    Status = gBS->AllocatePool(
        EfiLoaderData,
        FileInfoSize,
        (VOID**)&FileInfo
    );
    if (EFI_ERROR(Status))
    {
        File->Close(File);
        return NULL;
    }

    // get fileInfo
    Status = File->GetInfo(
        File,
        &gEfiFileInfoGuid,
        &FileInfoSize,
        FileInfo
    );
    if (EFI_ERROR(Status))
    {
        gBS->FreePool(FileInfo);
        File->Close(File);
        return NULL;
    }

    // now we have the info about the file, load its contents
    UINTN DataSize = FileInfo->FileSize;
    gBS->FreePool(FileInfo); // free the info
    VOID* fileData;
    // allocate memory for data
    Status = gBS->AllocatePool(
        EfiLoaderData,
        DataSize,
        &fileData
    );
    if(EFI_ERROR(Status))
    {
        File->Close(File);
        return NULL;
    }

    // get the file data
    Status = File->Read(
        File,
        &DataSize,
        fileData
    );

    File->Close(File);

    if(EFI_ERROR(Status))
    {
        gBS->FreePool(fileData);
        return NULL;
    }    

    *size = DataSize;
    return fileData;
}

EFI_STATUS writeFile(EFI_FILE_PROTOCOL* Root, CHAR16* location, VOID* data, UINTN size)
{
    EFI_FILE_PROTOCOL* File;

    EFI_STATUS status = Root->Open(
        Root,
        &File,
        location,
        EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE, // bitwise or, all file perms
        0
    );
    if (EFI_ERROR(status))
    {
        return status;
    }

    // get infoSize
    UINTN FileInfoSize = 0;
    status = File->GetInfo(
        File,
        &gEfiFileInfoGuid,
        &FileInfoSize,
        NULL
    );
    if (status != EFI_BUFFER_TOO_SMALL)
    {
        File->Close(File);
        return status;
    }

    // allocate memory for info
    EFI_FILE_INFO* FileInfo;
    status = gBS->AllocatePool(
        EfiLoaderData,
        FileInfoSize,
        (VOID**)&FileInfo
    );
    if (EFI_ERROR(status))
    {
        File->Close(File);
        return status;
    }

    // get fileInfo
    status = File->GetInfo(
        File,
        &gEfiFileInfoGuid,
        &FileInfoSize,
        FileInfo
    );
    if (EFI_ERROR(status))
    {
        gBS->FreePool(FileInfo);
        File->Close(File);
        return status;
    }
    // set size to zero
    FileInfo->FileSize = 0;
    status = File->SetInfo(
        File,
        &gEfiFileInfoGuid,
        FileInfoSize,
        FileInfo
    );
    if (EFI_ERROR(status))
    {
        gBS->FreePool(FileInfo);
        File->Close(File);
        return status;
    }
    gBS->FreePool(FileInfo);

    File->SetPosition(File, 0);

    UINTN dataSize = size;
    status = File->Write(
        File,
        &dataSize,
        data
    );
    if (!EFI_ERROR(status))
    {
        status = File->Flush(File);
    }

    File->Close(File);

    return status;
}

// globals
EFI_SYSTEM_TABLE* gSystemTable;
BOOLEAN running;
EFI_FILE_PROTOCOL* gRoot;
EFI_FILE_PROTOCOL* gCurrentDirectory;
// CHAR16 gCurrentPath[1024] = L"\\";

void PrintCursor(void)
{
    Print(L"\u2588");
}

BOOLEAN searchStrArray(UINTN count, CHAR16** strArray, CHAR16* search)
{
    for (UINTN i = 0; i < count; i++)
    {
        if(StrCmp(strArray[i], search) == 0)
        {
            return TRUE;
        }
    }
    return FALSE;
}

typedef struct
{
    CHAR16* name;
    void (*function)(CHAR16* args);
    BOOLEAN takesArgs;
} command;

void hello(CHAR16* args)
{
    Print(L"\r\nhello world!");
}

void clear(CHAR16* args)
{
    gSystemTable->ConOut->ClearScreen(gSystemTable->ConOut);
}

void exit(CHAR16* args)
{
    running = FALSE;
}

void reboot(CHAR16* args)
{
    gSystemTable->RuntimeServices->ResetSystem(
        EfiResetCold,
        EFI_SUCCESS,
        0,
        NULL
    );
}

void shutdown(CHAR16* args)
{
    gSystemTable->RuntimeServices->ResetSystem(
        EfiResetShutdown,
        EFI_SUCCESS,
        0,
        NULL
    );
}

void echo(CHAR16* args)
{
    Print(L"\r\n");
    Print(args);
}

EFI_STATUS list()
{
    gCurrentDirectory->SetPosition(gCurrentDirectory, 0);
    // get every file
    UINT8 buffer[2048];
    UINTN bufferSize;

    while(TRUE)
    {
        bufferSize = sizeof(buffer);
        EFI_STATUS status =
        gCurrentDirectory->Read(
            gCurrentDirectory,
            &bufferSize,
            buffer
        );

        if (!EFI_ERROR(status) && bufferSize != 0)
        {
            EFI_FILE_INFO* info = (EFI_FILE_INFO*)buffer;
            Print(L"\r\n");
            Print(L"%s", info->FileName);
        }
        else
        {
            return status;
        }
    }
    gCurrentDirectory->SetPosition(gCurrentDirectory, 0);
}

void ls(CHAR16* args)
{
    list();
}

EFI_STATUS changeDirectory(CHAR16* location)
{
    EFI_FILE_PROTOCOL* nd;

    EFI_STATUS status = gCurrentDirectory->Open(
        gCurrentDirectory,
        &nd,
        location,
        EFI_FILE_MODE_READ,
        0
    );

    if (EFI_ERROR(status))
    {
        Print(L"\ndirectory: ");
        Print(location);
        Print(L" could not be opened.");
        
        return status;
    }


    // get infoSize
    UINTN FileInfoSize = 0;
    status = nd->GetInfo(
        nd,
        &gEfiFileInfoGuid,
        &FileInfoSize,
        NULL
    );
    if (status != EFI_BUFFER_TOO_SMALL)
    {
        nd->Close(nd);
        return status;
    }
    // allocate memory for info
    EFI_FILE_INFO* ndInfo;
    status = gBS->AllocatePool(
        EfiLoaderData,
        FileInfoSize,
        (VOID**)&ndInfo
    );
    if (EFI_ERROR(status))
    {
        // allocation failed
        nd->Close(nd);
        
        Print(L"\ncould not allocate memory");        
        return status;
    }

    // get fileInfo
    status = nd->GetInfo(
        nd,
        &gEfiFileInfoGuid,
        &FileInfoSize,
        ndInfo
    );
    if (EFI_ERROR(status))
    {
        // getInfo failed
        gBS->FreePool(ndInfo);
        nd->Close(nd);

        Print(L"\ncould not get file info");
        return status;
    }

    if (!(ndInfo->Attribute & EFI_FILE_DIRECTORY))
    {
        // file is not a dir
        gBS->FreePool(ndInfo);
        nd->Close(nd);

        Print(L"\nfile: ");
        Print(location);
        Print(L" is not a directory.");
        
        return EFI_ACCESS_DENIED;
    }

    EFI_FILE_PROTOCOL* oldDir = gCurrentDirectory;
    gCurrentDirectory = nd;
    oldDir->Close(oldDir);
    gBS->FreePool(ndInfo);
    return EFI_SUCCESS;
}

void cd(CHAR16* args)
{
    changeDirectory(args);
}

void cdls(CHAR16* args)
{
    EFI_STATUS status = changeDirectory(args);
    if (!EFI_ERROR(status))
    {
        list(args);
    }
}

BOOLEAN fileProtected(CHAR16* name)
{
    CHAR16* protectedFiles[] = 
    {
        L"ASSETS",
        L"EFI",
        L"BOOT",
        L"BOOTX64.EFI"
    };

    UINTN count = sizeof(protectedFiles) / sizeof(CHAR16*);

    for (UINTN i = 0; i < count; i++)
    {
        if (StrCmp(name, protectedFiles[i]) == 0)
        {
            return TRUE;
        }
    }

    return FALSE;
}

CHAR16* loadTxt(EFI_FILE_PROTOCOL* Root, CHAR16* location, UINTN* size)
{
    VOID* data = readFile(Root, location, size);

    if (data == NULL)
    {
        Print(L"\r\n");
        Print(L"could not open file: ");
        Print(location);
        return NULL;
    }

    CHAR16* text = AllocatePool(*size + sizeof(CHAR16));

    if (text == NULL)
    {
        Print(L"\r\n");
        Print(L"could not allocate memory for file: ");
        Print(location);

        gBS->FreePool(data);
        return NULL;
    }

    CopyMem(text, data, *size);
    text[*size / sizeof(CHAR16)] = L'\0'; // add a termination character

    gBS->FreePool(data);
    return text;
}

void cat(CHAR16* args)
{
    if (fileProtected(args))
    {
        Print(L"\r\naccess denied.");
        return;
    }

    UINTN size;
    CHAR16* text = loadTxt(gCurrentDirectory, args, &size);

    if (text == NULL)
    {
        return;
    }

    Print(L"\r\n");
    Print(text);

    gBS->FreePool(text);    
}

void touch(CHAR16* args)
{
    if (args == NULL || *args == L'\0')
    {
        Print(L"\r\nprovide a filename.");
        return;
    }
    if (fileProtected(args))
    {
        Print(L"\r\naccess denied.");
        return;
    }
    if (StrCmp(args, L".") == 0 || StrCmp(args, L"..") == 0)
    {
        Print(L"\r\ncan't touch cd or pd.");
        return;
    }

    CHAR16 text[] = L"";
    EFI_STATUS status = writeFile(gCurrentDirectory, args, text, StrLen(text) * sizeof(CHAR16));

    if (EFI_ERROR(status))
    {
        Print(L"\r\n");
        Print(L"file: ");
        Print(args);
        Print(L" could not be written to.");
        Print(L"\r\n");
        Print(L"%r", status);
    }
}

void edit(CHAR16* args)
{
    if (args == NULL || *args == L'\0')
    {
        Print(L"\r\nprovide a filename.");
        return;
    }

    if (fileProtected(args))
    {
        Print(L"\r\naccess denied.");
        return;
    }

    if (StrCmp(args, L".") == 0 || StrCmp(args, L"..") == 0)
    {
        Print(L"\r\ncan't edit cd or pd.");
        return;
    }

    CHAR16 editBuffer[2048] = L"";
    UINTN editSize = sizeof(editBuffer)/sizeof(CHAR16);
    UINTN editLength = StrLen(editBuffer);

    UINTN cursorPosition = 0;

    UINTN cursorX = 0;
    UINTN cursorY = 0;

    BOOLEAN cursorVisible = FALSE;
    UINTN cursorTicks = 0;
    UINTN cursorTickPeriod = 15;

    BOOLEAN firstFrame = TRUE;
    BOOLEAN inputThisFrame = FALSE;
    BOOLEAN cursorChange = FALSE;

    BOOLEAN editing = TRUE;
    BOOLEAN exitMenu = FALSE;
    
    while(editing)
    {
        if (exitMenu)
        {
            EFI_INPUT_KEY key;
            while(!EFI_ERROR(gSystemTable->ConIn->ReadKeyStroke(gSystemTable->ConIn, &key)))
            {
                CHAR16 uc = key.UnicodeChar;
                if (uc == L'c')
                {
                    exitMenu = FALSE;
                    firstFrame = TRUE;
                }
                else if (uc == L'd')
                {
                    editing = FALSE;
                }
                else if (uc == L's')
                {
                    EFI_STATUS status = writeFile(gCurrentDirectory, args, editBuffer, editLength * sizeof(CHAR16));

                    if (EFI_ERROR(status))
                    {
                        Print(L"\r\nsave failed: %r", status);
                        continue;
                    }
                    editing = FALSE;
                }
            }
        }
        else {
        cursorChange = cursorTicks >= cursorTickPeriod;
        if (cursorChange)
        {
            cursorTicks = 0;
            cursorVisible = !cursorVisible;
        }

        inputThisFrame = FALSE;
        EFI_INPUT_KEY key;
        while(!EFI_ERROR(gSystemTable->ConIn->ReadKeyStroke(gSystemTable->ConIn, &key)))
        {
            inputThisFrame = TRUE;

            CHAR16 c = key.UnicodeChar;
            
            if (c == L'\b')
            {
                // backspace
                if (cursorPosition > 0)
                {
                    cursorPosition--;

                    if (editBuffer[cursorPosition] == L'\n')
                    {
                        UINTN x = 0;

                        for (INTN i = (INTN)cursorPosition - 1; i >= 0; i--)
                        {
                            if (editBuffer[i] == L'\n')
                            {
                                break;
                            }

                            x++;
                        }

                        cursorX = x;
                        cursorY--;
                    }
                    else
                    {
                        cursorX--;
                    }

                    for (UINTN i = cursorPosition; i < editLength; i++)
                    {
                        editBuffer[i] = editBuffer[i+1];
                    }
                }
            }
            else if (c == L'\r')
            {
                // on enter

                for (INTN i = (INTN)editLength; i >= (INTN)cursorPosition; i--)
                {
                    editBuffer[i+1] = editBuffer[i];
                }

                editBuffer[cursorPosition] = L'\n';
                cursorPosition++;
                
                cursorY++;
                cursorX = 0;
            }
            else if (key.ScanCode != 0)
            {
                UINTN s = key.ScanCode;

                if (s == SCAN_ESC)
                {
                    clear(L"");
                    exitMenu = TRUE;

                    gST->ConOut->SetCursorPosition(gST->ConOut, 0, 0);
                    Print(L"exit menu");
                    Print(L"\r\nc: cancel");
                    Print(L"\r\nd: discard");
                    Print(L"\r\ns: save");
                }
                else if (s == SCAN_LEFT)
                {
                    if (cursorPosition > 0)
                    {
                        cursorPosition--;

                        if (editBuffer[cursorPosition] == L'\n')
                        {
                            UINTN x = 0;

                            for (INTN i = (INTN)cursorPosition - 1; i >= 0; i--)
                            {
                                if (editBuffer[i] == L'\n')
                                {
                                    break;
                                }

                                x++;
                            }

                            cursorX = x;
                            cursorY--;
                        }
                        else
                        {
                            cursorX--;
                        }
                    }
                }
                else if (s == SCAN_RIGHT)
                {
                    if (cursorPosition < editLength)
                    {
                        if (editBuffer[cursorPosition] == L'\n')
                        {
                            cursorX = 0;
                            cursorY++;
                        }
                        else
                        {

                            cursorX++;
                        }
                        
                        cursorPosition++;
                    }
                }
            }
            else
            {
                // normal characters
                if (editLength < editSize - 1)
                {
                    for (INTN i = (INTN)editLength; i >= (INTN)cursorPosition; i--)
                    {
                        editBuffer[i+1] = editBuffer[i];
                    }

                    editBuffer[cursorPosition] = c;
                    cursorPosition++;

                    cursorX++;
                }
            }
        }
        editLength = StrLen(editBuffer);

        // render
        if (!exitMenu && (inputThisFrame || cursorChange || firstFrame)) // only if there was change
        {
            clear(L"");

            UINTN x = 0;
            UINTN y = 0;
            BOOLEAN cursorPrinted = FALSE;
            for (UINTN i = 0; i < editLength; i++)
            {
                if (x == cursorX && y == cursorY)
                {
                    if (cursorVisible)
                    {
                        PrintCursor();
                    }         
                    else
                    {
                        Print(L" ");
                    }           
                    cursorPrinted = TRUE;
                }

                if (editBuffer[i] == L'\n')
                {
                    Print(L"\r\n");
                }
                else
                {
                    Print(L"%c", editBuffer[i]);
                }

                if (editBuffer[i] == L'\n')
                {
                    x = 0;
                    y++;
                }
                else
                {
                    x++;
                }
            }
            if (!cursorPrinted)
            {
                if (cursorVisible)
                {
                    PrintCursor();
                }                 
                else
                {
                    Print(L" ");
                } 
            }
        }}

        // ~30hz (microseconds)
        gBS->Stall(33333);
        cursorTicks++;

        firstFrame = FALSE;
    }

    clear(L"");
}

void parseArgs(CHAR16* args, CHAR16** params, UINTN* count)
{
    UINTN argsLength = StrLen(args);

    // set every space to a break character
    *count = 0;
    UINTN nextParamChar = 0;
    for (UINTN i=0; i<argsLength; i++)
    {
        if (args[i] == L' ')
        {
            args[i] = L'\0';

            // enter last param into params
            params[*count] = &args[nextParamChar]; // from last char to current \0
            nextParamChar = i + 1;
            (*count)++;
        }
    }

    if (nextParamChar < argsLength)
    {
        params[*count] = &args[nextParamChar];
        (*count)++;
    }
}

EFI_STATUS removeFile(EFI_FILE_PROTOCOL* Root, CHAR16* location, BOOLEAN directoryDel)
{
    EFI_FILE_PROTOCOL* file;

    EFI_STATUS status = Root->Open(
        Root,
        &file,
        location,
        EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE,
        0
    );

    if (EFI_ERROR(status))
    {
        return status;
    }

    UINTN infoSize = 0;
    status = file->GetInfo(
        file,
        &gEfiFileInfoGuid,
        &infoSize,
        NULL
    );
    if (status != EFI_BUFFER_TOO_SMALL)
    {
        file->Close(file);
        return status;
    }
    
    EFI_FILE_INFO* info;
    status = gBS->AllocatePool(
        EfiLoaderData,
        infoSize,
        (VOID**)&info
    );
    if (EFI_ERROR(status))
    {
        file->Close(file);
        return status;
    }

    status = file->GetInfo(
        file,
        &gEfiFileInfoGuid,
        &infoSize,
        info
    );
    if (EFI_ERROR(status))
    {
        gBS->FreePool(info);
        file->Close(file);
        return status;
    }

    if ((!directoryDel) & info->Attribute & EFI_FILE_DIRECTORY)
    {
        gBS->FreePool(info);
        file->Close(file);
        return EFI_ACCESS_DENIED;
    }

    gBS->FreePool(info);
    return file->Delete(file);
}

void remove(CHAR16* args)
{
    CHAR16* params[256];
    UINTN paramCount;
    parseArgs(args, params, &paramCount);

    BOOLEAN directoryDel = searchStrArray(paramCount, params, L"-r");

    if (args == NULL || *args == L'\0')
    {
        Print(L"\r\nprovide a filename.");
        return;
    }

    if (fileProtected(args))
    {
        Print(L"\r\naccess denied.");
        return;
    }

    if (StrCmp(args, L".") == 0 || StrCmp(args, L"..") == 0)
    {
        Print(L"\r\ncan't delete cd or pd.");
        return;
    }

    EFI_STATUS status = removeFile(gCurrentDirectory, args, directoryDel);
    if (EFI_ERROR(status))
    {
        if (status == EFI_NOT_FOUND)
        {
            Print(L"\r\nfile: ");
            Print(args);
            Print(L" not found.");
        }
        else if (status == EFI_ACCESS_DENIED)
        {
            Print(L"\r\naccess denied.");
        }
        else
        {
            Print(L"\r\ncould not remove file: ");
            Print(args);
            Print(L"\r\n.");
        }
    }
}

EFI_STATUS createDirectory(EFI_FILE_PROTOCOL* Root, CHAR16* name)
{
    EFI_FILE_PROTOCOL* dir;

    EFI_STATUS status = Root->Open(
        Root,
        &dir,
        name,
        EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE,
        EFI_FILE_DIRECTORY
    );
    if (EFI_ERROR(status))
    {
        return status;
    }

    dir->Close(dir);
    return EFI_SUCCESS;
}

void mkdir(CHAR16* args)
{
    if (args == NULL || *args == L'\0')
    {
        Print(L"\r\nprovide a dir name.");
        return;
    }

    if (fileProtected(args))
    {
        Print(L"\r\naccess denied.");
        return;
    }

    if (StrCmp(args, L".") == 0 || StrCmp(args, L"..") == 0)
    {
        Print(L"\r\ncan't make dir cd or pd.");
        return;
    }

    EFI_STATUS status = createDirectory(gCurrentDirectory, args);

    if (EFI_ERROR(status))
    {
        Print(L"\r\ncould not make dir: ");
        Print(args);
    }
}

void help(CHAR16* args);

command commands[] =
{
    {L"hello", hello, FALSE},
    {L"help", help, FALSE},
    {L"clear", clear, FALSE},
    {L"cl", clear, FALSE},
    {L"exit", exit, FALSE},
    {L"reboot", reboot, FALSE},
    {L"shutdown", shutdown, FALSE},
    {L"echo", echo, TRUE},
    {L"list", ls, FALSE},
    {L"ls", ls, FALSE},
    {L"cd", cd, TRUE},
    {L"cdls", cdls, TRUE},
    {L"cat", cat, TRUE},
    {L"touch", touch, TRUE},
    {L"edit", edit, TRUE},
    {L"remove", remove, TRUE},
    {L"rm", remove, TRUE},
    {L"mkdir", mkdir, TRUE}
};

void help(CHAR16* args)
{
    Print(L"\navailable commands:");
    UINTN commandCount = sizeof(commands) / sizeof(commands[0]);
    for (UINTN i = 0; i < commandCount; i++)
    {
        Print(L"\n");
        Print(commands[i].name);
    }
}

// main
EFI_STATUS EFIAPI UefiMain(
    IN EFI_HANDLE ImageHandle,
    IN EFI_SYSTEM_TABLE* SystemTable
)
{
    // filesystem io
    // get info about the image (EFI) that is running
    EFI_LOADED_IMAGE_PROTOCOL* LoadedImage;
    gBS->HandleProtocol(
        ImageHandle,
        &gEfiLoadedImageProtocolGuid,
        (VOID**)&LoadedImage
    );
    // get the usb
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL* FileSystem;
    gBS->HandleProtocol(
        LoadedImage->DeviceHandle,
        &gEfiSimpleFileSystemProtocolGuid,
        (VOID**)&FileSystem
    );
    EFI_FILE_PROTOCOL* Root; // root directory
    FileSystem->OpenVolume(
        FileSystem,
        &Root
    );

    // set global
    gSystemTable = SystemTable;
    gRoot = Root;

    EFI_FILE_PROTOCOL* currentDirectory;
    EFI_STATUS status = gRoot->Open(
        gRoot,
        &currentDirectory,
        L"\\",
        EFI_FILE_MODE_READ,
        0
    );
    if (EFI_ERROR(status))
    {
        return EFI_SUCCESS;
    }
    gCurrentDirectory = currentDirectory;

    // clear screen
    SystemTable->ConOut->ClearScreen(SystemTable->ConOut);

    // main loop
    CHAR16 prompt[] = L"myOs> "; // intellisense complains, but this is valid
    Print(prompt);

    CHAR16 commandBuffer[256];
    commandBuffer[0] = L'\0';
    UINTN commandLength = 0;

    UINTN commandSize = sizeof(commands) / sizeof(commands[0]);

    BOOLEAN cursorVisible = FALSE;
    UINTN cursorTicks = 0;
    UINTN cursorTickPeriod = 15;

    running = TRUE;
    while(running)
    {
        // temporarily remove flashing cursor
        // so as not to intefere with
        // Print commands
        if (cursorVisible)
        {
            Print(L"\b \b");
        }

        if (cursorTicks >= cursorTickPeriod)
        {
            cursorTicks = 0;
            cursorVisible = !cursorVisible;
        }

        EFI_INPUT_KEY key;
        while(!EFI_ERROR(SystemTable->ConIn->ReadKeyStroke(SystemTable->ConIn, &key)))
        {
            cursorTicks = 0;
            cursorVisible = TRUE;

            CHAR16 unicodeChar = key.UnicodeChar;
            UINT16 scanCode = key.ScanCode;
            
            if (unicodeChar == L'\b')
            {
                if (commandLength > 0)
                {
                    commandLength--;
                    commandBuffer[commandLength] = L'\0';
                    Print(L"\b");
                }
            }
            else if (unicodeChar == L'\r')
            {
                // on return character
                CHAR16* command = commandBuffer;
                CHAR16* args = NULL;

                // sep commandBuffer into command and arguments
                for (UINTN i=0; i<commandLength; i++)
                {
                    // sep by space
                    if (commandBuffer[i] == L' ')
                    {
                        args = &commandBuffer[i+1]; // string that follows the space
                        commandBuffer[i] = L'\0'; // space becomes delimeter
                        command = commandBuffer; // string from start to where space was

                        break;
                    }
                }

                // find the command
                BOOLEAN commandFound = FALSE;
                for (UINTN i=0; i<commandSize; i++)
                {
                    if (StrCmp(command, commands[i].name) == 0)
                    {
                        // found
                        commandFound = TRUE;

                        if (commands[i].takesArgs)
                        {
                            if (args == NULL || *args ==  L'\0')
                            {
                                Print(L"\r\n");
                                Print(L"command: ");
                                Print(command);
                                Print(L" expected args.");
                            }
                            else
                            {
                                commands[i].function(args);
                            }
                        }
                        else
                        {
                            commands[i].function(args);
                        }
                        
                        break;
                    }
                }

                if (!commandFound)
                {
                    Print(L"\r\n");
                    Print(L"command: ");
                    Print(command);
                    Print(L" not found.");
                }

                Print(L"\r\n");
                Print(prompt);
                commandLength = 0;
                command[0] = L'\0';
            }
            else if (scanCode != 0)
            {
                // ignore special key
            }
            else if (commandLength < 255)
            {
                commandBuffer[commandLength] = unicodeChar;
                commandLength++;
                commandBuffer[commandLength] = L'\0';
                Print(L"%c", unicodeChar);
            }
        }

        // redraw cursor
        if (cursorVisible)
        {
            PrintCursor();
        }

        // ~30hz (microseconds)
        gBS->Stall(33333);
        cursorTicks++;
    }

    return EFI_SUCCESS;
    
}