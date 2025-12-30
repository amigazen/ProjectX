/*
 * AppX
 *
 * Copyright (c) 2025 amigazen project
 * Licensed under BSD 2-Clause License
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <exec/types.h>
#include <exec/execbase.h>
#include <dos/dos.h>
#include <intuition/intuition.h>
#include <intuition/intuitionbase.h>
#include <intuition/classusr.h>
#include <intuition/classes.h>
#include <classes/requester.h>
#include <reaction/reaction_macros.h>
#include <workbench/startup.h>
#include <workbench/workbench.h>
#include <workbench/icon.h>
#include <utility/tagitem.h>
#include <proto/exec.h>
#include <clib/alib_protos.h>
#include <proto/intuition.h>
#include <proto/dos.h>
#include <proto/icon.h>
#include <proto/wb.h>
#include <proto/requester.h>
#include <proto/utility.h>
#include <proto/graphics.h>
#include <proto/input.h>
#include <devices/input.h>
#include <devices/inputevent.h>
#include <dos/dostags.h>
#include <dos/rdargs.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* Library base pointers */
extern struct ExecBase *SysBase;
extern struct DosLibrary *DOSBase;
extern struct IntuitionBase *IntuitionBase;
extern struct Library *IconBase;
extern struct Library *WorkbenchBase;
extern struct Library *UtilityBase;
struct MsgPort *InputPort = NULL;
struct IOStdReq *InputIO = NULL;
struct Library *InputBase = NULL;

/* Reaction class library bases */
struct ClassLibrary *RequesterBase = NULL;

/* Reaction class handles */
Class *RequesterClass = NULL;

/* Forward declarations */
BOOL InitializeLibraries(VOID);
BOOL InitializeApplication(VOID);
VOID Cleanup(VOID);
VOID ShowErrorDialog(STRPTR title, STRPTR message);
BOOL ShowConfirmDialog(STRPTR fileName, STRPTR toolName);
BOOL OpenToolboxDrawer(STRPTR fileName, BPTR fileLock);
BOOL IsDirectory(STRPTR fileName, BPTR fileLock);
STRPTR GetToolTypeValue(struct DiskObject *icon, STRPTR toolTypeName);
BOOL IsLeftAmigaHeld(VOID);
BOOL HandleDrawerMode(STRPTR drawerPath);
BOOL MakeToolboxDrawer(STRPTR drawerPath, STRPTR toolName, BOOL copyImage);

static const char *verstag = "$VER: AppX 47.1 (29.12.2025)\n";
static const char *stack_cookie = "$STACK: 4096\n";
long oslibversion  = 47L; 


/* Application variables */

/* Main entry point */
int main(int argc, char *argv[])
{
    struct WBStartup *wbs = NULL;
    struct WBArg *wbarg;
    SHORT i;
    BOOL success = TRUE;
    BOOL fromWorkbench = FALSE;
    
    /* Check if running from Workbench */
    fromWorkbench = (argc == 0);
    
    if (!fromWorkbench) {
        /* CLI mode - check for DRAWER or TOOLBOX/TOOL options */
        struct RDArgs *rdargs;
        STRPTR drawerPath = NULL;
        STRPTR toolboxPath = NULL;
        STRPTR toolName = NULL;
        LONG copyImage = 0; /* COPYIMAGE/S - boolean switch */
        LONG args[4] = {0, 0, 0, 0}; /* DRAWER/K, TOOLBOX/K, TOOL/K, COPYIMAGE/S */
        CONST_STRPTR template = "DRAWER/K,TOOLBOX/K,TOOL/K,COPYIMAGE/S";
        LONG errorCode;
        
        /* Initialize libraries first (needed for ReadArgs and HandleDrawerMode/MakeToolboxDrawer) */
        if (!InitializeLibraries()) {
            return RETURN_FAIL;
        }
        
        SetIoErr(0);
        rdargs = ReadArgs(template, (LONG *)args, NULL);
        errorCode = IoErr();
        
        if (rdargs != NULL) {
            drawerPath = (STRPTR)args[0];
            toolboxPath = (STRPTR)args[1];
            toolName = (STRPTR)args[2];
            copyImage = args[3]; /* COPYIMAGE/S - 1 if set, 0 if not */
            
            /* Check for mutual exclusivity: DRAWER and TOOLBOX cannot both be specified */
            if (drawerPath != NULL && *drawerPath != '\0' && toolboxPath != NULL && *toolboxPath != '\0') {
                /* Both specified - invalid */
                FreeArgs(rdargs);
                Cleanup();
                return RETURN_FAIL;
            }
            
            /* Check if TOOLBOX is specified but TOOL is missing */
            if (toolboxPath != NULL && *toolboxPath != '\0' && (toolName == NULL || *toolName == '\0')) {
                /* TOOLBOX specified but TOOL is missing - invalid */
                FreeArgs(rdargs);
                Cleanup();
                return RETURN_FAIL;
            }
            
            /* COPYIMAGE is only valid with TOOLBOX mode */
            if (copyImage != 0 && (toolboxPath == NULL || *toolboxPath == '\0')) {
                /* COPYIMAGE specified but not in TOOLBOX mode - invalid */
                FreeArgs(rdargs);
                Cleanup();
                return RETURN_FAIL;
            }
            
            if (drawerPath != NULL && *drawerPath != '\0') {
                /* Handle drawer opening mode */
                if (HandleDrawerMode(drawerPath)) {
                    FreeArgs(rdargs);
                    Cleanup();
                    return RETURN_OK;
                } else {
                    FreeArgs(rdargs);
                    Cleanup();
                    return RETURN_FAIL;
                }
            } else if (toolboxPath != NULL && *toolboxPath != '\0') {
                /* Handle toolbox drawer creation mode (TOOL is guaranteed to be present here) */
                if (MakeToolboxDrawer(toolboxPath, toolName, copyImage != 0)) {
                    FreeArgs(rdargs);
                    Cleanup();
                    return RETURN_OK;
                } else {
                    FreeArgs(rdargs);
                    Cleanup();
                    return RETURN_FAIL;
                }
            } else {
                /* ReadArgs succeeded but no valid arguments provided */
                FreeArgs(rdargs);
                Cleanup();
                return RETURN_FAIL;
            }
        } else {
            /* ReadArgs failed - return error (no stdout available with cback.o) */
            Cleanup();
            return RETURN_FAIL;
        }
    }
    
    /* Get WBStartup message */
    wbs = (struct WBStartup *)argv;
    
    /* NOTE: Do NOT reply to WBStartup message - the startup code will do that */
    /* when main() returns. The locks in wa_Lock belong to Workbench and will */
    /* be unlocked by Workbench when the message is replied. */
    
    /* Initialize libraries */
    if (!InitializeLibraries()) {
        return RETURN_FAIL;
    }
    
    /* Initialize application (requester.class) */
    if (!InitializeApplication()) {
        Cleanup();
        return RETURN_FAIL;
    }
    
    /* Check if we have any file arguments */
    if (wbs->sm_NumArgs <= 1) {
        /* No files to process - show error */
        ShowErrorDialog("AppX", "\nNo directory specified.\n\nAppX must be set as the default tool on a toolbox drawer icon.\n");
        Cleanup();
        return RETURN_FAIL;
    }
    
    /* Process each file argument (skip index 0 which is our tool) */
    for (i = 1, wbarg = &wbs->sm_ArgList[i]; i < wbs->sm_NumArgs; i++, wbarg++) {
        BPTR oldDir = NULL;
        
        if (wbarg->wa_Lock && wbarg->wa_Name && *wbarg->wa_Name) {
            /* Change to the file's directory */
            oldDir = CurrentDir(wbarg->wa_Lock);
            
            /* Open the toolbox drawer */
            if (!OpenToolboxDrawer(wbarg->wa_Name, wbarg->wa_Lock)) {
                success = FALSE;
            }
            
            /* Restore original directory */
            if (oldDir != NULL) {
                CurrentDir(oldDir);
            }
        }
    }
    
    /* Cleanup */
    Cleanup();
    
    return success ? RETURN_OK : RETURN_FAIL;
}

