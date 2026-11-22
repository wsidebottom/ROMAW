# Document associations

These original Retro68 fixtures test inferred and explicit Finder metadata,
legacy empty sidecars, new resource-fork metadata, application document icons,
guest Toolbox launches and direct command-line document launches. The target validates
the document parameter list and reads the document using its working directory.
Failures and success use explicit GDB markers; ordinary process exit is not
accepted as success. User applications and preferences are untouched.

Build in WSL (adjust the Retro68 toolchain path if necessary):

```sh
cmake -S system/tests/document-associations -B build/document-association-probe \
  -DCMAKE_TOOLCHAIN_FILE="$HOME/retro68-toolchain/m68k-apple-macos/cmake/retro68.toolchain.cmake"
cmake --build build/document-association-probe -j10
python3 system/tests/document-associations/fixtures.py
bash system/tests/check-files.sh filetype
```

After building/staging ROMAW, run in PowerShell:

```powershell
./system/tests/document-associations/run-tests.ps1
```

The probe uses Macintosh Resource Manager and Process Manager calls directly.
It checks document parameters and application icon resources.
