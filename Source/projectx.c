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
#include <devices/input.h>
#include <devices/inputevent.h>
#include <dos/rdargs.h>
#include <dos/dostags.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>

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

/* Log file handle */
static BPTR logFile = NULL;

/* Forward declarations */
VOID LogMessage(STRPTR format, ...);
BOOL InitializeLibraries(VOID);
BOOL InitializeApplication(VOID);
VOID Cleanup(VOID);
BOOL IsDefIconsRunning(VOID);
VOID ShowErrorDialog(STRPTR title, STRPTR message);
BOOL OpenFileWithDefaultTool(STRPTR fileName, BPTR fileLock);
STRPTR GetFileTypeIdentifier(STRPTR fileName, BPTR fileLock);
STRPTR GetDefaultToolFromType(STRPTR typeIdentifier, STRPTR defIconNameOut, ULONG defIconNameSize);
BOOL IsProjectX(STRPTR toolName);
STRPTR GetProjectXName(struct WBStartup *wbs);
BOOL IsLeftShiftHeld(VOID);

static const char *verstag = "$VER: ProjectX 47.2 (2/1/2026)\n";
static const char *stack_cookie = "$STACK: 4096\n";
const long oslibversion = 47L;

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
        /* CLI mode - parse arguments and handle file */
        struct RDArgs *rdargs;
        STRPTR fileName = NULL;
        LONG openFlag = 0; /* OPEN/S - boolean switch */
        LONG args[2] = {0, 0}; /* FILE/A, OPEN/S */
        CONST_STRPTR template = "FILE/A,OPEN/S";
        LONG errorCode;
        STRPTR typeIdentifier = NULL;
        STRPTR defaultTool = NULL;
        UBYTE defIconName[64];
        BPTR fileLock = NULL;
        BPTR oldDir = NULL;
        BOOL success = FALSE;
        struct TagItem tags[3];
        
        /* Initialize libraries first (needed for ReadArgs and file operations) */
        if (!InitializeLibraries()) {
            return RETURN_FAIL;
        }
        
        /* Check if DefIcons is running */
        if (!IsDefIconsRunning()) {
            PutStr("ProjectX: DefIcons is not running.\n");
            PutStr("ProjectX requires DefIcons to identify file types.\n");
            Cleanup();
            return RETURN_FAIL;
        }
        
        SetIoErr(0);
        rdargs = ReadArgs(template, (LONG *)args, NULL);
        errorCode = IoErr();
        
        if (rdargs == NULL || errorCode != 0) {
            /* ReadArgs failed - show usage */
            PutStr("Usage: ProjectX FILE/A [OPEN/S]\n");
            PutStr("  FILE/A  - File to get default tool for\n");
            PutStr("  OPEN/S - If set, immediately launch the tool with the file\n");
            PutStr("           If not set, print the default tool name\n");
            if (rdargs != NULL) {
                FreeArgs(rdargs);
            }
            Cleanup();
            return RETURN_FAIL;
        }
        
        fileName = (STRPTR)args[0];
        openFlag = args[1]; /* OPEN/S - 1 if set, 0 if not */
        
        if (fileName == NULL || *fileName == '\0') {
            PutStr("ProjectX: No file specified.\n");
            FreeArgs(rdargs);
            Cleanup();
            return RETURN_FAIL;
        }
        
        /* Lock the file to get its directory */
        fileLock = Lock((UBYTE *)fileName, SHARED_LOCK);
        if (fileLock == NULL) {
            PutStr("ProjectX: Could not lock file.\n");
            FreeArgs(rdargs);
            Cleanup();
            return RETURN_FAIL;
        }
        
        /* Get the file's directory lock and filename part */
        {
            BPTR parentLock;
            STRPTR filePartPtr;
            UBYTE fileNameCopy[256];
            STRPTR fileNamePart = NULL;
            
            /* Get just the filename part */
            filePartPtr = FilePart(fileName);
            
            /* Make a copy of the filename part to ensure it's valid */
            /* FilePart returns a pointer into the original string, which may become invalid */
            if (filePartPtr != NULL && *filePartPtr != '\0') {
                /* Use full buffer size - Strncpy will handle truncation and null-termination */
                Strncpy(fileNameCopy, filePartPtr, sizeof(fileNameCopy));
                fileNamePart = fileNameCopy;
            } else {
                /* Fallback: use the original fileName if FilePart fails */
                fileNamePart = fileName;
            }
            
            /* Get parent directory lock */
            parentLock = ParentDir(fileLock);
            UnLock(fileLock);
            
            if (parentLock == NULL) {
                PutStr("ProjectX: Could not get parent directory.\n");
                FreeArgs(rdargs);
                Cleanup();
                return RETURN_FAIL;
            }
            
            fileLock = parentLock;
            oldDir = CurrentDir(fileLock);
            
            /* Get file type identifier using filename and directory lock */
            typeIdentifier = GetFileTypeIdentifier(fileNamePart, fileLock);
            
            if (!typeIdentifier || *typeIdentifier == '\0') {
                PutStr("ProjectX: Could not identify file type.\n");
                if (oldDir != NULL) {
                    CurrentDir(oldDir);
                }
                UnLock(fileLock);
                FreeArgs(rdargs);
                Cleanup();
                return RETURN_FAIL;
            }
            
            /* Get default tool from deficon */
            defIconName[0] = '\0';
            defaultTool = GetDefaultToolFromType(typeIdentifier, defIconName, sizeof(defIconName));
            
            if (!defaultTool || *defaultTool == '\0') {
                PutStr("ProjectX: No default tool found for this file type.\n");
                if (oldDir != NULL) {
                    CurrentDir(oldDir);
                }
                UnLock(fileLock);
                FreeArgs(rdargs);
                Cleanup();
                return RETURN_FAIL;
            }
            
            if (openFlag == 0) {
                /* OPEN/S not set - just print the default tool name */
                PutStr(defaultTool);
                PutStr("\n");
                success = TRUE;
            } else {
                /* OPEN/S set - launch the tool with the file */
                /* Build TagItem array for OpenWorkbenchObjectA */
                tags[0].ti_Tag = WBOPENA_ArgLock;
                tags[0].ti_Data = (ULONG)fileLock;
                tags[1].ti_Tag = WBOPENA_ArgName;
                tags[1].ti_Data = (ULONG)fileNamePart;
                tags[2].ti_Tag = TAG_DONE;
                
                /* Clear any previous error */
                SetIoErr(0);
                
                success = OpenWorkbenchObjectA(defaultTool, tags);
                errorCode = IoErr();
                
                if (!success || errorCode != 0) {
                    PutStr("ProjectX: Failed to launch tool.\n");
                    success = FALSE;
                }
            }
        }
        
        
        /* Restore original directory */
        if (oldDir != NULL) {
            CurrentDir(oldDir);
        }
        
        /* Free resources */
        UnLock(fileLock);
        if (defaultTool != NULL) {
            FreeVec(defaultTool);
        }
        FreeArgs(rdargs);
        Cleanup();
        
        return success ? RETURN_OK : RETURN_FAIL;
    }
    
    /* Get WBStartup message */
    wbs = (struct WBStartup *)argv;
    
    /* LogMessage("ProjectX: Starting, argc=%ld\n", argc); */
    
    /* Initialize libraries */
    if (!InitializeLibraries()) {
        /* LogMessage("ProjectX: InitializeLibraries failed\n"); */
        return RETURN_FAIL;
    }
    
    /* Initialize application (requester.class) */
    if (!InitializeApplication()) {
        /* LogMessage("ProjectX: InitializeApplication failed\n"); */
        Cleanup();
        return RETURN_FAIL;
    }
    
    /* LogMessage("ProjectX: Application initialized\n"); */
    
    /* Get our own name for loop detection */
    projectXName = GetProjectXName(wbs);
    /* LogMessage("ProjectX: Our name is %s\n", projectXName); */
    
    /* Check if DefIcons is running */
    if (!IsDefIconsRunning()) {
        /* LogMessage("ProjectX: DefIcons is not running\n"); */
        ShowErrorDialog("ProjectX", 
            "DefIcons is not running.\n\n"
            "ProjectX requires DefIcons to identify file types.\n"
            "Please start DefIcons and try again.");
        Cleanup();
        return RETURN_FAIL;
    }
    
    /* LogMessage("ProjectX: DefIcons is running\n"); */
    
    /* Check if we have any file arguments */
    if (wbs->sm_NumArgs <= 1) {
        /* LogMessage("ProjectX: No file arguments\n"); */
        /* No files to process - show error */
        ShowErrorDialog("ProjectX", "No file specified.\n\nProjectX must be set as the default tool on a project icon.");
        Cleanup();
        return RETURN_FAIL;
    }
    
    /* LogMessage("ProjectX: Processing %ld file arguments\n", wbs->sm_NumArgs - 1); */
    
    /* Process each file argument (skip index 0 which is our tool) */
    for (i = 1, wbarg = &wbs->sm_ArgList[i]; i < wbs->sm_NumArgs; i++, wbarg++) {
        BPTR oldDir = NULL;
        
        if (wbarg->wa_Lock && wbarg->wa_Name && *wbarg->wa_Name) {
            /* LogMessage("ProjectX: Processing file %ld: %s\n", i, wbarg->wa_Name); */
            
            /* Change to the file's directory */
            oldDir = CurrentDir(wbarg->wa_Lock);
            
            /* Open the file with its default tool */
            if (!OpenFileWithDefaultTool(wbarg->wa_Name, wbarg->wa_Lock)) {
                /* LogMessage("ProjectX: OpenFileWithDefaultTool failed for %s\n", wbarg->wa_Name); */
                success = FALSE;
            } else {
                /* LogMessage("ProjectX: OpenFileWithDefaultTool succeeded for %s\n", wbarg->wa_Name); */
            }
            
            /* Restore original directory */
            if (oldDir != NULL) {
                CurrentDir(oldDir);
            }
        }
    }
    
    /* LogMessage("ProjectX: Finished processing files, success=%ld\n", success); */
    
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
        return FALSE;
    }
    
    /* Open utility.library */
    UtilityBase = OpenLibrary("utility.library", 47L);
    if (UtilityBase == NULL) {
        CloseLibrary((struct Library *)IntuitionBase);
        IntuitionBase = NULL;
        return FALSE;
    }
    
    if (!(IconBase = OpenLibrary("icon.library", 47L))) {
        CloseLibrary(UtilityBase);
        UtilityBase = NULL;
        CloseLibrary((struct Library *)IntuitionBase);
        IntuitionBase = NULL;
        return FALSE;
    }
    
    if (!(WorkbenchBase = OpenLibrary("workbench.library", 44L))) {
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
    
    /* Open log file */
    /* logFile = Open("codecraft:projectx.log", MODE_NEWFILE); */
    /* if (logFile == NULL) { */
    /*     Try to open existing file for append */
    /*     logFile = Open("codecraft:projectx.log", MODE_READWRITE); */
    /*     if (logFile != NULL) { */
    /*         Seek(logFile, 0, OFFSET_END); */
    /*     } */
    /* } */
    
    /* LogMessage("ProjectX: Libraries initialized\n"); */
    
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

/* Log message to file */
VOID LogMessage(STRPTR format, ...)
{
    UBYTE buffer[512];
    LONG result;
    va_list args;
    
    if (logFile == NULL || UtilityBase == NULL) {
        return;
    }
    
    va_start(args, format);
    result = VSNPrintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    
    if (result > 0 && result < sizeof(buffer)) {
        Write(logFile, buffer, result);
        Flush(logFile);
    }
}

/* Cleanup libraries */
VOID Cleanup(VOID)
{
    /* LogMessage("ProjectX: Cleanup starting\n"); */
    
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
    
    /* if (logFile != NULL) { */
    /*     Close(logFile); */
    /*     logFile = NULL; */
    /* } */
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

/* Check if Left Shift key is currently held down */
BOOL IsLeftShiftHeld(VOID)
{
    UWORD qualifier;
    
    if (InputBase == NULL) {
        return FALSE;
    }
    
    /* PeekQualifier returns the current qualifier state */
    qualifier = PeekQualifier();
    
    /* Check if Left Shift key is held */
    if (qualifier & IEQUALIFIER_LSHIFT) {
        return TRUE;
    }
    
    return FALSE;
}

/* Show error dialog */
VOID ShowErrorDialog(STRPTR title, STRPTR message)
{
    Object *reqobj;
    
    if (RequesterClass == NULL) {
        /* Requester class not available - log error */
        /* LogMessage("ProjectX Error: %s\n", message); */
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
/* BOOL ShowConfirmDialog(STRPTR fileName, STRPTR toolName) */
/* { */
/*     Object *reqobj; */
/*     char title[256]; */
/*     char message[512]; */
/*     ULONG result; */
/*      */
/*     if (RequesterClass == NULL) { */
/*         Requester class not available - default to yes */
/*         return TRUE; */
/*     } */
/*      */
/*     Format the title and message */
/*     Strncpy(title, "ProjectX", 255); */
/*     title[255] = '\0'; */
/*      */
/*     SNPrintf(message, sizeof(message), */
/*             "\n\n" */
/*             "File: %s\n\n" */
/*             "Tool: %s\n\n" */
/*             "Launch this tool?\n\n", */
/*             fileName, toolName); */
/*      */
/*     LogMessage("ProjectX: About to create requester object\n"); */
/*     Flush(logFile); */
/*      */
/*     Create the requester object with confirmation type */
/*     reqobj = NewObject(RequesterClass, NULL, */
/*                        REQ_TitleText, title, */
/*                        REQ_BodyText, message, */
/*                        REQ_Type, REQTYPE_INFO, */
/*                        REQ_GadgetText, "Yes|No", */
/*                        REQ_Image, REQIMAGE_QUESTION, */
/*                        TAG_END); */
/*      */
/*     LogMessage("ProjectX: NewObject returned reqobj=%p\n", reqobj); */
/*     Flush(logFile); */
/*      */
/*     if (reqobj != NULL) { */
/*         LogMessage("ProjectX: Showing confirmation dialog for file=%s tool=%s\n", fileName, toolName); */
/*         LogMessage("ProjectX: About to call DoMethod(RM_OPENREQ)...\n"); */
/*         Flush(logFile); */
/*          */
/*         Show the requester and wait for user response */
/*         RM_OPENREQ returns button number: 1 = first button (Yes), 2 = second button (No) */
/*         result = DoMethod(reqobj, RM_OPENREQ, NULL, 0L, NULL, TAG_DONE); */
/*          */
/*         LogMessage("ProjectX: DoMethod(RM_OPENREQ) returned, result=%ld\n", result); */
/*         Flush(logFile); */
/*          */
/*         Clean up the requester object BEFORE checking result */
/*         This ensures the requester is closed before we proceed */
/*         LogMessage("ProjectX: About to dispose requester object\n"); */
/*         DisposeObject(reqobj); */
/*         reqobj = NULL; */
/*          */
/*         LogMessage("ProjectX: Requester object disposed\n"); */
/*         Flush(logFile); */
/*          */
/*         Return TRUE only if user clicked "Yes" (first button, result == 1) */
/*         Result values: 1 = first button (Yes), 2 = second button (No), 0 = cancelled/error */
/*         if (result == 1) { */
/*             LogMessage("ProjectX: User confirmed (result=%ld), proceeding to launch tool\n", result); */
/*             Flush(logFile); */
/*             return TRUE; */
/*         } */
/*         Result 0, 2, or higher means No, cancelled, or error */
/*         LogMessage("ProjectX: User cancelled or error (result=%ld), NOT launching tool\n", result); */
/*         Flush(logFile); */
/*         return FALSE; */
/*     } */
/*      */
/*     If requester creation failed, default to NO (don't launch) */
/*     LogMessage("ProjectX: Requester creation failed, defaulting to NO\n"); */
/*     Flush(logFile); */
/*     return FALSE; */
/* } */

/* Open file with its default tool */
BOOL OpenFileWithDefaultTool(STRPTR fileName, BPTR fileLock)
{
    STRPTR typeIdentifier = NULL;
    STRPTR defaultTool = NULL;
    BOOL success = FALSE;
    struct TagItem tags[4];
    UBYTE defIconName[64];
    UBYTE errorMsg[512];
    LONG errorCode;
    
    /* LogMessage("ProjectX: OpenFileWithDefaultTool called for file=%s\n", fileName); */
    
    /* Step 1: Get file type identifier */
    typeIdentifier = GetFileTypeIdentifier(fileName, fileLock);
    /* LogMessage("ProjectX: File type identifier=%s\n", typeIdentifier ? typeIdentifier : (STRPTR)"(null)"); */
    
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
    /* Check if Left Shift is held - if so, use MultiView instead of DefIcons default tool */
    if (IsLeftShiftHeld()) {
        /* Left Shift held - use MultiView as universal fallback viewer */
        defaultTool = AllocVec(strlen("MultiView") + 1, MEMF_CLEAR);
        if (defaultTool != NULL) {
            Strncpy((UBYTE *)defaultTool, "MultiView", strlen("MultiView") + 1);
        }
    } else {
        /* Normal path - get default tool from DefIcons */
        defIconName[0] = '\0';
        defaultTool = GetDefaultToolFromType(typeIdentifier, defIconName, sizeof(defIconName));
    }
    /* LogMessage("ProjectX: Default tool=%s defIconName=%s\n", defaultTool ? defaultTool : (STRPTR)"(null)", defIconName); */
    
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
                SNPrintf(errorMsg, sizeof(errorMsg),
                    "Default tool lookup failed.\n\n"
                    "File type: %s\n"
                    "Default icon: %s.info\n"
                    "Default tool in icon: %s\n\n"
                    "The icon exists and has a default tool,\n"
                    "but ProjectX could not retrieve it.\n"
                    "This may be a bug in ProjectX.",
                    typeIdentifier,
                    defIconName[0] != '\0' ? (STRPTR)defIconName : (STRPTR)"(unknown)",
                    actualDefaultTool);
            } else {
                /* Icon exists but has no default tool */
                SNPrintf(errorMsg, sizeof(errorMsg),
                    "No default tool found.\n\n"
                    "File type: %s\n"
                    "Default icon: %s.info\n\n"
                    "The default icon exists but does not have\n"
                    "a default tool specified.\n\n"
                    "Please edit the icon and set a default tool.",
                    typeIdentifier,
                    defIconName[0] != '\0' ? (STRPTR)defIconName : (STRPTR)"(unknown)");
            }
        } else {
            /* Icon does not exist */
            SNPrintf(errorMsg, sizeof(errorMsg),
                "No default tool found.\n\n"
                "File type: %s\n"
                "Default icon: %s.info\n\n"
                "The default icon does not exist in\n"
                "ENV:Sys/ or ENVARC:Sys/.\n\n"
                "You may need to create this icon.",
                typeIdentifier,
                defIconName[0] != '\0' ? (STRPTR)defIconName : (STRPTR)"(unknown)");
        }
        ShowErrorDialog("ProjectX", errorMsg);
        return FALSE;
    }
    
    /* Step 3: Check for infinite loop - is the default tool ProjectX? */
    if (IsProjectX(defaultTool)) {
        /* LogMessage("ProjectX: Infinite loop detected, default tool is ProjectX\n"); */
        /* Prevent infinite loop */
        if (defaultTool) {
            FreeVec(defaultTool);
        }
        return FALSE;
    }
    
    /* LogMessage("ProjectX: No infinite loop, proceeding to launch tool\n"); */
    
    /* Step 4: Open the file using OpenWorkbenchObjectA (no confirmation dialog) */
    /* LogMessage("ProjectX: About to call OpenWorkbenchObjectA tool=%s file=%s\n", defaultTool, fileName); */
    
    /* Build TagItem array for OpenWorkbenchObjectA */
    tags[0].ti_Tag = WBOPENA_ArgLock;
    tags[0].ti_Data = (ULONG)fileLock;
    tags[1].ti_Tag = WBOPENA_ArgName;
    tags[1].ti_Data = (ULONG)fileName;
    tags[2].ti_Tag = TAG_DONE;
    
    /* Clear any previous error */
    SetIoErr(0);
    
    /* LogMessage("ProjectX: Calling OpenWorkbenchObjectA...\n"); */
    success = OpenWorkbenchObjectA(defaultTool, tags);
    /* LogMessage("ProjectX: OpenWorkbenchObjectA returned success=%ld\n", success); */
    
    /* Check IoErr() regardless of return value, as OpenWorkbenchObjectA may return TRUE even on failure */
    errorCode = IoErr();
    /* LogMessage("ProjectX: IoErr() returned errorCode=%ld\n", errorCode); */
    
    if (!success || errorCode != 0) {
        /* OpenWorkbenchObjectA failed - show error code */
        /* LogMessage("ProjectX: OpenWorkbenchObjectA failed success=%ld errorCode=%ld\n", success, errorCode); */
        SNPrintf(errorMsg, sizeof(errorMsg),
            "Failed to launch tool.\n\n"
            "Tool: %s\n"
            "File: %s\n\n"
            "Error code: %ld\n\n"
            "The tool could not be launched.\n"
            "Please check that the tool exists.",
            defaultTool ? defaultTool : (STRPTR)"(null)",
            fileName ? fileName : (STRPTR)"(null)",
            errorCode);
        ShowErrorDialog("ProjectX", errorMsg);
        
        /* Free the allocated default tool string */
        if (defaultTool) {
            FreeVec(defaultTool);
        }
        return FALSE;
    }
    
    /* LogMessage("ProjectX: Tool launched successfully\n"); */
    
    /* Free the allocated default tool string */
    if (defaultTool) {
        FreeVec(defaultTool);
    }
    
    return success;
}