/* Initialize required libraries */
BOOL InitializeLibraries(VOID)
{
    /* Open intuition.library */
    IntuitionBase = (struct IntuitionBase *)OpenLibrary("intuition.library", 47L);
    if (IntuitionBase == NULL) {
        PutStr("AppX: Failed to open intuition.library\n");
        return FALSE;
    }
    
    /* Open utility.library */
    UtilityBase = OpenLibrary("utility.library", 47L);
    if (UtilityBase == NULL) {
        PutStr("AppX: Failed to open utility.library\n");
        CloseLibrary((struct Library *)IntuitionBase);
        IntuitionBase = NULL;
        return FALSE;
    }
    
    if (!(IconBase = OpenLibrary("icon.library", 47L))) {
        PutStr("AppX: Failed to open icon.library (version 47 or higher required)\n");
        CloseLibrary(UtilityBase);
        UtilityBase = NULL;
        CloseLibrary((struct Library *)IntuitionBase);
        IntuitionBase = NULL;
        return FALSE;
    }
    
    if (!(WorkbenchBase = OpenLibrary("workbench.library", 44L))) {
        PutStr("AppX: Failed to open workbench.library\n");
        CloseLibrary(IconBase);
        IconBase = NULL;
        CloseLibrary(UtilityBase);
        UtilityBase = NULL;
        CloseLibrary((struct Library *)IntuitionBase);
        IntuitionBase = NULL;
        return FALSE;
    }
    
    /* Open input.device for qualifier checking (optional - not critical) */
    InputPort = CreateMsgPort();
    if (InputPort != NULL) {
        InputIO = (struct IOStdReq *)CreateIORequest(InputPort, sizeof(struct IOStdReq));
        if (InputIO != NULL) {
            if (OpenDevice("input.device", 0, (struct IORequest *)InputIO, 0) == 0) {
                /* Set InputBase to the library base inside the device structure */
                InputBase = (struct Library *)InputIO->io_Device;
            } else {
                DeleteIORequest((struct IORequest *)InputIO);
                InputIO = NULL;
                DeleteMsgPort(InputPort);
                InputPort = NULL;
            }
        } else {
            DeleteMsgPort(InputPort);
            InputPort = NULL;
        }
    }
    
    return TRUE;
}

/* Initialize Reaction classes */
BOOL InitializeApplication(VOID)
{
    /* Open requester.class */
    RequesterBase = (struct ClassLibrary *)OpenLibrary("requester.class", 47L);
    if (RequesterBase == NULL) {
        return FALSE;
    }
    
    /* Get the requester class */
    RequesterClass = REQUESTER_GetClass();
    if (RequesterClass == NULL) {
        CloseLibrary(RequesterBase);
        RequesterBase = NULL;
        return FALSE;
    }
    
    return TRUE;
}

/* Cleanup libraries */
VOID Cleanup(VOID)
{
    if (RequesterClass != NULL) {
        RequesterClass = NULL;
    }
    
    if (RequesterBase != NULL) {
        CloseLibrary(RequesterBase);
        RequesterBase = NULL;
    }
    
    if (WorkbenchBase) {
        CloseLibrary(WorkbenchBase);
        WorkbenchBase = NULL;
    }
    
    if (IconBase) {
        CloseLibrary(IconBase);
        IconBase = NULL;
    }
    
    if (UtilityBase) {
        CloseLibrary(UtilityBase);
        UtilityBase = NULL;
    }
    
    if (IntuitionBase) {
        CloseLibrary((struct Library *)IntuitionBase);
        IntuitionBase = NULL;
    }
    
    if (InputIO != NULL) {
        CloseDevice((struct IORequest *)InputIO);
        DeleteIORequest((struct IORequest *)InputIO);
        InputIO = NULL;
        InputBase = NULL;
    }
    if (InputPort != NULL) {
        DeleteMsgPort(InputPort);
        InputPort = NULL;
    }
    
}

