# AFP in Chooser

ROMAW can mount an AFP-over-TCP volume using guest authentication. Install a local
copy of Chooser and AppleShare IP 3.7.4 with their resource forks intact. Select
**AppleShare IP** in Chooser, dismiss the legacy startup message if it appears,
and click **Server IP Address…**. Enter the server's IPv4 address or a hostname
that resolves to IPv4 and the volume's name, then click **Mount**.

The connection uses AFP 3.4 over TCP port 548. The browse window lists the live
File Manager catalog of the mounted volume, including nested folders. Use **Up**
to return to the parent and **Previous** and **Next** to page through a folder.
The mount is read-only and lives in the ROMAW process that opened it. Only guest
login is supported. Separate ROMAW processes have independent volume queues.

The original AppleShare IP package supplies the Chooser icon and controls. ROMAW
recognizes the 3.7.4 connection entry and handles that action through the native
AFP transport. The package still handles its other actions, including its
legacy AppleShare startup check. The installed Apple resources are local inputs.

Run `bash system/tests/check-runtime.sh afp-guest /absolute/path/to/ROMAW.exe`
to verify a 68K guest's AFPX mount record, volume lookup, root catalog, fork
read and unmount against the test server. The test requires the configured
STORAGE-PROXY fixture and runs on Windows through WSL interop.
