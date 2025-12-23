# ProjectX

Open Project files on Workbench with the DefIcons DefaultTool with ProjectX.

## About ProjectX

On Amiga Workbench, every project file can have a default tool specified in its `.info` metadata file. When you double-click a project icon, Workbench launches that default tool. However, this creates problems:

- Files sent to other users may reference tools that don't exist on their system
- You can't easily use different tools for the same file type
- Moving or uninstalling tools breaks file associations
- Files with `.info` metadata can't use the DefaultTools set in DefIcons icon templates

ProjectX solves this by acting as an intermediary: when set as a default tool on any project icon, it identifies the file type, looks up the appropriate default tool from DefIcons or system default icons, and launches that tool using the Workbench API.

## Key Features

- **Universal Compatibility** - Works with any project file, including those with `.info` metadata
- **DefIcons Integration** - Uses DefIcons file type identification and default icon system
- **Workbench API** - Uses `OpenWorkbenchObjectA()` for proper Workbench integration
- **Loop Prevention** - Prevents infinite loops if ProjectX is set as its own default tool
- **Toolbox Drawers** - Convert drawer icons to project-drawers that launch tools inside them
- **Modifier Key Support** - Hold Right Shift while double-clicking a toolbox drawer to open it as a normal drawer window
- **Simple Design** - Clean, focused implementation following Amiga conventions

## Usage

### As DefaultTool (Workbench Mode)

1. Set ProjectX as the default tool on any project icon (via Icon Information dialog)
2. Double-click the project icon
3. ProjectX will identify the file type and launch the appropriate default tool

**Example:**
```
1. Right-click on a project file icon
2. Select "Information..." from the menu
3. Set "Default tool:" to "ProjectX"
4. Double-click the icon - ProjectX will open it with the correct tool
```

### Command-Line Modes

#### Creating Toolbox Drawers

ProjectX can convert drawer icons to "toolbox drawers" - drawers that act like tools. When you double-click a toolbox drawer, it launches a tool inside the drawer instead of opening the drawer window.

**Usage:**
```bash
ProjectX TOOLBOX=<drawer path> TOOL=<tool name>
```

**Example:**
```bash
ProjectX TOOLBOX=Work:MyApp TOOL=MyAppTool
```

This will:
- Convert the drawer icon from `WBDRAWER` to `WBPROJECT` type
- Set ProjectX as the default tool
- Add a `TOOLBOX` tooltype with the tool name
- Preserve all existing tooltypes

**Requirements:**
- The drawer must exist and have a `WBDRAWER` icon type
- The tool must exist inside the drawer and have a `WBTOOL` icon type
- `TOOLBOX` and `TOOL` arguments are required together
- `TOOLBOX` and `DRAWER` modes are mutually exclusive

#### Opening Toolbox Drawers

When you double-click a toolbox drawer, it launches the tool specified in the `TOOLBOX` tooltype. To open it as a normal drawer window instead, hold the **Right Shift** key while double-clicking.

## How It Works

1. ProjectX receives a `WBStartup` message from Workbench with the file to open
2. Uses `GetIconTagList()` with `ICONGETA_IdentifyBuffer` to identify the file type
3. Constructs the default icon name (e.g., `def_ELF` for ELF files)
4. Retrieves the default icon using `ICONGETA_GetDefaultName`
5. Extracts the default tool from that icon
6. Uses `OpenWorkbenchObjectA()` to launch the tool with the file

## Building from Source

### Requirements
- SAS/C compiler 6.58
- NDK 3.2
- ToolKit

### Build Commands
```bash
# Using SMakefile
cd Source/
smake ProjectX

smake install ; Will copy ProjectX to the SDK/C drawer in the project directory

smake clean ; Will clean the local project folder of build artifacts
```

## Installation

1. Find the ProjectX executable in SDK/C/ in this distribution
2. Copy to your preferred location (e.g., `SYS:C/`)
3. Set ProjectX as the default tool on project icons you want to use it with

## Future Enhancements

- File requester to select tool when no default is found
- Option to save selected tool as default for future use
- Support for multiple files of the same type
- Tooltype configuration options

## ChangeLog

### Version 47.2 (23.12.2025)
- Added support for 'ToolBox' Drawers
- Added TOOLBOX/K and TOOL/K command-line arguments to convert a Drawer into a ToolBox
- Added Right Shift modifier key support for opening toolbox drawers as normal drawers

### Version 47.1 (21.12.2025)
- Initial release
- Basic file type identification and default tool lookup
- Loop prevention
- Workbench API integration

## About [amigazen project](http://www.amigazen.com)

*A web, suddenly*

*Forty years meditation*

*Minds awaken, free*

**amigazen project** uses modern software development tools and methods to update and rerelease classic Amiga open source software.

## Contact 

- At GitHub https://github.com/amigazen/ProjectX
- on the web at http://www.amigazen.com/toolkit/ (Amiga browser compatible)
- or email toolkit@amigazen.com

## Acknowledgements

*Amiga* is a trademark of **Amiga Inc**. 

ProjectX is named in homage to the classic Amiga game Project-X and the IconX tool for launching batch scripts from Workbench.