/* Check if a file is a directory */
/* fileLock is the lock to the parent directory, fileName is the file name */
BOOL IsDirectory(STRPTR fileName, BPTR fileLock)
{
    struct FileInfoBlock *fib;
    BPTR fileLock2 = NULL;
    BOOL isDir = FALSE;
    BPTR oldDir = NULL;
    
    if (fileLock == NULL || fileName == NULL) {
        return FALSE;
    }
    
    /* Change to the parent directory */
    oldDir = CurrentDir(fileLock);
    
    /* Lock the file itself */
    fileLock2 = Lock((UBYTE *)fileName, SHARED_LOCK);
    
    if (fileLock2 != NULL) {
        fib = (struct FileInfoBlock *)AllocDosObject(DOS_FIB, NULL);
        if (fib != NULL) {
            if (Examine(fileLock2, fib)) {
                /* fib_DirEntryType > 0 means it's a directory */
                if (fib->fib_DirEntryType > 0) {
                    isDir = TRUE;
                }
            }
            FreeDosObject(DOS_FIB, fib);
        }
        UnLock(fileLock2);
    }
    
    /* Restore original directory */
    if (oldDir != NULL) {
        CurrentDir(oldDir);
    }
    
    return isDir;
}

/* Get ToolType value from a DiskObject */
STRPTR GetToolTypeValue(struct DiskObject *icon, STRPTR toolTypeName)
{
    STRPTR toolTypeValue = NULL;
    
    if (icon == NULL || toolTypeName == NULL || icon->do_ToolTypes == NULL) {
        return NULL;
    }
    
    /* FindToolType returns the value part after the '=' sign, or NULL if not found */
    toolTypeValue = (STRPTR)FindToolType((UBYTE **)icon->do_ToolTypes, (UBYTE *)toolTypeName);
    
    return toolTypeValue;
}

/* Check if Right Shift key is currently held down */
BOOL IsLeftAmigaHeld(VOID)
{
    UWORD qualifier;
    
    if (InputBase == NULL) {
        return FALSE;
    }
    
    /* PeekQualifier returns the current qualifier state */
    qualifier = PeekQualifier();
    
    /* Check if Right Shift key (LCOMMAND) is held */
    if (qualifier & IEQUALIFIER_RSHIFT) {
        return TRUE;
    }
    
    return FALSE;
}

/* Show error dialog */
VOID ShowErrorDialog(STRPTR title, STRPTR message)
{
    Object *reqobj;
    
    if (RequesterClass == NULL) {
        /* Requester class not available - use PutStr as fallback */
        PutStr("AppX Error: ");
        PutStr(message);
        PutStr("\n");
        return;
    }
    
    /* Create the requester object with error type */
    reqobj = NewObject(RequesterClass, NULL,
                       REQ_TitleText, title,
                       REQ_BodyText, message,
                       REQ_Type, REQTYPE_INFO,
                       REQ_GadgetText, "OK",
                       REQ_Image, REQIMAGE_ERROR,
                       TAG_END);
    
    if (reqobj != NULL) {
        /* Show the requester and wait for user response */
        DoMethod(reqobj, RM_OPENREQ, NULL, 0L, NULL, TAG_DONE);
        
        /* Clean up the requester object */
        DisposeObject(reqobj);
    }
}

/* Show confirmation dialog before launching tool */
/* NOTE: Currently bypassed - always returns TRUE (code kept for future multi-tool selection) */
BOOL ShowConfirmDialog(STRPTR fileName, STRPTR toolName)
{
    Object *reqobj;
    char title[256];
    char message[512];
    ULONG result;
    
    /* Bypass confirmation dialog for now - always proceed */
    return TRUE;
    
    /* Code below kept for future use when we want to offer tool selection */
    if (RequesterClass == NULL) {
        /* Requester class not available - default to yes */
        return TRUE;
    }
    
    /* Format the title and message */
    Strncpy(title, "AppX", 255);
    title[255] = '\0';
    
    sprintf(message,
            "\n\n"
            "File: %s\n\n"
            "Tool: %s\n\n"
            "Launch this tool?\n\n",
            fileName, toolName);
    
    /* Create the requester object with confirmation type */
    reqobj = NewObject(RequesterClass, NULL,
                       REQ_TitleText, title,
                       REQ_BodyText, message,
                       REQ_Type, REQTYPE_INFO,
                       REQ_GadgetText, "Yes|No",
                       REQ_Image, REQIMAGE_QUESTION,
                       TAG_END);
    
    if (reqobj != NULL) {
        /* Show the requester and wait for user response */
        /* RM_OPENREQ returns button number: 1 = first button (Yes), 2 = second button (No) */
        result = DoMethod(reqobj, RM_OPENREQ, NULL, 0L, NULL, TAG_DONE);
        
        /* Clean up the requester object */
        DisposeObject(reqobj);
        
        /* Return TRUE if user clicked "Yes" (first button, result == 1) */
        /* Note: Some implementations may return 0 for first button instead of 1 */
        if (result == 1) {
            return TRUE;
        }
        /* Result 0 might also mean first button in some implementations */
        if (result == 0) {
            /* Assume 0 means first button (Yes) was clicked */
            return TRUE;
        }
        /* Result 2 or higher means No or other button */
        return FALSE;
    }
    
    /* If requester creation failed, default to yes */
    return TRUE;
}

