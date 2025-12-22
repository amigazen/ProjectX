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
        /* CLI mode - not supported yet */
        PutStr("ProjectX must be started from Workbench\n");
        return RETURN_FAIL;
    }
    
    /* Get WBStartup message */
    wbs = (struct WBStartup *)argv;
    
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
    struct TagItem tags[4];
    LONG errorCode = 0;
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
    char message[512];
    ULONG result;
    
    if (RequesterClass == NULL) {
        /* Requester class not available - default to yes */
        return TRUE;
    }
    
    /* Format the title and message */
    Strncpy(title, "ProjectX", 255);
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
        if (result == 1) {
            return TRUE;
        }
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
    struct TagItem tags[4];
    UBYTE defIconName[64];
    UBYTE errorMsg[512];
    
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
                    defIconName[0] != '\0' ? (STRPTR)defIconName : "(unknown)",
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
                    defIconName[0] != '\0' ? (STRPTR)defIconName : "(unknown)");
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
                defIconName[0] != '\0' ? (STRPTR)defIconName : "(unknown)");
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
    
    success = OpenWorkbenchObjectA(defaultTool, tags);
    
    /* Free the allocated default tool string */
    if (defaultTool) {
        FreeVec(defaultTool);
    }
    
    return success;
}