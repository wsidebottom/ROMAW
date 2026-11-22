/*+===================================================================
  File:      text_resources.r

  Summary:   Classic Finder text resources. Non-ASCII escapes are MacRoman bytes.

  Classes:   None.

  Functions: No explicitly exported functions.

  Origin:    Existing repository source in executor/resources/finder; original attribution and provenance comments are
             preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

#include "resource_types.r"

resource 'MACS' (0, purgeable) {
    "@ROMAW_VERSION@"
};

resource 'STR ' (2260, purgeable, preload) {
    "Print Window\0xC9"
};

resource 'STR ' (2750, purgeable) {
    "Finder Shortcuts"
};

resource 'STR ' (3080, purgeable) {
    "^0"
};

resource 'STR ' (3091, purgeable) {
    "^0 Info"
};

resource 'STR ' (3260, purgeable) {
    "\0xC9"
};

resource 'STR ' (3504, purgeable) {
    "Numbers only, please!"
};

resource 'STR ' (3510, purgeable) {
    "This text has reached its maximum length.  No more characters can be added."
};

resource 'STR ' (4010) {
    "Finder Help"
};

resource 'STR ' (6010, purgeable) {
    "^1"
};

resource 'STR ' (8750, purgeable) {
    "Copy"
};

resource 'STR ' (9752, purgeable) {
    "edition"
};

resource 'STR ' (9760, purgeable) {
    "^0 edition"
};

resource 'STR ' (10000, purgeable) {
    "About This PC"
};

resource 'STR ' (10250, purgeable) {
    "Desktop"
};

resource 'STR ' (10500, purgeable) {
    "Clipboard"
};

resource 'STR ' (11000, purgeable) {
    "Find"
};

resource 'STR ' (11004, purgeable) {
    "The item size must be a number."
};

resource 'STR ' (11252, purgeable) {
    "folder"
};

resource 'STR ' (11502, purgeable) {
    "disk"
};

resource 'STR ' (12502, purgeable) {
    "script"
};

resource 'STR ' (12510, purgeable) {
    "This is a language script.  It allows your computer to use the linguistic rules of a foreign language.  It belongs in the System file."
};

resource 'STR ' (12752, purgeable) {
    "file"
};

resource 'STR ' (12753, purgeable) {
    "Verify"
};

resource 'STR ' (13002, purgeable) {
    "suitcase"
};

resource 'STR ' (13502, purgeable) {
    "resource"
};

resource 'STR ' (14002, purgeable) {
    "sound"
};

resource 'STR ' (14252, purgeable) {
    "desk accessory"
};

resource 'STR ' (14502, purgeable) {
    "font"
};

resource 'STR ' (14752, purgeable) {
    "keyboard layout"
};

resource 'STR ' (14760, purgeable) {
    "This is a keyboard layout.  With this item you can use the Keyboard control panel to choose a different layout of characters on your keyboard.  It must be in the System file to work."
};

resource 'STR ' (15502, purgeable) {
    "font suitcase"
};

resource 'STR ' (15752, purgeable) {
    "desk accessory suitcase"
};

resource 'STR ' (17002, purgeable) {
    "document"
};

resource 'STR ' (17003, purgeable) {
    "Sign"
};

resource 'STR ' (17252, purgeable) {
    "application program"
};

resource 'STR ' (17254, purgeable) {
    "The memory size must be a number."
};

resource 'STR ' (19504, purgeable) {
    "The font size for views must be a number."
};

resource 'STR ' (19550, purgeable) {
    "Geneva"
};

resource 'STR ' (20502, purgeable) {
    "alias"
};

resource 'STR ' (27260, purgeable) {
    "Print Desktop\0xC9"
};

resource 'STR#' (150, purgeable) {
    {
        "zero K";
        "^0 MB";
        "^0, ^1";
        "\0xD1";
        "^#%";
        "^0K";
        "*";
        "?";
        "^0 GB";
        "^0 TB";
    }
};

resource 'STR#' (1250, purgeable) {
    {
        "The System file is the wrong version.  Please use the System file that came with this version of ROMAW.";
        "The command could not be completed, ^2.";
        "window";
        "because *0x//*an error of type ^0 occurred";
        "An unexpected error occurred, ^1.";
        "The application \0xD2^1\0xD3 could not be opened because there is not enough system memory available for \0xD2^2\0xD3.\0x0D\0x0DTry increasing -syszone and/or -applzone.";
        "The application \0xD2^1\0xD3 could not be opened because \0xD2^2\0xD3 could not be found.";
        "The application \0xD2^1\0xD3 can not be opened on this computer.";
        "The application \0xD2^1\0xD3 can not be opened because an unexpected error occured.";
        "Unknown";
    }
};

resource 'STR#' (3000, purgeable) {
    {
        "Clean Up Window";
        "Close Window";
        "New Folder";
        "Open";
        "^0 item%0%%s%";
        "Clean Up Selection";
        "Clean Up by ^0";
        "Close All";
        "Close Outline";
        "Open";
        "by ^0";
        "Small Icon";
        "Icon";
        "Name";
        "Date";
        "Size";
        "Kind";
        "Comments";
        "Label";
        "Version";
        "Name";
        "Date";
        "Size";
        "Kind";
        "Comments";
        "Label";
        "Version";
    }
};

resource 'STR#' (5000, purgeable) {
    {
        "because an item with *0x/that/* name already exists";
        "because *0x//*the disk is full";
        "because *0x//*a disk error occurred";
        "because *0x/it/* is in use";
        "because *0x//*you do not have enough access privileges";
        "because *0x//*a child or parent folder is already shared";
        "because *0x//*the server is no longer connected";
        "because *0x//*the folder cannot be found";
        "because *0x/it/* cannot be found";
        "because *0x//*the disk\0xD5s directory is full";
        "because *0x//*the disk is locked";
        "because *0x/it/* cannot be moved into *0x/itself/*";
        "because *0x/it/* contains items that are in use";
        "because *0x//*there isn\0xD5t enough memory available.  Try increasing -syszone and/or -applzone.";
        "because *0x//*too many files are open";
        "because *0x//*there isn\0xD5t enough memory available";
        "because *0x//*memory is fragmented";
        "because *0x//*too many processes are running";
        "because *0x//*the application doesn\0xD5t have enough memory";
        "because *0x//*you cannot erase a shared disk";
        "because *0x//*you do not have the privilege to make changes";
        "because *0x/it/* is in use";
        "because *0x/it/* is locked";
        "because *0x/it/* contains items that are in use";
        "because *0x/it/* contains locked items";
        "because *0x//*a folder cannot be replaced by an item it contains";
        "because *0x//*a file cannot be replaced by a folder";
        "because *0x//*a folder cannot be replaced by a file";
        "because *0x//*the original item could not be found";
        "because *0x/it/* cannot be copied into *0x/itself/*";
        "because *0x/it/* is in the Trash.  To use *0x/this item/*, first drag *0x/it/* out of the Trash";
        "because *0x//*no printer has been selected.  Please use the Chooser to select a printer";
        "The ^3 could not be opened, ^2.";
        "*0x/The/* ^0 \0xD2^1\0xD3 could not *0x/be/* ^3, ^2.";
        "^3 for *0x/the/* ^0 \0xD2^1\0xD3 could not be opened, ^2.";
        "^3 ^2.";
        "Page Setup window";
        "*0x/printed/*";
        "*0x/opened/*";
        "*0x/printed/*";
        "*0x/put away/*";
        "*0x/erased/*";
        "*0x/deleted/*";
        "The info window";
        "The sharing window";
        "You cannot create a folder in *0x/the/* ^0 \0xD2^1\0xD3,";
        "You cannot move %4%\0xD2^4\0xD3%the selected items% to *0x/the/* ^0 \0xD2^1\0xD3,";
        "You cannot copy %4%\0xD2^4\0xD3%the selected items% onto *0x/the/* ^0 \0xD2^1\0xD3,";
        "You cannot duplicate in *0x/the/* ^0 \0xD2^1\0xD3,";
        "You cannot move %4%\0xD2^4\0xD3%the selected items% from *0x/the/* ^0 \0xD2^1\0xD3,";
        "You cannot replace *0x/the/* ^0 \0xD2^1\0xD3,";
        "Sorry, the desktop folder on the startup disk could not be created.  Try unlocking the disk.";
        "The shutdown could not be completed, because the application \0xD2^1\0xD3 could not quit.";
        "Some documents could not be opened.  Try opening the documents from within the application.";
        "Some documents could not be printed.  Try printing the documents from within the application.";
        "About This PC\0xC9";
        "About ROMAW Finder\0xC9";
        "@ROMAW_VERSION@";
        "Shut Down";
        "Shut Down ";
        "because *0x/it/* is not in any drive";
        "because *0x//*there are too many nested folders";
        "You cannot make an alias for *0x/the/* ^0 \0xD2^1\0xD3,";
        "because System Folder Protection is on.  To move *0x/this item/*, first turn off System Folder Protection in the General Controls panel";
        "because Application Folder Protection is on.  To move *0x/this item/*, first turn off Application Folder Protection in the General Controls panel";
    }
};

resource 'STR#' (8750, purgeable) {
    {
        "Copying:";
        "Copying:";
        "Moving:";
        "Copying:";
        "Verifying:";
        "*0x/read/*";
        "*0x/copied/*";
        "*0x/moved/*";
        "*0x/written/*";
        "*0x/verified/*";
        "Preparing to copy\0xC9";
        "Preparing to move\0xC9";
        "Items remaining to be copied:";
        "Items remaining to be moved:";
        "Finishing copy\0xC9";
        "Finishing move\0xC9";
        "*0x/The/* ^2 couldn\0xD5t *0x/be/* ^3, ^1.  Do you want to continue copying?";
        "You don\0xD5t have enough access privileges to see all the items in \0xD2^1\0xD3.  Copy the visible ones?";
        "You don\0xD5t have enough access privileges to see all the items if they are put into \0xD2^1\0xD3.  Put them there anyway?";
        "^0 \0xD2^1\0xD3";
        "There is not enough room on the disk \0xD2^3\0xD3 to copy %1%\0xD2^4\0xD3%the selected items% (an additional ^2 is needed).";
        "There is not enough room on the disk \0xD2^3\0xD3 to copy %1%\0xD2^4\0xD3%the selected items% (an additional ^2 must be thrown away first).";
        "Move";
        "There is not enough room on the disk \0xD2^2\0xD3 to copy %1%\0xD2^3\0xD3%the selected items%, unless you empty the Trash.  Do you want to empty the Trash now?";
        "%1%\0xD2^1\0xD3%Some selected items% cannot be copied, because of copy protection.%1%%  Do you want to continue copying?%";
        "Canceling, please wait...";
    }
};

resource 'STR#' (9000, purgeable) {
    {
        "Some items in this location have the same names as items you\0xD5re moving.  Do you want to replace them with the ones you\0xD5re moving?";
        "An item named \0xD2^1\0xD3 already exists in this location.  Do you want to replace it with the one you\0xD5re moving?";
        "An older item named \0xD2^1\0xD3 already exists in this location.  Do you want to replace it with the one you\0xD5re moving?";
        "A newer item named \0xD2^1\0xD3 already exists in this location.  Are you sure you want to replace the newer item with the one you\0xD5re moving?";
    }
};

resource 'STR#' (10000, purgeable) {
    {
        "The application \0xD2^2\0xD3 has unexpectedly quit, ^1.";
        "^0 used as RAM on ^1";
        "System Software";
        "System Enabler ";
    }
};

resource 'STR#' (10250, purgeable) {
    {
        "Clean Up Desktop";
        "Clean Up All";
        "The Trash cannot be moved off the desktop.";
        "The startup disk cannot be removed from the desktop, because it contains the active system software.";
        "PrintMonitor";
        "There is not enough memory to print now.  ^1 will attempt to print again when more memory is available.  Closing windows or quitting application programs can make more memory available.";
        "Nothing can be printed now, because ^1 could not be found.  To print, put ^1 into the Extensions folder in the System Folder.";
        "shared";
        "locked";
        "400K";
        "Items from ^1 disks cannot be moved to the desktop.  Do you want to copy %2%\0xD2^3\0xD3%the selected items% to the startup disk?  (%2%It%They% will appear on the desktop.)";
        "The disk \0xD2^2\0xD3 cannot be used, ^1.";
        "The ^1 \0xD2^2\0xD3 could not be opened, because the application program that created it could not be found.  Do you want to open it using \0xD2^3\0xD3?";
    }
};

resource 'STR#' (10500, purgeable) {
    {
        "none";
        "unknown";
        "There is not enough memory to show the Clipboard.";
        "picture";
        "text";
        "styled text";
        "sound";
    }
};

resource 'STR#' (11000, purgeable) {
    {
        "\0xD2^1\0xD3 was found on the desktop.";
        "Searching on \0xD2^0\0xD3\0xC9";
        "Searching inside \0xD2^0\0xD3\0xC9";
        "Items found on \0xD2^0\0xD3:";
        "Items found inside \0xD2^0\0xD3:";
        "Items left to display:";
        "It is not possible to show all of the matching items in one window.  Select \0xD2Find Again\0xD3 to see more matching items ^1.";
        "on \0xD2^0\0xD3";
        "inside \0xD2^0\0xD3";
        "on the desktop";
        "name";
        "size";
        "kind";
        "version";
        "comments";
        "label";
        "lock";
        "creation";
        "modification";
        "No matching items were found.";
        "on \0xD2^0\0xD3";
        "inside \0xD2^0\0xD3";
    }
};

resource 'STR#' (11250, purgeable) {
    {
        "New Folder with Selection";
        "^0, for ^1";
        "untitled folder";
        " copy";
        "Icon\0x0D";
        "See Folders, See Files, Make Changes";
        "See Files, Make Changes";
        "See Folders, Make Changes";
        "Make Changes";
        "See Folders, See Files";
        "See Files";
        "See Folders";
        "No Privileges";
        "<Guest>";
        "<Any User>";
        "<None>";
        "The shared folder \0xD2^1\0xD3 is in use by users on the network.  Are you sure you want to deny these users access to this folder?";
        "Save changes to access privileges for \0xD2^1\0xD3?";
        "Are you sure you want to change the owner for \0xD2^1\0xD3?";
        "Are you sure you want to change all folders inside this one to show these privileges?";
        "The changes could not be saved (^1).";
        "The changes could not be saved (access privileges error).";
        "The changes could not be saved (unknown user).";
        "The changes could not be saved (unknown group).";
        "The access privileges could not be shown (^1).";
        "Folders cannot be copied onto 400K disks.  To copy the contents of a folder, open the folder and drag the items inside it to the disk.";
        "The changes to some folders couldn\0xD5t be saved, because you don\0xD5t own one or more of the enclosed folders.";
        "";
        "The changes to some folders couldn\0xD5t be saved, because the volume was disconnected.";
        "The changes to some folders couldn\0xD5t be saved (unknown error).";
        "\0xD2^1\0xD3 could not be shared, because it is inside a shared folder or disk.";
        "\0xD2^1\0xD3 could not be shared, because there is already a shared folder with that name.";
        "\0xD2^1\0xD3 could not be shared, because the maximum number of folders are already shared.";
        "folder";
        "disk";
        "There is not enough memory to keep the window \0xD2^1\0xD3 open.";
        "because *0x/it/* cannot be replaced by an alias with the same name";
        "\0xD2^1\0xD3 could not be shared, because there is a shared folder inside it.";
        "Folders can not be shared until file sharing is turned on using the Sharing Setup control panel.  Do you want the control panel opened now?";
        "Applications cannot be shared until program linking is turned on using the Sharing Setup control panel.  Do you want the control panel opened now?";
        "Applications and Folders cannot be shared until file sharing and program linking are turned on using the Sharing Setup control panel.  Do you want the control panel opened now?";
        "That name is too long.  Folder names can have up to 31 characters.";
        "That name is too long.  Owner names can have up to 31 characters.";
        "That name is too long.  User/Group names can have up to 31 characters.";
        "Items on floppy disks cannot be shared.";
        "You cannot share applications on other computers.";
        "One or more items could not be shared, because not all volumes are available for file sharing.";
        "File sharing is currently starting up.  Wait until the Sharing Setup control panel indicates it is on, and then try again.";
        "because *0x/it/* is shared";
        "The selected items cannot all be put into the same location, because more than one of them is named \0xD2^1\0xD3.  You can drag these items one at a time.";
        "The sharing window cannot be opened for folders on local disks with the currently installed version of file server software.";
        "The sharing window cannot be opened for folders on local disks, because the File Sharing Extension is not currently installed.";
        "\0xD2^1\0xD3 cannot be moved or copied here, because the name is reserved by the system software.";
        "because an invisible item with the same name *0x//*already exists in this location";
        "%2%The ^1 \0xD2^0\0xD3%The fonts% will not be available to currently running applications until they have quit.";
        "This ROMAW instance cannot use any more fonts at this time; only 128 fonts can be used at once.";
        "The items were successfully stored in their respective places inside the System Folder.";
        "The file \0xD2^1\0xD3 cannot be removed or changed, because the script system requires it.";
    }
};

resource 'STR#' (11330, purgeable) {
    {
        "font%1%%s%";
        "sound%1%%s%";
        "script system%1%%s%";
        "keyboard layout%1%%s%";
        "desk accessor%1%y%ies%";
        "control panel%1%%s%";
        "extension%1%%s%";
        "item%1%%s%";
        " folder";
        " file";
        "#1#These#Some of these# items need to be stored in special places inside the System Folder in order to be available to ROMAW.  Put them where they belong?";
        "These items need to be stored in the System file in order to be available to this ROMAW instance.  Put them in the System file?";
        "^1 ^2 %1%was *0x/put/*%were *0x/put/*% into the ^3^4.\0x0D";
        "The selected items cannot be moved together, because more than one of the selected ^1 are named \0xD2^2\0xD3.";
        "";
    }
};

resource 'STR#' (11332, purgeable) {
    {
        "Fonts need to be stored in the ^2^3 in order to be available to this ROMAW instance.  Put %1%\0xD2^4\0xD3%these fonts% into the ^2^3?";
        "Sounds need to be stored in the ^2^3 in order to be available to this ROMAW instance.  Put %1%\0xD2^4\0xD3%these sounds% into the ^2^3?";
        "Script systems need to be stored in the ^2^3 in order to be available to this ROMAW instance.  Put %1%\0xD2^4\0xD3%these script systems% into the ^2^3?";
        "Keyboard layouts need to be stored in the ^2^3 in order to be available to this ROMAW instance.  Put %1%\0xD2^4\0xD3%these keyboard layouts% into the ^2^3?";
        "Desk accessories need to be stored in the ^2^3 in order to appear in the Apple menu.  Put %1%\0xD2^4\0xD3%these desk accessories% into the ^2^3?";
        "Control panels need to be stored in the ^2^3 or they may not work properly.  Put %1%\0xD2^4\0xD3%these control panels% into the ^2^3?";
        "Extensions need to be stored in the ^2^3 in order to be available to this ROMAW instance.  Put %1%\0xD2^4\0xD3%these extensions% into the ^2^3?";
    }
};

resource 'STR#' (11500, purgeable) {
    {
        "Completely erase disk named\0x0D\0xD2^0\0xD3 (^1)?";
        "Are you sure you want to rebuild the desktop file on the disk \0xD2^1\0xD3?  Comments in info windows will be lost.";
        "Completely erase the disk named \0xD2^1\0xD3 that you just inserted?";
        "MultiFinder";
        "Desktop";
        "Are you sure you want to completely replace contents of\0x0D  \0xD2^0\0xD3 (^1)\0x0D\0x0Dwith contents of\0x0D  \0xD2^2\0xD3 (^3)?";
        "because *0x/it/* is the startup disk, which contains the active system software";
        "because *0x/it/* is the startup disk, which contains the active system software";
        "Updating disk for new system software\0xC9";
        "Desktop DB";
        "Desktop DF";
        "internal drive";
        "external drive";
        "The desktop file could not be completely rebuilt because some folders are nested too deeply.";
        "shared disk";
        "Rescued items from ";
        "The shared disk \0xD2^1\0xD3 could not be put away, because some of its items have been moved to the Trash.  Do you want to delete those items and then put away the disk?";
        "Rebuilding the desktop file\0xC9";
        "Some items could not be deleted.  Do you want to put away those items and then put away the disk?";
        "The disk \0xD2^2\0xD3 is ^1 too small to hold all of the contents of the selected disk.";
        "The contents of the disk \0xD2^1\0xD3 cannot be replaced, because the disk is locked.";
        "That name is too long.  Disk names can have up to 27 characters.";
        "not in any drive";
        "unknown drive";
        "external hard disk";
        "because *0x/it/* is a 400K disk";
        "Are you sure you want to remove the disk \0xD2^1\0xD3 from the desktop?  It will reappear on the desktop when ROMAW is restarted.";
        "because *0x/it/* is being shared";
        "Preserving comments\0xC9";
        "Are you sure you want to rebuild the desktop file on the disk \0xD2^1\0xD3?";
        "An error occured while rebuilding the desktop file on the disk \0xD2^1\0xD3; some comments from info windows were not preserved.";
        "An error occured while rebuilding the desktop file on the disk \0xD2^1\0xD3; comments from info windows were not preserved.";
    }
};

resource 'STR#' (11750, purgeable) {
    {
        "Trash";
        "The Trash is empty.";
        "^1 file%1%%s%#6# and#,# ^2 folder%2%%s%#6##, and ^6 other item%6%%s%# are in the Trash#1#.# for a total of ^3.\0x0D#^4^5";
        "#1##^1 application%1% is%s are% in the Trash.\0x0D#";
        "#1##^1 System File%1% is%s are% in the Trash.\0x0D#";
        "The Trash contains ^1 item%1%.  It uses%s, which use% ^2 of disk space.  Are you sure you want to permanently remove %1%it%these items%?";
        "This command cannot be used on items in the Trash.";
        "Preparing to empty the Trash\0xC9";
        "Items remaining to be deleted:";
        "Empty Trash";
        "Empty Trash\0xC9";
        "The Trash cannot be emptied, because all of the items in it (other than folders) are locked.  To delete locked items, hold down the Option key while you choose Empty Trash.";
        "%1%One item%Some items% could not be deleted because %1%it is%they are% locked.  Do you want to delete the other items?";
        "Items from 400K disks cannot be left in the Trash.  Do you want to delete %1%\0xD2^2\0xD3%the selected items% immediately?";
        "The item \0xD2^2\0xD3 could not be deleted, ^1.  Do you want to continue?";
        "%1%\0xD2^2\0xD3%The selected items% cannot be left in the Trash.  Do you want to delete %1%it%them% immediately?";
        "The folder \0xD2^1\0xD3 cannot be put in the Trash, because it is shared.";
        "The selection cannot be put in the Trash, because a folder within is shared.";
        "%1%\0xD2^2\0xD3%Some of the selected items% cannot be put in the Trash, because %1%it has the same name as a locked or unrenameable item%they have the same names as locked or unrenameable items% in the Trash.  Do you want to delete %1%it%them% immediately?";
        "The System Folder cannot be put in the Trash, because it contains the active system software.";
    }
};

resource 'STR#' (12000, purgeable) {
    {
        "because *0x/it/* is too #2#*0x/new/*#*0x/old/*#";
        "The system extension \0xD2^2\0xD3 cannot be used, ^1.";
    }
};

resource 'STR#' (12250, purgeable) {
    {
        "The control panel \0xD2^1\0xD3 cannot be used with ROMAW.";
        "The control panel \0xD2^1\0xD3 cannot be used now.  There may not be enough memory available, or the control panel may be damaged.";
        "The control panel \0xD2^1\0xD3 cannot be used now, because not enough memory is available.";
        "\0xD2^1\0xD3 is a Monitors extension.  Open the Monitors control panel to use it.";
    }
};

resource 'STR#' (12750, purgeable) {
    {
        "Chooser extension";
        "system extension";
        "system extension";
        "database extension";
        "communications tool";
        "control panel";
        "PostScript\0xAA font";
        "printing extension";
        "paper type";
        "PowerTalk extension";
        "PowerTalk template";
        "Chooser extension\0x0D\0x0DThis file allows you to install a new item in your Chooser (a network printer, for example).  To add this functionality to your Chooser, place this file in the Extensions folder.";
        "System extension\0x0D\0x0DThis file adds functionality to ROMAW.  To add this file\0xD5s functionality to ROMAW, place the file in the Extensions folder and then restart ROMAW.";
        "System extension\0x0D\0x0DThis file adds functionality to ROMAW.  To add this file\0xD5s functionality to ROMAW, place the file in the Extensions folder and then restart ROMAW.";
        "Database extension\0x0D\0x0DThis file allows ROMAW to communicate with a particular database program.  To add this functionality to ROMAW, place this file in the Extensions folder and then restart ROMAW.";
        "Communications tool\0x0D\0x0DThis file adds communications functionality to ROMAW.  To add this file\0xD5s functionality to ROMAW, place this file in the Extensions folder.";
        "Paper type\0x0D\0x0DThis file is a paper type for QuickDraw\0xAA GX.  To add this file\0xD5s functionality to ROMAW, place the file in the Extensions folder and then restart ROMAW.";
        "PowerTalk extension\0x0D\0x0DThis file adds functionality to PowerTalk.  To add this file\0xD5s functionality to ROMAW, place the file in the Extensions folder and then restart ROMAW.";
        "PowerTalk template\0x0D\0x0DThis file adds functionality to PowerTalk.  To add this file\0xD5s functionality to ROMAW, place the file in the Extensions folder and then restart ROMAW.";
        "Note Pad";
        "Scrapbook";
        "Note Pad File";
        "Scrapbook File";
        "The Clipboard file must be in the System Folder to be opened.";
        "This file is used by the desk accessory \0xD2^1\0xD3.  To use it, first put it into the System Folder.";
        "This file is used by the desk accessory \0xD2^1\0xD3.  To be opened, this file must be named \0xD2^2\0xD3 and located in the System Folder.";
        "To open this file, the desk accessory \0xD2^1\0xD3 must be in the Apple Menu Items folder.";
        "This file is used by the system software.  It cannot be opened.";
        "That name is too long.  File names can have up to 31 characters.";
        "extension";
        "document";
        "suitcase";
        "The signature on \0xD2^1\0xD3 could not be verified. Either the file has been modified since it was signed or the signature itself has been altered.";
        "The signature on \0xD2^1\0xD3 could not be checked.";
        "Do you wish to remove or verify the signature on \0xD2^1\0xD3?";
        "The signature on \0xD2^1\0xD3 could not be removed because the file is in use, locked, or otherwise unavailable.";
    }
};

resource 'STR#' (13000, purgeable) {
    {
        "#0#The selected items#%0%\0xD2^1\0xD3%Some of the selected items%# cannot be put into the System file.  Only sounds and keyboard layouts can be put in the System file.";
        "because *0x/it/* is damaged";
        "Items from within \0xD2^1\0xD3 cannot be moved into the drop folder \0xD2^2\0xD3.";
        "Some of the selected items are desk accessories, which cannot be put in the System file.  If you want them to appear in the Apple menu, drag them to the Apple Menu Items folder.";
        "#0#The selected items#%0%\0xD2^1\0xD3%Some of the selected items%# cannot be duplicated, because items of this kind cannot be renamed.  To copy %0%it%them%, hold down the Option key and drag %0%it%them% to another location.";
        "You cannot change items used by the system while programs other than the Finder are open.  To make changes to this folder or suitcase, first quit all open application programs and desk accessories.";
        "The suitcase \0xD2^1\0xD3 is locked, so its contents cannot be removed.  Would you like to copy them instead?";
        "because *0x//*suitcase files can only hold 64 desk accessories";
        "Mover ran out of memory.";
        "The keyboard layout \0xD2^1\0xD3 is the active keyboard layout and cannot be removed from the System file.";
        "#0#The selected items#%0%\0xD2^1\0xD3%Some of the selected items%# cannot be put into the System file.  Only sounds and keyboard layouts can be put in the System file.  Fonts can be put in the Fonts folder.";
        "The script \0xD2^1\0xD3 is in use by the System file, and cannot be removed.  You can disable secondary scripts by restarting and holding down the option and space keys.";
        "Keyboard layouts cannot be moved at the same time as the script that owns them.  Please drag the items separately.";
    }
};

resource 'STR#' (14250, purgeable) {
    {
        "Desk accessories in suitcase files cannot be opened.  To open a desk accessory, drag it out of the suitcase file first.";
        "That name is too long.  Desk accessory names can have up to 30 characters.";
    }
};

resource 'STR#' (14514, purgeable) {
    {
        " colors";
        ", 4";
        ", 16";
        ", 256";
        "";
        "";
        "bold";
        "italic";
        "underline";
        "outline";
        "shadow";
        "condense";
        "extend";
        " (";
        ")";
        ", ";
        " ";
    }
};

resource 'STR#' (14516, purgeable) {
    {
        "How razorback-jumping frogs can level six piqued gymnasts!";
    }
};

resource 'STR#' (15500, purgeable) {
    {
        "#0#The selected items#%0%\0xD2^1\0xD3%Some of the selected items%# cannot be put into \0xD2^2\0xD3.  Only fixed-size fonts and TrueType fonts can be put in a font suitcase.";
    }
};

resource 'STR#' (15750, purgeable) {
    {
        "#0#The selected items#%0%\0xD2^1\0xD3%Some of the selected items%# cannot be put into \0xD2^2\0xD3.  Only desk accessories can be put in a desk accessory suitcase.";
    }
};

resource 'STR#' (17000, purgeable) {
    {
        "^0 document";
        "^0 stationery pad";
        "stationery pad";
        "unknown";
        "";
        "The document \0xD2^1\0xD3 is locked, so you will not be able to save any changes.  Do you want to open it anyway?";
        "because *0x//*the application \0xD2^0\0xD3 could not be found";
        "because the application program that created *0x/it/* could not be found";
        "Save new document as:";
        "Some of the selected documents are locked, so you will not be able to save any changes to them.  Do you want to open them anyway?";
        "Signing could not be completed.  There may be a problem with the signer \0xD2^1\0xD3.";
        "\0xD2^1\0xD3 could not be signed because it is not a file.  Do you wish to continue signing any remaining files?";
        "\0xD2^1\0xD3 has already been signed.  Do you want to replace that signature?";
        "\0xD2^1\0xD3 could not be signed because it is in use or otherwise unavailable.  Do you wish to continue signing any remaining files?";
        "The signer \0xD2^1\0xD3 is invalid. It has expired or is not yet active.";
        "signer";
        "\0xD2^1\0xD3 could not be signed because the disk is full.  Any remaining files will also not be signed.";
    }
};

resource 'STR#' (17250, purgeable) {
    {
        "Opening the application \0xD2^1\0xD3\0xC9";
        "***";
        "***";
        "You have set the Minimum size below ^2, which may cause \0xD2^1\0xD3 to crash.\0x0D\0x0DAre you sure you want to continue?";
        "***";
        "***";
        "\0xD2^1\0xD3 could not be opened, because its current memory size of ^3 is too low.  To open \0xD2^1,\0xD3 first select Get Info and raise the Minimum and Preferred memory sizes to at least ^2.";
        "This version of Font/DA Mover is out of date.  To install a font, drag it to the System file.  To put a desk accessory in the Apple menu, drag it to the Apple Menu Items folder.";
        "This version of MPW is not compatible with the current version of system software.  Please upgrade to the latest MPW.";
        "The memory size must be less than 1,000,000K.";
        "You have set the Minimum and Preferred sizes below ^2, which may cause \0xD2^1\0xD3 to crash.\0x0D\0x0DAre you sure you want to continue?";
        "The Minimum size can\0xD5t be larger than the Preferred size.\0x0D\0x0DDo you want to set the Minimum size to the Suggested size of ^1?";
        "The Minimum size can\0xD5t be larger than the Preferred size.\0x0D\0x0DDo you want to set the Preferred size to the Suggested size of ^1?";
        "There is not enough memory to open \0xD2^1\0xD3 (^2 needed, ^3 available).^4";
        "\0x0D\0x0DTo make more memory available, try quitting \0xD2^1\0xD3.";
        "\0x0D\0x0DTo make more memory available, try quitting the ^1 applications that are currently open.";
        "\0x0D\0x0DClosing windows or quitting desk accessories can make more memory available.";
        "Memory requirements will decrease by ^1 if virtual memory is turned on in the Memory control panel.";
        "Memory requirements will increase by ^1 if virtual memory is turned off in the Memory control panel.";
        "The Minimum size can\0xD5t be smaller than ^1.\0x0D\0x0DDo you want to set the Minimum size to the Suggested size of ^2?";
        "The Minimum and Preferred size can\0xD5t be smaller than ^1.\0x0D\0x0DDo you want to set the Minimum and Preferred size to the Suggested size of ^2?";
    }
};

resource 'STR#' (19500, purgeable) {
    {
        "The font size for views must be between ^0 and ^1.";
    }
};

resource 'STR#' (20500, purgeable) {
    {
        " alias";
        "because *0x//*you have reached the limit of the number of file servers you can log on to at one time";
        "because *0x//*the file server \0xD2^1\0xD3, which contains the original item, will not allow any additional users to log on. Try again later";
        "because the shared disk \0xD2^1\0xD3#2#, which contains *0x//*the original item,## could not be found on the network";
        "because the disk \0xD2^1\0xD3#2#, which contains *0x//*the original item,## could not be found";
        "The alias \0xD2^1\0xD3 could not be used, ^2.";
        "because *0x//*this item is really not an alias (oops!).  The problem has now been corrected.  Please try again";
        " : ";
        "The command could not be completed, ^2.";
        "because *0x//*this item is really not an alias (oops!).  Make a copy of this item and try again using the copy";
        "because your password must be changed.  Please set a new password";
        "because your password has expired.  Please set a new password";
    }
};

resource 'STR#' (20750, purgeable) {
    {
        "^0 available";
        "^0 in disk";
        "^0 bytes used";
        "^0 on disk";
        "^0 items";
        "^0\0x0D\0x0D^1";
        "n/a";
        "^0, ^1";
        "^1 (^0)";
        "You do not have enough privileges to make changes here.  Do you want to create the alias%1%%es% on the desktop?";
        "The name \0xD2^1\0xD3 is already taken.  Please use a different name.";
        "because *0x/it/* was created on the desktop";
        "The disk is locked, so you cannot make changes here.  Do you want to create the alias%1%%es% on the desktop?";
        "The name \0xD2^1\0xD3 is reserved by the system software.  Please use a different name.";
    }
};

resource 'STR#' (27500, purgeable) {
    {
        "Finder Preferences";
        "Color for \0xD2^0\0xD3";
    }
};

resource 'vers' (1) {
    0x07, 0x55, final, 0x00, 0,
    "7.5.5",
    "7.5.5, \0xA9 Apple Computer, Inc. 1983-96"
};

resource 'vers' (2) {
    0x07, 0x53, final, 0x00, 0,
    "7.5.3",
    "System 7.5 Version 7.5.3"
};

/* Original Finder display credit: "Executor Finder \0xA9 2026". */