/* Open toolbox drawer - handles directories with TOOLBOX tooltype */
BOOL OpenToolboxDrawer(STRPTR fileName, BPTR fileLock)
{
    BOOL success = FALSE;
    BOOL confirmed = FALSE;
    struct TagItem tags[1];
    UBYTE errorMsg[512];
    struct DiskObject *projectIcon = NULL;
    STRPTR toolboxValue = NULL;
    UBYTE fullToolPath[512];
    LONG errorCode;
    
    /* Check if this is a directory (drawer) */
    if (IsDirectory(fileName, fileLock)) {
        UBYTE dirPath[512];
        UBYTE fullDirPath[512];
        UBYTE iconPath[512];
        UBYTE toolboxValueCopy[256];
        LONG errorCode;
        LONG errorCode1;
        LONG errorCode2;
        UBYTE errorMsg[512];
        UBYTE triedPath1[512];
        UBYTE triedPath2[512];
        
        /* Get the full path to the directory from the lock */
        NameFromLock(fileLock, dirPath, sizeof(dirPath));
        
        /* Construct the full path to the directory itself (parent + directory name) */
        /* Use AddPart to properly handle volume roots (no extra /) */
        Strncpy(fullDirPath, dirPath, sizeof(fullDirPath) - 1);
        fullDirPath[sizeof(fullDirPath) - 1] = '\0';
        if (!AddPart(fullDirPath, fileName, sizeof(fullDirPath))) {
            ShowErrorDialog("AppX",
                "\nPath too long.\n\n"
                "The directory path is too long to process.\n");
            return FALSE;
        }
        
        /* Remove trailing slash if present */
        {
            LONG len;
            len = strlen((char *)fullDirPath);
            if (len > 0 && fullDirPath[len - 1] == '/') {
                fullDirPath[len - 1] = '\0';
            }
        }
        
        /* Construct full path to the icon file (with .info for PutDiskObject and file checks) */
        /* For file extensions, append directly instead of using AddPart() */
        {
            LONG baseLen;
            baseLen = strlen((char *)fullDirPath);
            if (baseLen + 5 >= sizeof(iconPath)) { /* 5 = strlen(".info") + null terminator */
                ShowErrorDialog("AppX",
                    "\nPath too long.\n\n"
                    "The icon file path is too long to process.\n");
                return FALSE;
            }
            Strncpy(iconPath, fullDirPath, sizeof(iconPath) - 1);
            iconPath[sizeof(iconPath) - 1] = '\0';
            Strncpy(iconPath + baseLen, ".info", sizeof(iconPath) - baseLen);
            iconPath[sizeof(iconPath) - 1] = '\0';
        }
        
        /* Clear any previous error */
        /* GetDiskObject() automatically appends .info, so use base path */
        SetIoErr(0);
        projectIcon = GetDiskObject(fullDirPath);
        errorCode1 = IoErr();
        
            if (projectIcon == NULL) {
                /* GetDiskObject failed - show detailed error */
                /* GetDiskObject() automatically appends .info, so it tried fullDirPath + ".info" */
                SNPrintf(triedPath1, sizeof(triedPath1), "%s.info", fullDirPath);
                SNPrintf(triedPath2, sizeof(triedPath2), "%s", fullDirPath);
                
                SNPrintf(errorMsg, sizeof(errorMsg),
                    "Could not load project icon.\n\n"
                    "Directory: %s\n"
                    "Directory name: %s\n\n"
                    "Tried icon paths:\n"
                    "1. %s (GetDiskObject base path + .info)\n"
                    "   Error code: %ld\n\n"
                    "The icon file could not be found or read.\n"
                    "Please ensure the directory has a .info icon file.",
                    dirPath, fileName, triedPath1, errorCode1);
                ShowErrorDialog("AppX", errorMsg);
                return FALSE;
            }
        
        /* Get the TOOLBOX tooltype value */
        toolboxValue = GetToolTypeValue(projectIcon, "TOOLBOX");
        
        if (toolboxValue == NULL || *toolboxValue == '\0') {
            FreeDiskObject(projectIcon);
            ShowErrorDialog("AppX",
                "\nNo TOOLBOX tooltype found.\n\n"
                "This directory icon must have a TOOLBOX tooltype\n"
                "specifying the application to run.\n");
            return FALSE;
        }
        
        /* Copy the toolbox value before freeing the icon */
        Strncpy(toolboxValueCopy, toolboxValue, sizeof(toolboxValueCopy) - 1);
        toolboxValueCopy[sizeof(toolboxValueCopy) - 1] = '\0';
        
        /* Construct full path: full directory path + tool name */
        /* Use AddPart to properly handle volume roots (no extra /) */
        Strncpy(fullToolPath, fullDirPath, sizeof(fullToolPath) - 1);
        fullToolPath[sizeof(fullToolPath) - 1] = '\0';
        if (!AddPart(fullToolPath, toolboxValueCopy, sizeof(fullToolPath))) {
            ShowErrorDialog("AppX",
                "\nPath too long.\n\n"
                "The tool path is too long to process.\n");
            return FALSE;
        }
        
        /* Check if Right Shift key is held - if so, spawn second process to open drawer */
        if (IsLeftAmigaHeld()) {
            /* Right Shift key held - spawn a second process to handle drawer opening */
            /* This avoids the ERROR_OBJECT_IN_USE issue because the second process */
            /* doesn't have the WBStartup lock */
            UBYTE command[512];
            struct TagItem sysTags[2];
            LONG sysResult;
            
            /* Free the icon before spawning the process */
            FreeDiskObject(projectIcon);
            projectIcon = NULL;
            
            /* Build command: Use PROGDIR: to get full path to AppX */
            {
                BPTR progDirLock;
                UBYTE progPath[256];
                
                progDirLock = Lock("PROGDIR:", ACCESS_READ);
                if (progDirLock != NULL) {
                    NameFromLock(progDirLock, progPath, sizeof(progPath));
                    UnLock(progDirLock);
                    SNPrintf(command, sizeof(command), "%s/AppX DRAWER=%s", progPath, fullDirPath);
                } else {
                    /* Fallback: try without path (assumes AppX is in PATH) */
                    SNPrintf(command, sizeof(command), "AppX DRAWER=%s", fullDirPath);
                }
            }
            
            /* Spawn the second process asynchronously */
            sysTags[0].ti_Tag = SYS_Asynch;
            sysTags[0].ti_Data = (ULONG)TRUE;
            sysTags[1].ti_Tag = TAG_DONE;
            
            SetIoErr(0);
            sysResult = System(command, sysTags);
            
            if (sysResult == -1) {
                /* System() failed */
                errorCode = IoErr();
                SNPrintf(errorMsg, sizeof(errorMsg),
                    "Failed to spawn drawer opening process.\n\n"
                    "Path: %s\n\n"
                    "Error code: %ld\n\n"
                    "The drawer could not be opened.",
                    fullDirPath, errorCode);
                ShowErrorDialog("AppX", errorMsg);
                return FALSE;
            }
            
            /* Success - second process spawned, primary process exits immediately */
            return TRUE;
        }
        
        /* Free the icon BEFORE showing dialog */
        FreeDiskObject(projectIcon);
        projectIcon = NULL;
        
        /* Right Shift key not held - show confirmation dialog with the full directory path */
        confirmed = ShowConfirmDialog(fullDirPath, toolboxValueCopy);
        if (!confirmed) {
            /* User clicked No - return FALSE but don't show error */
            return FALSE;
        }
        
        /* User clicked Yes - launch the tool */
        /* Check if the tool is a shell script (has FIBF_SCRIPT protection bit) */
        {
            BPTR toolCheckLock = NULL;
            struct FileInfoBlock *fib = NULL;
            BOOL isScript = FALSE;
            
            /* Lock the tool to examine it */
            toolCheckLock = Lock((UBYTE *)fullToolPath, SHARED_LOCK);
            if (toolCheckLock != NULL) {
                fib = (struct FileInfoBlock *)AllocDosObject(DOS_FIB, NULL);
                if (fib != NULL) {
                    if (Examine(toolCheckLock, fib)) {
                        /* Check if FIBF_SCRIPT bit is set */
                        if (fib->fib_Protection & FIBF_SCRIPT) {
                            isScript = TRUE;
                        }
                    }
                    FreeDosObject(DOS_FIB, fib);
                }
                UnLock(toolCheckLock);
            }
            
            if (isScript) {
                /* Tool is a shell script - use SystemTagList() to execute it */
                struct TagItem sysTags[2];
                LONG sysResult;
                
                /* Build command: just the tool path (shell will handle script execution) */
                sysTags[0].ti_Tag = SYS_Asynch;
                sysTags[0].ti_Data = (ULONG)TRUE;
                sysTags[1].ti_Tag = TAG_DONE;
                
                SetIoErr(0);
                sysResult = SystemTagList(fullToolPath, sysTags);
                errorCode = IoErr();
                
                if (sysResult == -1 || errorCode != 0) {
                    /* SystemTagList failed - show error */
                    SNPrintf(errorMsg, sizeof(errorMsg),
                        "\nFailed to launch script %s\n\n"
                        "Error code: %ld\n\n"
                        "Please check that the script exists and is executable.\n",
                        fullToolPath, errorCode);
                    ShowErrorDialog("AppX", errorMsg);
                    return FALSE;
                }
                
                /* Success - script was launched */
                return TRUE;
            } else {
                /* Tool is not a script - use OpenWorkbenchObjectA() */
                tags[0].ti_Tag = TAG_DONE;
                
                /* Clear any previous error */
                SetIoErr(0);
                
                success = OpenWorkbenchObjectA(fullToolPath, tags);
                
                /* Check IoErr() regardless of return value, as OpenWorkbenchObjectA may return TRUE even on failure */
                errorCode = IoErr();
                
                if (!success || errorCode != 0) {
                    /* OpenWorkbenchObjectA failed - show error code */
                    SNPrintf(errorMsg, sizeof(errorMsg),
                        "\nFailed to launch %s\n\n"
                        "Error code: %ld\n\n"
                        "Please check that the Tool exists.\n",
                        fullToolPath, errorCode);
                    ShowErrorDialog("AppX", errorMsg);
                    return FALSE;
                }
                
                /* Success - tool was launched */
                return TRUE;
            }
        }
    }
    
    /* Not a directory - show error */
    ShowErrorDialog("AppX",
        "\nNot a directory.\n\n"
        "AppX only works with toolbox drawer icons.\n"
        "Please set AppX as the default tool on a drawer icon\n"
        "with a TOOLBOX tooltype.\n");
    return FALSE;
}

