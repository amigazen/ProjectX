/*
 * ProjectX
 *
 * Copyright (c) 2025 amigazen project
 * Licensed under BSD 2-Clause License
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
#include <proto/rexxsyslib.h>
#include <devices/input.h>
#include <devices/inputevent.h>
#include <rexx/storage.h>
#include <rexx/errors.h>
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
/* RexxSysBase is declared in proto/rexxsyslib.h - no need to redeclare */

/* Reaction class library bases */
struct ClassLibrary *RequesterBase = NULL;

/* Reaction class handles */
Class *RequesterClass = NULL;

/* Forward declarations */
BOOL InitializeLibraries(VOID);
BOOL InitializeApplication(VOID);
VOID Cleanup(VOID);
BOOL IsDefIconsRunning(VOID);
VOID ShowErrorDialog(STRPTR title, STRPTR message);
BOOL ShowConfirmDialog(STRPTR fileName, STRPTR toolName);
BOOL OpenFileWithDefaultTool(STRPTR fileName, BPTR fileLock);
STRPTR GetFileTypeIdentifier(STRPTR fileName, BPTR fileLock);
STRPTR GetDefaultToolFromType(STRPTR typeIdentifier, STRPTR defIconNameOut, ULONG defIconNameSize);
BOOL IsProjectX(STRPTR toolName);
STRPTR GetProjectXName(struct WBStartup *wbs);
BOOL IsDirectory(STRPTR fileName, BPTR fileLock);
STRPTR GetToolTypeValue(struct DiskObject *icon, STRPTR toolTypeName);
BOOL IsLeftAmigaHeld(VOID);
BOOL SendRexxCommandToWorkbench(STRPTR command);
BOOL HandleDrawerMode(STRPTR drawerPath);

static const char *verstag = "$VER: ProjectX 47.1 (21.12.2025)\n";
static const char *stack_cookie = "$STACK: 4096\n";

