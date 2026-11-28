# AFP in Chooser

Open **Apple → Chooser** in ROMAW Finder or in a classic ROMAW app. Chooser lists
local servers that advertise AFP over TCP using DNS-SD. Select a server to see its
volumes, or enter a hostname or IPv4 address in **Server address** and click
**Connect**. You can append a port to use a port other than 548. Select a volume
and click **Mount**. Sign in with your AFP account or choose **Continue as Guest**.
Account mounts stay in the current ROMAW process. The shared mount profile
contains guest mounts only.

ROMAW negotiates AFP 3.1 through 3.4 and mounts volumes read-only. Account
login selects the strongest supported method advertised by the server in this
order: SRP, DHX2, DHCAST128, 2-Way Randnum, and Randnum. The Windows build
uses OpenSSL 3 for these password exchanges. If the staging script cannot find
that DLL in its usual prefixes, set OPENSSL_WINDOWS_PREFIX to the Windows
cross-build prefix that contains it.
Kerberos ticket login and Recon1 session resumption are currently unsupported. Finder shows the
share as a mounted volume and browses its catalog through the File Manager.
Other ROMAW processes using the same profile open their own AFP sessions for the
current shared mount. Chooser remembers the last server. Eject the share to
remove the shared mount from the profile.

In classic ROMAW apps, Chooser runs as a Macintosh Dialog Manager window with
scrollable List Manager panes for servers and volumes. Chooser stays open
while you use other windows. Close it with its close box or **Close**.
Finder opens Chooser as a classic child window within its desktop. A small
icon beside a volume identifies an existing mount. Select it and click
**Open** to browse the volume in Finder.
The built-in Chooser replaces the legacy desk accessory and AppleShare extensions. It requires no Apple resource files. Reference disassembly of the
local Apple versions found the Chooser entry in DRVR 15 and AppleShare IP's
connection dispatcher in PACK -4096. These were used to identify the legacy
entry points. The built-in Chooser uses ROMAW's AFP client directly.

Run `bash system/tests/check-runtime.sh afp-guest /absolute/path/to/ROMAW.exe`
to verify File Manager mounting, catalog and fork operations from a 68K app.
After mounting Macintosh from STORAGE-PROXY in Chooser, run
`bash system/tests/check-runtime.sh afp-shared-guest /absolute/path/to/ROMAW.exe`
to check that a separately launched 68K app can find the shared volume, browse
its catalog and read a file fork. Both checks execute on Windows via WSL interop.
