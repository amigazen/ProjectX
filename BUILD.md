## Building from Source

### Requirements
- SAS/C compiler 6.58
- NDK 3.2
- ToolKit SDK

### Build Commands
```bash
; Using SMakefile
cd Source/
smake

smake install ; Will copy ProjectX to the SDK/C drawer in the project directory

smake clean ; Will clean the local project folder of build artifacts
```

## Installation

1. Find the ProjectX executable in SDK/C/ in this distribution
2. Place in `SYS:C/` or your preferred location
3. Set 'ProjectX' as the default tool on project icons you want to use it with