/* Application variables */
static STRPTR projectXName = NULL;

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
        /* CLI mode - check for DRAWER option */
        struct RDArgs *rdargs;
        STRPTR drawerPath = NULL;
        STRPTR args[1] = {NULL}; /* DRAWER/K - one string pointer */
        CONST_STRPTR template = "DRAWER/K";
        LONG errorCode;
        
        /* Initialize libraries first (needed for ReadArgs and HandleDrawerMode) */
        if (!InitializeLibraries()) {
            return RETURN_FAIL;
        }
        
        SetIoErr(0);
        rdargs = ReadArgs(template, (LONG *)args, NULL);
        errorCode = IoErr();
        
        if (rdargs != NULL) {
            drawerPath = args[0];
            
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
            } else {
                /* ReadArgs succeeded but no DRAWER argument provided */
                FreeArgs(rdargs);
                Cleanup();
                return RETURN_FAIL;
            }
        } else {
            /* ReadArgs failed - show error and usage */
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
    
    /* Get our own name for loop detection */
    projectXName = GetProjectXName(wbs);
    
    /* Check if DefIcons is running */
    if (!IsDefIconsRunning()) {
        ShowErrorDialog("ProjectX", 
            "DefIcons is not running.\n\n"
            "ProjectX requires DefIcons to identify file types.\n"
            "Please start DefIcons and try again.");
        Cleanup();
        return RETURN_FAIL;
    }
    
    /* Check if we have any file arguments */
    if (wbs->sm_NumArgs <= 1) {
        /* No files to process - show error */
        ShowErrorDialog("ProjectX", "No file specified.\n\nProjectX must be set as the default tool on a project icon.");
        Cleanup();
        return RETURN_FAIL;
    }
    
    /* Process each file argument (skip index 0 which is our tool) */
    for (i = 1, wbarg = &wbs->sm_ArgList[i]; i < wbs->sm_NumArgs; i++, wbarg++) {
        BPTR oldDir = NULL;
        
        if (wbarg->wa_Lock && wbarg->wa_Name && *wbarg->wa_Name) {
            /* Change to the file's directory */
            oldDir = CurrentDir(wbarg->wa_Lock);
            
            /* Open the file with its default tool */
            if (!OpenFileWithDefaultTool(wbarg->wa_Name, wbarg->wa_Lock)) {
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
        PutStr("ProjectX: Failed to open intuition.library\n");
        return FALSE;
    }
    
    /* Open utility.library */
    UtilityBase = OpenLibrary("utility.library", 47L);
    if (UtilityBase == NULL) {
        PutStr("ProjectX: Failed to open utility.library\n");
        CloseLibrary((struct Library *)IntuitionBase);
        IntuitionBase = NULL;
        return FALSE;
    }
    
    if (!(IconBase = OpenLibrary("icon.library", 47L))) {
        PutStr("ProjectX: Failed to open icon.library (version 47 or higher required)\n");
        CloseLibrary(UtilityBase);
        UtilityBase = NULL;
        CloseLibrary((struct Library *)IntuitionBase);
        IntuitionBase = NULL;
        return FALSE;
    }
    
    if (!(WorkbenchBase = OpenLibrary("workbench.library", 44L))) {
        PutStr("ProjectX: Failed to open workbench.library\n");
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
    
    /* Open rexxsyslib.library for ARexx commands (optional - not critical) */
    RexxSysBase = OpenLibrary("rexxsyslib.library", 0);
    
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
    
    if (RexxSysBase != NULL) {
        CloseLibrary(RexxSysBase);
        RexxSysBase = NULL;
    }
    
    if (projectXName != NULL) {
        FreeVec(projectXName);
        projectXName = NULL;
    }
}

/* Check if DefIcons is running by looking for its message port */
BOOL IsDefIconsRunning(VOID)
{
    struct MsgPort *port;
    
    /* Look for the DEFICONS message port */
    port = FindPort("DEFICONS");
    
    if (port != NULL) {
        return TRUE;
    }
    return FALSE;
}

/* Get file type identifier using icon.library identification */
STRPTR GetFileTypeIdentifier(STRPTR fileName, BPTR fileLock)
{
    static UBYTE typeBuffer[256];
    struct TagItem tags[4];
    LONG errorCode = 0;
    struct DiskObject *icon = NULL;
    BPTR oldDir = NULL;
    
    /* Initialize buffer */
    typeBuffer[0] = '\0';
    
    /* Change to file's directory for identification */
    if (fileLock != NULL) {
        oldDir = CurrentDir(fileLock);
    }
    
    /* Set up tags for identification only */
    tags[0].ti_Tag = ICONGETA_IdentifyBuffer;
    tags[0].ti_Data = (ULONG)typeBuffer;
    tags[1].ti_Tag = ICONGETA_IdentifyOnly;
    tags[1].ti_Data = TRUE;
    tags[2].ti_Tag = ICONA_ErrorCode;
    tags[2].ti_Data = (ULONG)&errorCode;
    tags[3].ti_Tag = TAG_DONE;
    
    /* Get file type identifier */
    /* Note: With ICONGETA_IdentifyOnly, GetIconTagList returns NULL */
    /* but the type identifier is placed in the buffer */
    icon = GetIconTagList(fileName, tags);
    
    /* If icon was returned (shouldn't happen with IdentifyOnly), free it */
    if (icon) {
        FreeDiskObject(icon);
    }
    
    /* Restore original directory */
    if (oldDir != NULL) {
        CurrentDir(oldDir);
    }
    
    /* Return the type identifier, or NULL if identification failed */
    /* Check both errorCode and that buffer has content */
    if (errorCode == 0 && typeBuffer[0] != '\0') {
        return typeBuffer;
    }
    
    return NULL;
}

/* Get default tool from file type identifier */
/* Returns the default tool, or NULL if not found */
/* defIconNameOut will contain the name of the def_ icon that was tried */
STRPTR GetDefaultToolFromType(STRPTR typeIdentifier, STRPTR defIconNameOut, ULONG defIconNameSize)
{
    struct DiskObject *defaultIcon = NULL;
    STRPTR defaultTool = NULL;
    UBYTE defIconName[64];
    BPTR oldDir = NULL;
    BPTR envDir = NULL;
    
    if (!typeIdentifier || *typeIdentifier == '\0') {
        if (defIconNameOut && defIconNameSize > 0) {
            defIconNameOut[0] = '\0';
        }
        return NULL;
    }
    
    /* Construct default icon name: def_XXX using SNPrintf */
    SNPrintf(defIconName, sizeof(defIconName), "def_%s", typeIdentifier);
    
    /* Copy to output buffer */
    if (defIconNameOut && defIconNameSize > 0) {
        SNPrintf(defIconNameOut, defIconNameSize, "%s", defIconName);
    }
    
    /* Get the default icon from ENVARC:Sys/ or ENV:Sys/ */
    /* Use GetDiskObject directly, same as the diagnostic code */
    
    /* Try ENV:Sys first */
    if ((envDir = Lock("ENV:Sys", SHARED_LOCK)) != NULL) {
        oldDir = CurrentDir(envDir);
        defaultIcon = GetDiskObject(defIconName);
        CurrentDir(oldDir);
        UnLock(envDir);
    }
    
    /* If not found, try ENVARC:Sys */
    if (!defaultIcon && (envDir = Lock("ENVARC:Sys", SHARED_LOCK)) != NULL) {
        oldDir = CurrentDir(envDir);
        defaultIcon = GetDiskObject(defIconName);
        CurrentDir(oldDir);
        UnLock(envDir);
    }
    
    if (defaultIcon) {
        /* Extract default tool from the icon */
        /* do_DefaultTool is a STRPTR - check if it's not NULL and not empty */
        if (defaultIcon->do_DefaultTool != NULL) {
            /* Check if the string has content (not just a null terminator) */
            if (defaultIcon->do_DefaultTool[0] != '\0') {
                /* Copy the default tool string before freeing the DiskObject */
                UBYTE toolBuffer[256];
                ULONG toolLen;
                
                toolLen = strlen(defaultIcon->do_DefaultTool);
                /* Pass the full buffer size (256) to Strncpy - it will handle truncation and null-termination */
                Strncpy(toolBuffer, defaultIcon->do_DefaultTool, 256);
                
                /* Allocate memory for the tool name to return */
                /* We need to allocate this because we're freeing the DiskObject */
                defaultTool = AllocVec(toolLen + 1, MEMF_CLEAR);
                if (defaultTool) {
                    Strncpy((UBYTE *)defaultTool, toolBuffer, toolLen + 1);
                }
            }
        }
        /* Note: If icon was found but has no default tool, defaultTool will be NULL */
        
        FreeDiskObject(defaultIcon);
    }
    
    return defaultTool;
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

/* Check if left Amiga key is currently held down */
BOOL IsLeftAmigaHeld(VOID)
{
    UWORD qualifier;
    
    if (InputBase == NULL) {
        return FALSE;
    }
    
    /* PeekQualifier returns the current qualifier state */
    qualifier = PeekQualifier();
    
    /* Check if left Amiga key (LCOMMAND) is held */
    if (qualifier & IEQUALIFIER_RSHIFT) {
        return TRUE;
    }
    
    return FALSE;
}

/* Send an ARexx command to Workbench (non-blocking) */
BOOL SendRexxCommandToWorkbench(STRPTR command)
{
    struct RexxMsg *rmsg;
    struct MsgPort *port;
    STRPTR argString;
    BOOL success = FALSE;
    
    if (RexxSysBase == NULL || command == NULL) {
        return FALSE;
    }
    
    /* Find the Workbench ARexx port */
    port = FindPort("WORKBENCH");
    if (port == NULL) {
        return FALSE;
    }
    
    /* Create an argument string for the command */
    argString = CreateArgstring(command, strlen(command));
    if (argString == NULL) {
        return FALSE;
    }
    
    /* Create a Rexx message */
    rmsg = CreateRexxMsg(port, NULL, NULL);
    if (rmsg != NULL) {
        /* Fill the message with our command */
        /* RXCOMM = command type, RXFF_RESULT = request result string */
        /* FillRexxMsg takes: rmsg, action, flags */
        FillRexxMsg(rmsg, RXCOMM, RXFF_RESULT);
        /* Set the argument string directly in the message */
        rmsg->rm_Args[0] = argString;
        
        /* Send the message asynchronously - don't wait for reply */
        /* This avoids deadlock - Workbench will process it after we exit */
        PutMsg(port, (struct Message *)rmsg);
        
        /* Don't wait for reply - just assume success if message was sent */
        /* Workbench will process the command after ProjectX exits */
        success = TRUE;
        
        /* Note: We don't clean up the message here - Workbench will reply to it */
        /* and the message will be cleaned up by the system */
    }
    
    /* Free the argument string */
    if (argString != NULL) {
        DeleteArgstring(argString);
    }
    
    return success;
}

/* Check if tool name is ProjectX (to prevent infinite loops) */
BOOL IsProjectX(STRPTR toolName)
{
    if (!toolName || !projectXName) {
        return FALSE;
    }
    
    /* Compare tool name with our name (case-insensitive) */
    /* Stricmp returns 0 if strings match, non-zero otherwise */
    if (Stricmp(toolName, projectXName) == 0) {
        return TRUE;
    }
    return FALSE;
}

/* Get ProjectX executable name from WBStartup */
STRPTR GetProjectXName(struct WBStartup *wbs)
{
    static UBYTE nameBuffer[256];
    STRPTR fileName;
    STRPTR filePart;
    
    if (wbs && wbs->sm_ArgList && wbs->sm_ArgList[0].wa_Name) {
        /* Get the filename from the first argument (which is our tool) */
        fileName = wbs->sm_ArgList[0].wa_Name;
        filePart = FilePart(fileName);
        
        if (filePart && *filePart) {
            /* Copy just the filename part (without path) */
            Strncpy(nameBuffer, filePart, 255);
            nameBuffer[255] = '\0';
            return nameBuffer;
        }
    }
    
    /* Fallback: use static name */
    Strncpy(nameBuffer, "ProjectX", 255);
    nameBuffer[255] = '\0';
    return nameBuffer;
}

/* Show error dialog */
VOID ShowErrorDialog(STRPTR title, STRPTR message)
{
    Object *reqobj;
    
    if (RequesterClass == NULL) {
        /* Requester class not available - use PutStr as fallback */
        PutStr("ProjectX Error: ");
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
BOOL ShowConfirmDialog(STRPTR fileName, STRPTR toolName)
{
    Object *reqobj;
    char title[256];
    UWORD qualifier;
    BOOL shiftHeld;
    UBYTE shiftStatus[64];
    char message[512];
    ULONG result;
    
    if (RequesterClass == NULL) {
        /* Requester class not available - default to yes */
        return TRUE;
    }
    
    /* Format the title and message */
    Strncpy(title, "ProjectX", 255);
    title[255] = '\0';
    
    /* Check left Amiga key status */
    if (InputBase != NULL) {
        qualifier = PeekQualifier();
        shiftHeld = (qualifier & IEQUALIFIER_RSHIFT) ? TRUE : FALSE;
        SNPrintf(shiftStatus, sizeof(shiftStatus),
            "Left Amiga: %s (qualifier=0x%04x, InputBase=0x%08lx)",
            shiftHeld ? "HELD" : "NOT HELD",
            qualifier,
            (ULONG)InputBase);
    } else {
        SNPrintf(shiftStatus, sizeof(shiftStatus),
            "Left Amiga: UNAVAILABLE (InputBase=%p)",
            InputBase);
    }
    
    sprintf(message,
            "\n\n"
            "File: %s\n\n"
            "Tool: %s\n\n"
            "%s\n\n"
            "Launch this tool?\n\n",
            fileName, toolName, shiftStatus);
    
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

/* Open file with its default tool */
BOOL OpenFileWithDefaultTool(STRPTR fileName, BPTR fileLock)
{
    STRPTR typeIdentifier = NULL;
    STRPTR defaultTool = NULL;
    BOOL success = FALSE;
    BOOL confirmed = FALSE;
    struct TagItem tags[3];
    UBYTE defIconName[64];
    UBYTE errorMsg[512];
    struct DiskObject *projectIcon = NULL;
    STRPTR toolboxValue = NULL;
    UBYTE fullToolPath[512];
    LONG errorCode;
    
    /* Special case: Check if this is a directory (drawer) */
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
            ShowErrorDialog("ProjectX",
                "Path too long.\n\n"
                "The directory path is too long to process.");
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
                ShowErrorDialog("ProjectX",
                    "Path too long.\n\n"
                    "The icon file path is too long to process.");
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
                ShowErrorDialog("ProjectX", errorMsg);
                return FALSE;
            }
        
        /* Get the TOOLBOX tooltype value */
        toolboxValue = GetToolTypeValue(projectIcon, "TOOLBOX");
        
        if (toolboxValue == NULL || *toolboxValue == '\0') {
            FreeDiskObject(projectIcon);
            ShowErrorDialog("ProjectX",
                "No TOOLBOX tooltype found.\n\n"
                "This directory icon must have a TOOLBOX tooltype\n"
                "specifying the application to run.");
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
            ShowErrorDialog("ProjectX",
                "Path too long.\n\n"
                "The tool path is too long to process.");
            return FALSE;
        }
        
        /* Check if left Amiga key is held - if so, spawn second process to open drawer */
        if (IsLeftAmigaHeld()) {
            /* Left Amiga key held - spawn a second process to handle drawer opening */
            /* This avoids the ERROR_OBJECT_IN_USE issue because the second process */
            /* doesn't have the WBStartup lock */
            UBYTE command[512];
            struct TagItem sysTags[2];
            LONG sysResult;
            
            /* Free the icon before spawning the process */
            FreeDiskObject(projectIcon);
            projectIcon = NULL;
            
            /* Build command: Use PROGDIR: to get full path to ProjectX */
            {
                BPTR progDirLock;
                UBYTE progPath[256];
                
                progDirLock = Lock("PROGDIR:", ACCESS_READ);
                if (progDirLock != NULL) {
                    NameFromLock(progDirLock, progPath, sizeof(progPath));
                    UnLock(progDirLock);
                    SNPrintf(command, sizeof(command), "%s/ProjectX DRAWER=%s", progPath, fullDirPath);
                } else {
                    /* Fallback: try without path (assumes ProjectX is in PATH) */
                    SNPrintf(command, sizeof(command), "ProjectX DRAWER=%s", fullDirPath);
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
                ShowErrorDialog("ProjectX", errorMsg);
                return FALSE;
            }
            
            /* Success - second process spawned, primary process exits immediately */
            return TRUE;
        }
        
        /* Free the icon BEFORE showing dialog */
        FreeDiskObject(projectIcon);
        projectIcon = NULL;
        
        /* Left Amiga key not held - show confirmation dialog with the full directory path */
        confirmed = ShowConfirmDialog(fullDirPath, toolboxValueCopy);
        if (!confirmed) {
            /* User clicked No - return FALSE but don't show error */
            return FALSE;
        }
        
        /* User clicked Yes - launch the tool */
        /* Launch the tool without any arguments */
        tags[0].ti_Tag = TAG_DONE;
        
        /* Clear any previous error */
        SetIoErr(0);
        
        success = OpenWorkbenchObjectA(fullToolPath, tags);
        
        /* Check IoErr() regardless of return value, as OpenWorkbenchObjectA may return TRUE even on failure */
        errorCode = IoErr();
        
        if (!success || errorCode != 0) {
            /* OpenWorkbenchObjectA failed - show error code */
            UBYTE errorMsg[512];
            
            SNPrintf(errorMsg, sizeof(errorMsg),
                "Failed to launch tool.\n\n"
                "Tool path: %s\n\n"
                "Return value: %s\n"
                "Error code: %ld\n\n"
                "The tool could not be launched.\n"
                "Please check that the tool exists and has an icon.",
                fullToolPath, success ? "TRUE" : "FALSE", errorCode);
            ShowErrorDialog("ProjectX", errorMsg);
            return FALSE;
        }
        
        /* Success - tool was launched */
        return TRUE;
    }
    
    /* Normal case: Regular file, use DefIcons lookup */
    /* Step 1: Get file type identifier */
    typeIdentifier = GetFileTypeIdentifier(fileName, fileLock);
    
    if (!typeIdentifier || *typeIdentifier == '\0') {
        /* File type could not be identified */
        /* DefIcons is running (we checked earlier), so file type is unknown */
        ShowErrorDialog("ProjectX", 
            "Could not identify file type.\n\n"
            "The file type is not recognized by DefIcons.\n"
            "You may need to add a rule for this file type\n"
            "in DefIcons preferences.");
        return FALSE;
    }
    
    /* Step 2: Get default tool for this file type */
    defIconName[0] = '\0';
    defaultTool = GetDefaultToolFromType(typeIdentifier, defIconName, sizeof(defIconName));
    
    if (!defaultTool || *defaultTool == '\0') {
        /* No default tool found for this file type */
        /* Try to determine if icon exists and what its default tool actually is */
        BOOL iconExists = FALSE;
        UBYTE actualDefaultTool[256];
        struct DiskObject *testIcon = NULL;
        BPTR testOldDir = NULL;
        BPTR testEnvDir = NULL;
        ULONG toolLen;
        
        actualDefaultTool[0] = '\0';
        
        /* Check if icon file exists in ENV:Sys and read its default tool */
        if ((testEnvDir = Lock("ENV:Sys", SHARED_LOCK)) != NULL) {
            testOldDir = CurrentDir(testEnvDir);
            testIcon = GetDiskObject(defIconName);
            if (testIcon) {
                iconExists = TRUE;
                /* Check what the default tool actually is */
                /* Copy the string before freeing the DiskObject */
                if (testIcon->do_DefaultTool != NULL && testIcon->do_DefaultTool[0] != '\0') {
                    toolLen = strlen(testIcon->do_DefaultTool);
                    /* Pass the full buffer size (256) to Strncpy - it will handle truncation and null-termination */
                    /* Buffer is 256 bytes, so Strncpy can copy up to 255 chars + null terminator */
                    Strncpy(actualDefaultTool, testIcon->do_DefaultTool, 256);
                    /* Strncpy automatically null-terminates, even if truncated */
                }
                FreeDiskObject(testIcon);
            }
            CurrentDir(testOldDir);
            UnLock(testEnvDir);
        }
        
        /* If not found, check ENVARC:Sys */
        if (!iconExists && (testEnvDir = Lock("ENVARC:Sys", SHARED_LOCK)) != NULL) {
            testOldDir = CurrentDir(testEnvDir);
            testIcon = GetDiskObject(defIconName);
            if (testIcon) {
                iconExists = TRUE;
                /* Check what the default tool actually is */
                /* Copy the string before freeing the DiskObject */
                if (testIcon->do_DefaultTool != NULL && testIcon->do_DefaultTool[0] != '\0') {
                    toolLen = strlen(testIcon->do_DefaultTool);
                    /* Pass the full buffer size (256) to Strncpy - it will handle truncation and null-termination */
                    /* Buffer is 256 bytes, so Strncpy can copy up to 255 chars + null terminator */
                    Strncpy(actualDefaultTool, testIcon->do_DefaultTool, 256);
                    /* Strncpy automatically null-terminates, even if truncated */
                }
                FreeDiskObject(testIcon);
            }
            CurrentDir(testOldDir);
            UnLock(testEnvDir);
        }
        
        /* Build detailed error message */
        if (iconExists) {
            if (actualDefaultTool[0] != '\0') {
                /* Icon exists and has a default tool, but GetDefaultToolFromType didn't find it */
                /* This suggests a bug in our lookup code */
                sprintf(errorMsg,
                    "Default tool lookup failed.\n\n"
                    "File type: %s\n"
                    "Default icon: %s.info\n"
                    "Default tool in icon: %s\n\n"
                    "The icon exists and has a default tool,\n"
                    "but ProjectX could not retrieve it.\n"
                    "This may be a bug in ProjectX.",
                    typeIdentifier,
                    (STRPTR)(defIconName[0] != '\0' ? (STRPTR)defIconName : (STRPTR)(UBYTE *)"(unknown)"),
                    actualDefaultTool);
            } else {
                /* Icon exists but has no default tool */
                sprintf(errorMsg,
                    "No default tool found.\n\n"
                    "File type: %s\n"
                    "Default icon: %s.info\n\n"
                    "The default icon exists but does not have\n"
                    "a default tool specified.\n\n"
                    "Please edit the icon and set a default tool.",
                    typeIdentifier,
                    (STRPTR)(defIconName[0] != '\0' ? (STRPTR)defIconName : (STRPTR)(UBYTE *)"(unknown)"));
            }
        } else {
            /* Icon does not exist */
            sprintf(errorMsg,
                "No default tool found.\n\n"
                "File type: %s\n"
                "Default icon: %s.info\n\n"
                "The default icon does not exist in\n"
                "ENV:Sys/ or ENVARC:Sys/.\n\n"
                "You may need to create this icon.",
                typeIdentifier,
                (STRPTR)(defIconName[0] != '\0' ? defIconName : "(unknown)"));
        }
        ShowErrorDialog("ProjectX", errorMsg);
        return FALSE;
    }
    
    /* Step 3: Check for infinite loop - is the default tool ProjectX? */
    if (IsProjectX(defaultTool)) {
        /* Prevent infinite loop */
        if (defaultTool) {
            FreeVec(defaultTool);
        }
        return FALSE;
    }
    
    /* Step 4: Show confirmation dialog */
    confirmed = ShowConfirmDialog(fileName, defaultTool);
    if (!confirmed) {
        /* User cancelled */
        if (defaultTool) {
            FreeVec(defaultTool);
        }
        return FALSE;
    }
    
    /* Step 5: Open the file using OpenWorkbenchObjectA */
    /* Build TagItem array for OpenWorkbenchObjectA */
    tags[0].ti_Tag = WBOPENA_ArgLock;
    tags[0].ti_Data = (ULONG)fileLock;
    tags[1].ti_Tag = WBOPENA_ArgName;
    tags[1].ti_Data = (ULONG)fileName;
    tags[2].ti_Tag = TAG_DONE;
    
    /* Clear any previous error */
    SetIoErr(0);
    
    success = OpenWorkbenchObjectA(defaultTool, tags);
    
    /* Check IoErr() regardless of return value, as OpenWorkbenchObjectA may return TRUE even on failure */
    errorCode = IoErr();
    
    if (!success || errorCode != 0) {
        /* OpenWorkbenchObjectA failed - show error code */
        UBYTE errorMsg[512];
        
        SNPrintf(errorMsg, sizeof(errorMsg),
            "Failed to launch tool.\n\n"
            "Tool: %s\n"
            "File: %s\n\n"
            "Return value: %s\n"
            "Error code: %ld\n\n"
            "The tool could not be launched.\n"
            "Please check that the tool exists and is executable.",
            defaultTool, fileName, success ? "TRUE" : "FALSE", errorCode);
        ShowErrorDialog("ProjectX", errorMsg);
    }
    
    /* Free the allocated default tool string */
    if (defaultTool) {
        FreeVec(defaultTool);
    }
    
    return success;
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