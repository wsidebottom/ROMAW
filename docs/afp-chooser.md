# AFP in Chooser

Open **Apple → Chooser** in ROMAW Finder or in a classic ROMAW app. Chooser lists
local servers that advertise AFP over TCP using DNS-SD. Select a server to see its
volumes, or enter a hostname or IPv4 address in **Server address** and click
**Connect**. You can append a port to use a port other than 548. Select a volume
and click **Mount**.

ROMAW uses AFP 3.4 guest login and mounts volumes read-only. Finder shows the
share as a mounted volume and browses its catalog through the File Manager.
Other ROMAW processes using the same profile open their own AFP sessions for the
current shared mount. Chooser remembers the last server. Eject the share to
remove the shared mount from the profile.

The C Chooser replaces the classic Chooser desk accessory and its AppleShare
extensions. It requires no Apple resource files. Reference disassembly of the
local Apple versions found the Chooser entry in DRVR 15 and AppleShare IP's
connection dispatcher in PACK -4096. These were used to identify the legacy
entry points. The built-in Chooser uses ROMAW's AFP client directly.

Run `bash system/tests/check-runtime.sh afp-guest /absolute/path/to/ROMAW.exe`
to verify File Manager mounting, catalog and fork operations from a 68K app.
After mounting Macintosh from STORAGE-PROXY in Chooser, run
`bash system/tests/check-runtime.sh afp-shared-guest /absolute/path/to/ROMAW.exe`
to check that a separately launched 68K app can find the shared volume, browse
its catalog and read a file fork. Both checks execute on Windows via WSL interop.