/* Handle drawer opening mode (CLI mode) */
/* This function is called from a second process spawned by the main process */
/* It waits for the icon file to be available, changes the type, opens the drawer, */
/* waits a few seconds, then restores the icon type */
BOOL HandleDrawerMode(STRPTR drawerPath)
{
    UBYTE iconPath[512];
    UBYTE fullDirPath[512];
    struct DiskObject *projectIcon = NULL;
    LONG originalType;
    struct TagItem tags[1];
    BOOL putSuccess;
    BPTR iconFile;
    LONG waitCount;
    BOOL success = FALSE;
    LONG errorCode;
    
    if (drawerPath == NULL || *drawerPath == '\0') {
        return FALSE;
    }
    
    /* Copy the drawer path */
    Strncpy(fullDirPath, drawerPath, sizeof(fullDirPath) - 1);
    fullDirPath[sizeof(fullDirPath) - 1] = '\0';
    
    /* Remove trailing slash if present */
    {
        LONG len;
        len = strlen((char *)fullDirPath);
        if (len > 0 && fullDirPath[len - 1] == '/') {
            fullDirPath[len - 1] = '\0';
        }
    }
    
    /* Construct full path to the icon file (with .info for PutDiskObject and file checks) */
    /* For file extensions, append directly instead of using AddPart() */
    {
        LONG baseLen;
            baseLen = strlen((char *)fullDirPath);
            if (baseLen + 5 >= sizeof(iconPath)) { /* 5 = strlen(".info") + null terminator */
                return FALSE;
            }
            Strncpy(iconPath, fullDirPath, sizeof(iconPath) - 1);
            iconPath[sizeof(iconPath) - 1] = '\0';
            Strncpy(iconPath + baseLen, ".info", sizeof(iconPath) - baseLen);
            iconPath[sizeof(iconPath) - 1] = '\0';
        }
    
    /* Wait until we can get a lock on the icon file */
    /* This ensures the parent process has released it */
    waitCount = 0;
    while (waitCount < 50) { /* Wait up to 5 seconds (50 * 100ms) */
        SetIoErr(0);
        iconFile = Open(iconPath, MODE_OLDFILE);
        if (iconFile != NULL) {
            Close(iconFile);
            break;
        }
        
        /* Try without .info extension */
        SetIoErr(0);
        iconFile = Open(fullDirPath, MODE_OLDFILE);
        if (iconFile != NULL) {
            Close(iconFile);
            /* Use fullDirPath as iconPath */
            Strncpy(iconPath, fullDirPath, sizeof(iconPath) - 1);
            iconPath[sizeof(iconPath) - 1] = '\0';
            break;
        }
        
        /* Wait 100ms before trying again */
        Delay(10); /* 10 ticks = ~100ms */
        waitCount++;
    }
    
    if (waitCount >= 50) {
        return FALSE;
    }
    
    /* Libraries should already be initialized by main(), but check anyway */
    if (IconBase == NULL || WorkbenchBase == NULL) {
        if (!InitializeLibraries()) {
            return FALSE;
        }
    }
    
    /* Load the icon */
    /* GetDiskObject() automatically appends .info, so use base path */
    SetIoErr(0);
    projectIcon = GetDiskObject(fullDirPath);
    
    if (projectIcon == NULL) {
        Cleanup();
        return FALSE;
    }
    
    /* Save the original icon type */
    originalType = projectIcon->do_Type;
    
    /* Change icon type to WBDRAWER */
    projectIcon->do_Type = WBDRAWER;
    
    /* Save the icon with the new type */
    /* PutDiskObject() may also append .info, so try base path first */
    putSuccess = PutDiskObject(fullDirPath, projectIcon);
    if (!putSuccess) {
        /* Try with .info extension explicitly */
        putSuccess = PutDiskObject(iconPath, projectIcon);
    }
    
    if (!putSuccess) {
        projectIcon->do_Type = originalType;
        FreeDiskObject(projectIcon);
        Cleanup();
        return FALSE;
    }
    
    /* Free the icon so Workbench will read it fresh from disk */
    FreeDiskObject(projectIcon);
    projectIcon = NULL;
    
    /* Ensure the icon file write is flushed to disk */
    SetIoErr(0);
    iconFile = Open(iconPath, MODE_OLDFILE);
    if (iconFile != NULL) {
        Flush(iconFile);
        Close(iconFile);
    } else {
        /* Try without .info extension */
        SetIoErr(0);
        iconFile = Open(fullDirPath, MODE_OLDFILE);
        if (iconFile != NULL) {
            Flush(iconFile);
            Close(iconFile);
        }
    }
    
    /* Now open the drawer */
    tags[0].ti_Tag = TAG_DONE;
    SetIoErr(0);
    success = OpenWorkbenchObjectA(fullDirPath, tags);
    errorCode = IoErr();
    
    if (!success || errorCode != 0) {
        /* Failed to open - restore icon type immediately */
        /* Reload the icon to restore the type */
        /* GetDiskObject() automatically appends .info, so use base path */
        SetIoErr(0);
        projectIcon = GetDiskObject(fullDirPath);
        
        if (projectIcon != NULL) {
            /* Restore the original icon type */
            projectIcon->do_Type = originalType;
            
            /* Save the icon with the original type restored */
            /* PutDiskObject() may also append .info, so try base path first */
            PutDiskObject(fullDirPath, projectIcon);
            if (IoErr() != 0) {
                SetIoErr(0);
                PutDiskObject(iconPath, projectIcon);
            }
            
            FreeDiskObject(projectIcon);
        }
    } else {
        /* Drawer opened successfully - wait until it closes, then restore icon type */
        /* Use WorkbenchControlA to check if drawer is still open */
        {
            LONG isOpen;
            struct TagItem wbTags[2];
            LONG checkCount;
            
            /* Check every 500ms (50 ticks) until drawer is closed */
            checkCount = 0;
            while (checkCount < 600) { /* Wait up to 30 seconds (600 * 50 ticks) */
                wbTags[0].ti_Tag = WBCTRLA_IsOpen;
                wbTags[0].ti_Data = (ULONG)&isOpen;
                wbTags[1].ti_Tag = TAG_DONE;
                
                SetIoErr(0);
                if (WorkbenchControlA(fullDirPath, wbTags)) {
                    if (!isOpen) {
                        /* Drawer is now closed */
                        break;
                    }
                } else {
                    /* WorkbenchControlA failed - assume drawer is closed */
                    break;
                }
                
                /* Wait 500ms before checking again */
                Delay(50); /* 50 ticks = ~500ms */
                checkCount++;
            }
        }
        
        /* Drawer is now closed - restore icon type */
        SetIoErr(0);
        projectIcon = GetDiskObject(fullDirPath);
        
        if (projectIcon != NULL) {
            /* Restore the original icon type */
            projectIcon->do_Type = originalType;
            
            /* Save the icon with the original type restored */
            /* PutDiskObject() may also append .info, so try base path first */
            PutDiskObject(fullDirPath, projectIcon);
            if (IoErr() != 0) {
                SetIoErr(0);
                PutDiskObject(iconPath, projectIcon);
            }
            
            FreeDiskObject(projectIcon);
        }
    }
    Cleanup();
    return success;
}

