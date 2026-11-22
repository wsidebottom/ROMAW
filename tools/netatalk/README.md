# Netatalk absent forks

`empty-forks.patch` records the change deployed to `root@10.0.0.114` on
2026-11-02. It applies to the local Netatalk 4.4.2 tree **after** its existing
OS X sidecar/ATTR fixes, rather than to an unmodified upstream release.
Source: `/usr/local/src/netatalk-4.4.2`; installed prefix: `/opt/netatalk4`.

The patch:

- Lists valid resource-only AppleDouble files without creating their data files,
  for the selected OS X or Netatalk volume backend.
- Validates bounded AppleDouble descriptors before accepting orphan entries.
- Creates a data anchor when AFP access requires its stable inode for share-mode
  locks and CNIDs. Resource-only rename/delete/copy can require this anchor too.
- Avoids creating new metadata during ordinary data-fork opens.
- Avoids all-zero native two-entry sidecars on new OS X-backend files. Legacy
  Netatalk metadata can contain names, dates and CNIDs, so it remains required.
- Preserves the existing FinderInfo-first/resource-last serializer and ATTR data.

Writable resource-fork opens may still require a sidecar for cross-process
locking. This patch does not implement a separate persistent lock/CNID store,
remove existing empty files in bulk, or add Basilisk/percent backends to Netatalk.
ROMAW supports those additional host formats independently.

Validation used an isolated share under the server backup directory, with no
production data converted. The added AFP handler probe checks data-only creation
and writable open with no sidecar, resource-only enumeration, required-anchor
creation, rename and deletion. Both Meson integration tests and the existing
OS X resource/FinderInfo/opaque ATTR round-trip fixture passed. After install and
restart, the service was active and responded to DSI GetStatus on port 548.
No interactive Mac client copy/save session was performed for this change.

Server backup and logs:
`/root/netatalk-empty-forks-20261102-073033/` contains `source-before.tar`, the
previous `sbin/` and `lib/`, isolated fixtures, build/install/test logs, and the
test configuration. `/root/netatalk-empty-forks-current` records that location.

To reproduce on the matching pre-change local tree, use `patch -p1 --dry-run`
before applying the patch, then `ninja -C build` and
`meson test -C build --print-errorlogs`. The additional handler probe runs when
`afpdtest` reads an isolated `test.conf` whose `test` share uses `ea = osx` and
`cnid scheme = last`; it creates files named `LazyPlain`, `LazyResource` and
`LazyRename` in that disposable share. Do not point it at production data.

Rollback requires stopping Netatalk, restoring the backed-up source and installed
`sbin/` and `lib/` directories, then starting it again. Keep the backup: it includes
the earlier local interoperability fixes that must not be replaced by pristine
upstream binaries.