/* Make a toolbox drawer: convert a drawer icon to a project-drawer with AppX as default tool */
/* drawerPath: full path to the drawer */
/* toolName: name of the tool inside the drawer (without path) */
/* copyImage: if TRUE, copy the image from the tool icon instead of the drawer icon */
BOOL MakeToolboxDrawer(STRPTR drawerPath, STRPTR toolName, BOOL copyImage)
{
    struct DiskObject *drawerIcon = NULL;
    struct DiskObject *toolIcon = NULL;
    UBYTE fullToolPath[512];
    UBYTE fullDirPath[512];
    UBYTE iconPath[512];
    UBYTE appXPath[256];
    STRPTR *oldToolTypes = NULL;
    STRPTR *newToolTypes = NULL;
    LONG toolTypeCount = 0;
    LONG i;
    LONG toolboxIndex = -1;
    BOOL success = FALSE;
    BPTR drawerLock = NULL;
    BPTR toolLock = NULL;
    BPTR progDirLock = NULL;
    
    if (drawerPath == NULL || *drawerPath == '\0' || toolName == NULL || *toolName == '\0') {
        return FALSE;
    }
    
    /* Libraries should already be initialized by main(), but check anyway */
    if (IconBase == NULL || WorkbenchBase == NULL || DOSBase == NULL) {
        if (!InitializeLibraries()) {
            return FALSE;
        }
    }
    
    /* Copy drawer path and remove trailing slash if present */
    Strncpy(fullDirPath, drawerPath, sizeof(fullDirPath) - 1);
    fullDirPath[sizeof(fullDirPath) - 1] = '\0';
    {
        LONG len = strlen((char *)fullDirPath);
        if (len > 0 && fullDirPath[len - 1] == '/') {
            fullDirPath[len - 1] = '\0';
        }
    }
    
    /* Construct icon path by appending .info */
    {
        LONG baseLen = strlen((char *)fullDirPath);
        if (baseLen + 5 >= sizeof(iconPath)) { /* 5 = strlen(".info") + null terminator */
            return FALSE;
        }
        Strncpy(iconPath, fullDirPath, sizeof(iconPath) - 1);
        iconPath[sizeof(iconPath) - 1] = '\0';
        Strncpy(iconPath + baseLen, ".info", sizeof(iconPath) - baseLen);
        iconPath[sizeof(iconPath) - 1] = '\0';
    }
    
    /* Construct full path to tool inside drawer */
    Strncpy(fullToolPath, fullDirPath, sizeof(fullToolPath) - 1);
    fullToolPath[sizeof(fullToolPath) - 1] = '\0';
    AddPart(fullToolPath, toolName, sizeof(fullToolPath));
    
    /* Verify drawer exists */
    drawerLock = Lock((UBYTE *)fullDirPath, SHARED_LOCK);
    if (drawerLock == NULL) {
        return FALSE;
    }
    UnLock(drawerLock);
    
    /* Verify tool exists and is a tool */
    toolLock = Lock((UBYTE *)fullToolPath, SHARED_LOCK);
    if (toolLock == NULL) {
        return FALSE;
    }
    
    /* Load tool icon and verify it's a WBTOOL type */
    toolIcon = GetDiskObject(fullToolPath);
    UnLock(toolLock);
    
    if (toolIcon == NULL) {
        return FALSE;
    }
    
    if (toolIcon->do_Type != WBTOOL) {
        FreeDiskObject(toolIcon);
        return FALSE;
    }
    
    /* Keep toolIcon loaded if copyImage is TRUE, otherwise free it */
    if (!copyImage) {
        FreeDiskObject(toolIcon);
        toolIcon = NULL;
    }
    
    /* Load drawer icon */
    drawerIcon = GetDiskObject(fullDirPath);
    if (drawerIcon == NULL) {
        if (toolIcon != NULL) {
            FreeDiskObject(toolIcon);
        }
        return FALSE;
    }
    
    /* Verify it's a WBDRAWER type */
    if (drawerIcon->do_Type != WBDRAWER) {
        FreeDiskObject(drawerIcon);
        if (toolIcon != NULL) {
            FreeDiskObject(toolIcon);
        }
        return FALSE;
    }
    
    /* Get AppX path using PROGDIR: */
    progDirLock = Lock("PROGDIR:", ACCESS_READ);
    if (progDirLock != NULL) {
        NameFromLock(progDirLock, appXPath, sizeof(appXPath));
        UnLock(progDirLock);
        AddPart(appXPath, "AppX", sizeof(appXPath));
    } else {
        /* Fallback: just use "AppX" (assumes it's in PATH) */
        Strncpy(appXPath, "AppX", sizeof(appXPath) - 1);
        appXPath[sizeof(appXPath) - 1] = '\0';
    }
    
    /* Count existing tooltypes and find TOOLBOX if it exists */
    if (drawerIcon->do_ToolTypes != NULL) {
        for (i = 0; drawerIcon->do_ToolTypes[i] != NULL; i++) {
            toolTypeCount++;
            /* Check if this is TOOLBOX */
            if (Strnicmp((UBYTE *)drawerIcon->do_ToolTypes[i], (UBYTE *)"TOOLBOX=", 8) == 0) {
                toolboxIndex = i;
            }
        }
    }
    
    /* Allocate new tooltype array */
    /* We need space for: existing tooltypes + TOOLBOX (if not found) + NULL terminator */
    /* If TOOLBOX exists, we'll replace it, so same count */
    {
        LONG newCount = toolTypeCount;
        if (toolboxIndex < 0) {
            newCount++; /* Need to add TOOLBOX */
        }
        newCount++; /* NULL terminator */
        
        newToolTypes = (STRPTR *)AllocMem(newCount * sizeof(STRPTR), MEMF_CLEAR | MEMF_PUBLIC);
        if (newToolTypes == NULL) {
            FreeDiskObject(drawerIcon);
            return FALSE;
        }
    }
    
    /* Copy existing tooltypes and add/update TOOLBOX */
    {
        LONG newIndex = 0;
        UBYTE toolboxToolType[128];
        
        /* Build TOOLBOX tooltype string */
        SNPrintf(toolboxToolType, sizeof(toolboxToolType), "TOOLBOX=%s", toolName);
        
        /* Copy existing tooltypes, replacing TOOLBOX if found */
        if (drawerIcon->do_ToolTypes != NULL) {
            for (i = 0; drawerIcon->do_ToolTypes[i] != NULL; i++) {
                if (i == toolboxIndex) {
                    /* Replace existing TOOLBOX */
                    newToolTypes[newIndex] = (STRPTR)AllocMem(strlen((char *)toolboxToolType) + 1, MEMF_PUBLIC);
                    if (newToolTypes[newIndex] == NULL) {
                        /* Free what we allocated so far */
                        {
                            LONG j;
                            for (j = 0; j < newIndex; j++) {
                                FreeMem(newToolTypes[j], strlen((char *)newToolTypes[j]) + 1);
                            }
                        }
                        FreeMem(newToolTypes, (toolTypeCount + (toolboxIndex < 0 ? 2 : 1)) * sizeof(STRPTR));
                        FreeDiskObject(drawerIcon);
                        return FALSE;
                    }
                    {
                        LONG toolTypeLen = strlen((char *)toolboxToolType);
                        Strncpy((UBYTE *)newToolTypes[newIndex], toolboxToolType, toolTypeLen + 1);
                    }
                    newIndex++;
                } else {
                    /* Copy existing tooltype */
                    LONG len = strlen((char *)drawerIcon->do_ToolTypes[i]);
                    newToolTypes[newIndex] = (STRPTR)AllocMem(len + 1, MEMF_PUBLIC);
                    if (newToolTypes[newIndex] == NULL) {
                        /* Free what we allocated so far */
                        {
                            LONG j;
                            for (j = 0; j < newIndex; j++) {
                                FreeMem(newToolTypes[j], strlen((char *)newToolTypes[j]) + 1);
                            }
                        }
                        FreeMem(newToolTypes, (toolTypeCount + (toolboxIndex < 0 ? 2 : 1)) * sizeof(STRPTR));
                        FreeDiskObject(drawerIcon);
                        return FALSE;
                    }
                    Strncpy((UBYTE *)newToolTypes[newIndex], (UBYTE *)drawerIcon->do_ToolTypes[i], len + 1);
                    newIndex++;
                }
            }
        }
        
        /* Add TOOLBOX if it wasn't found */
        if (toolboxIndex < 0) {
            newToolTypes[newIndex] = (STRPTR)AllocMem(strlen((char *)toolboxToolType) + 1, MEMF_PUBLIC);
            if (newToolTypes[newIndex] == NULL) {
                /* Free what we allocated so far */
                {
                    LONG j;
                    for (j = 0; j < newIndex; j++) {
                        FreeMem(newToolTypes[j], strlen((char *)newToolTypes[j]) + 1);
                    }
                }
                FreeMem(newToolTypes, (toolTypeCount + 2) * sizeof(STRPTR));
                FreeDiskObject(drawerIcon);
                return FALSE;
            }
            {
                LONG toolTypeLen = strlen((char *)toolboxToolType);
                Strncpy((UBYTE *)newToolTypes[newIndex], toolboxToolType, toolTypeLen + 1);
            }
            newIndex++;
        }
        
        /* NULL terminator */
        newToolTypes[newIndex] = NULL;
    }
    
    /* Determine source icon for duplication */
    {
        struct DiskObject *sourceIcon = copyImage ? toolIcon : drawerIcon;
        struct DiskObject *newIcon = NULL;
        struct TagItem dupTags[4];
        struct TagItem putTags[3];
        LONG errorCode;
        
        /* Delete the old icon file to avoid corruption issues */
        SetIoErr(0);
        DeleteFile(iconPath);
        if (IoErr() != 0) {
            SetIoErr(0);
            DeleteFile(fullDirPath);
        }
        
        /* Duplicate the source icon (drawer or tool) using DupDiskObjectA */
        /* This properly duplicates all icon data including images and image data */
        dupTags[0].ti_Tag = ICONDUPA_DuplicateImages;
        dupTags[0].ti_Data = TRUE;
        dupTags[1].ti_Tag = ICONDUPA_DuplicateImageData;
        dupTags[1].ti_Data = TRUE;
        dupTags[2].ti_Tag = ICONDUPA_DuplicateToolTypes;
        dupTags[2].ti_Data = FALSE; /* We'll replace with our own tooltypes */
        dupTags[3].ti_Tag = TAG_DONE;
        
        newIcon = DupDiskObjectA(sourceIcon, dupTags);
        if (newIcon == NULL) {
            /* Free new tooltypes */
            {
                LONG j;
                for (j = 0; newToolTypes[j] != NULL; j++) {
                    FreeMem(newToolTypes[j], strlen((char *)newToolTypes[j]) + 1);
                }
            }
            FreeMem(newToolTypes, (toolTypeCount + (toolboxIndex < 0 ? 2 : 1)) * sizeof(STRPTR));
            FreeDiskObject(drawerIcon);
            if (toolIcon != NULL) {
                FreeDiskObject(toolIcon);
            }
            return FALSE;
        }
        
        /* Copy icon position from drawer icon (always use drawer position) */
        newIcon->do_CurrentX = drawerIcon->do_CurrentX;
        newIcon->do_CurrentY = drawerIcon->do_CurrentY;
        
        /* Set icon properties */
        newIcon->do_Type = WBPROJECT;
        newIcon->do_ToolTypes = newToolTypes;
        
        /* Allocate and set default tool */
        newIcon->do_DefaultTool = (STRPTR)AllocMem(strlen((char *)appXPath) + 1, MEMF_PUBLIC);
        if (newIcon->do_DefaultTool == NULL) {
            /* Free new tooltypes */
            {
                LONG j;
                for (j = 0; newToolTypes[j] != NULL; j++) {
                    FreeMem(newToolTypes[j], strlen((char *)newToolTypes[j]) + 1);
                }
            }
            FreeMem(newToolTypes, (toolTypeCount + (toolboxIndex < 0 ? 2 : 1)) * sizeof(STRPTR));
            FreeDiskObject(newIcon);
            FreeDiskObject(drawerIcon);
            if (toolIcon != NULL) {
                FreeDiskObject(toolIcon);
            }
            return FALSE;
        }
        {
            LONG pathLen = strlen((char *)appXPath);
            Strncpy((UBYTE *)newIcon->do_DefaultTool, appXPath, pathLen + 1);
        }
        
        /* Save the new icon using PutIconTagList for better control */
        SetIoErr(0);
        errorCode = 0;
        putTags[0].ti_Tag = ICONPUTA_NotifyWorkbench;
        putTags[0].ti_Data = TRUE;
        putTags[1].ti_Tag = ICONA_ErrorCode;
        putTags[1].ti_Data = (ULONG)&errorCode;
        putTags[2].ti_Tag = TAG_DONE;
        
        success = PutIconTagList(fullDirPath, newIcon, putTags);
        if (!success || errorCode != 0) {
            /* Try with explicit .info path */
            SetIoErr(0);
            errorCode = 0;
            success = PutIconTagList(iconPath, newIcon, putTags);
            if (success && errorCode == 0) {
                success = TRUE;
            } else {
                success = FALSE;
            }
        }
        
        /* Free the new icon (PutIconTagList has made copies of images, tooltypes, and strings) */
        /* Note: We must NOT free the image pointers - PutIconTagList copies them */
        /* But we should clear tooltypes and defaulttool since we allocated those */
        newIcon->do_ToolTypes = NULL;
        newIcon->do_DefaultTool = NULL;
        FreeDiskObject(newIcon);
        
        /* Free new tooltypes (PutIconTagList has made copies, so we can free these) */
        {
            LONG j;
            for (j = 0; newToolTypes[j] != NULL; j++) {
                FreeMem(newToolTypes[j], strlen((char *)newToolTypes[j]) + 1);
            }
        }
        FreeMem(newToolTypes, (toolTypeCount + (toolboxIndex < 0 ? 2 : 1)) * sizeof(STRPTR));
    }
    
    /* Free the original icons */
    FreeDiskObject(drawerIcon);
    if (toolIcon != NULL) {
        FreeDiskObject(toolIcon);
    }
    
    return success;
}