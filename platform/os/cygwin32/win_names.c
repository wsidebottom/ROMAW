/*+===================================================================
  File:      win_names.c

  Summary:   Implements win names support for host-platform configuration and integration.

  Classes:   None.

  Functions: ROMlib_host_get_file_id, ROMlib_host_locate_file_id, ROMlib_host_name_to_mac, ROMlib_mac_name_to_utf8,
             ROMlib_win_hide_desktop, ROMlib_win_icon_open, ROMlib_win_icon_stat, ROMlib_win_share_file_replacement

  Origin:    Existing repository source in executor/src/config/os/cygwin32; original attribution and provenance comments
             are preserved below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* SPDX-License-Identifier: MIT */
#if defined(HAVE_CONFIG_H)
#include "config.h"
#endif
#include <windows.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <io.h>
#include <errno.h>
#include "rsys/host_names.h"
#include "rsys/mac_storage.h"

int ROMlib_win_share_file_replacement(int fd)
{
    /* Desktop DB is published by atomic replacement. Retain its real open
       file while allowing the database writer to replace the directory entry. */
    HANDLE file =
        ReOpenFile((HANDLE)_get_osfhandle(fd), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, 0);
    if (file == INVALID_HANDLE_VALUE) {
        DWORD error = GetLastError();
        errno = error == ERROR_SHARING_VIOLATION ? EBUSY : error == ERROR_INVALID_HANDLE ? EBADF : EACCES;
        return -1;
    }
    int shared = _open_osfhandle((intptr_t)file, O_RDONLY | O_BINARY);
    if (shared < 0) {
        CloseHandle(file);
        return -1;
    }
    int result = _dup2(shared, fd);
    int saved_errno = errno;
    _close(shared);
    errno = saved_errno;
    return result;
}

int ROMlib_host_get_file_id(const char* path, ROMlib_host_file_id* id)
{
    const DWORD sharing = FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;
    HANDLE file = CreateFileA(path, 0, sharing, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    if (file == INVALID_HANDLE_VALUE)
        return 0;
    BY_HANDLE_FILE_INFORMATION info;
    const BOOL ok = GetFileInformationByHandle(file, &info);
    CloseHandle(file);
    if (ok) {
        id->volume_serial = info.dwVolumeSerialNumber;
        id->index_high = info.nFileIndexHigh;
        id->index_low = info.nFileIndexLow;
    }
    return ok && (info.nFileIndexHigh || info.nFileIndexLow);
}

char* ROMlib_host_locate_file_id(const char* volume_path, const ROMlib_host_file_id* id)
{
    const DWORD sharing = FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;
    HANDLE volume = CreateFileA(volume_path, 0, sharing, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    if (volume == INVALID_HANDLE_VALUE)
        return NULL;
    BY_HANDLE_FILE_INFORMATION info;
    HANDLE file = INVALID_HANDLE_VALUE;
    if (GetFileInformationByHandle(volume, &info) && info.dwVolumeSerialNumber == id->volume_serial) {
        FILE_ID_DESCRIPTOR descriptor = {.dwSize = sizeof descriptor, .Type = FileIdType};
        descriptor.FileId.QuadPart = ((uint64_t)id->index_high << 32) | id->index_low;
        file = OpenFileById(volume, &descriptor, FILE_READ_ATTRIBUTES, sharing, NULL, FILE_FLAG_BACKUP_SEMANTICS);
    }
    CloseHandle(volume);
    if (file == INVALID_HANDLE_VALUE)
        return NULL;
    DWORD capacity = MAX_PATH;
    char* path = malloc(capacity);
    if (path) {
        DWORD copied = GetFinalPathNameByHandleA(file, path, capacity, FILE_NAME_NORMALIZED);
        if (copied >= capacity && copied < UINT32_MAX) {
            capacity = copied + 1;
            char* larger = realloc(path, capacity);
            if (larger) {
                path = larger;
                copied = GetFinalPathNameByHandleA(file, path, capacity, FILE_NAME_NORMALIZED);
            }
            else
                copied = 0;
        }
        if (!copied || copied >= capacity) {
            free(path);
            path = NULL;
        }
        else if (strncmp(path, "\\\\?\\UNC\\", 8) == 0) {
            path[6] = '\\';
            memmove(path, path + 6, strlen(path + 6) + 1);
        }
        else if (strncmp(path, "\\\\?\\", 4) == 0)
            memmove(path, path + 4, strlen(path + 4) + 1);
    }
    CloseHandle(file);
    return path;
}

int ROMlib_mac_name_to_utf8(const char* input, int length, char* output, int capacity)
{
    if (!input || length < 0 || length > 255 || !output || capacity <= 0)
        return -1;
    WCHAR wide[255];
    int count = MultiByteToWideChar(10000, 0, input, length, wide, 255);
    int bytes = count ? WideCharToMultiByte(CP_UTF8, 0, wide, count, output, capacity - 1, NULL, NULL) : 0;
    if (length && !bytes)
        return -1;
    output[bytes] = 0;
    return bytes;
}

int ROMlib_host_name_to_mac(const char* input, int length, char* output)
{
    if (length <= 0)
        return length;
    int ascii = 1;
    for (int i = 0; i < length; ++i)
        if ((unsigned char)input[i] >= 128)
            ascii = 0;
    if (ascii) {
        memmove(output, input, length);
        return length;
    }
    WCHAR* wide = malloc((size_t)length * sizeof *wide);
    WCHAR* check = malloc((size_t)length * sizeof *check);
    char* mac = malloc(length);
    int result = -1;
    if (wide && check && mac) {
        int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, input, length, wide, length);
        if (!count)
            count = MultiByteToWideChar(CP_ACP, 0, input, length, wide, length);
        if (count) {
            BOOL substituted = FALSE;
            int bytes = WideCharToMultiByte(10000, 0, wide, count, mac, length, NULL, &substituted);
            int restored = bytes ? MultiByteToWideChar(10000, 0, mac, bytes, check, length) : 0;
            if (bytes && !substituted && restored == count && !memcmp(wide, check, count * sizeof *wide)) {
                memmove(output, mac, bytes);
                result = bytes;
            }
        }
    }
    free(mac);
    free(check);
    free(wide);
    return result;
}

/* SMB represents the classic Icon<CR> helper as Icon<U+F00D>. Keep this
   explicit alias separate from general filename normalization: ANSI APIs
   cannot represent that private-use character without losing its identity. */
static WCHAR* icon_alias(const char* path)
{
    const char* leaf = path;
    for (const char* p = path; *p; ++p)
        if (*p == '/' || *p == '\\')
            leaf = p + 1;
    int raw = !_stricmp(leaf, MAC_STORAGE_CUSTOM_ICON_NAME) || !_stricmp(leaf, "._" MAC_STORAGE_CUSTOM_ICON_NAME) ||
              !_stricmp(leaf, "%" MAC_STORAGE_CUSTOM_ICON_NAME);
    if (!raw && _stricmp(leaf, MAC_STORAGE_ESCAPED_ICON_NAME) && _stricmp(leaf, "._" MAC_STORAGE_ESCAPED_ICON_NAME) &&
        _stricmp(leaf, "%" MAC_STORAGE_ESCAPED_ICON_NAME))
        return NULL;
    int length = MultiByteToWideChar(CP_ACP, 0, path, -1, NULL, 0);
    if (length < 4)
        return NULL;
    WCHAR* wide = malloc((size_t)length * sizeof *wide);
    if (!wide) {
        errno = ENOMEM;
        return NULL;
    }
    if (!MultiByteToWideChar(CP_ACP, 0, path, -1, wide, length)) {
        free(wide);
        return NULL;
    }
    int end = length - (raw ? 2 : 4);
    wide[end] = MAC_STORAGE_SFM_ICON_CHARACTER;
    wide[end + 1] = 0;
    return wide;
}

int ROMlib_win_icon_stat(const char* path, struct stat* info)
{
    WCHAR* wide = icon_alias(path);
    if (!wide)
        return -1;
    struct _stat64 native;
    int result = _wstat64(wide, &native);
    if (!result) {
        memset(info, 0, sizeof *info);
        info->st_dev = native.st_dev;
        info->st_ino = native.st_ino;
        info->st_mode = native.st_mode;
        info->st_nlink = native.st_nlink;
        info->st_uid = native.st_uid;
        info->st_gid = native.st_gid;
        info->st_rdev = native.st_rdev;
        info->st_size = native.st_size;
        info->st_atime = native.st_atime;
        info->st_mtime = native.st_mtime;
        info->st_ctime = native.st_ctime;
    }
    free(wide);
    return result;
}

int ROMlib_win_icon_open(const char* path, int flags, int mode)
{
    if (flags & (O_CREAT | O_EXCL))
        return -1;
    WCHAR* wide = icon_alias(path);
    if (!wide)
        return -1;
    int result = _wopen(wide, flags, mode);
    free(wide);
    return result;
}

void ROMlib_win_hide_desktop(int fd, const char* path)
{
    const char* name = path;
    for (const char* p = path; *p; ++p)
        if (*p == '/' || *p == '\\')
            name = p + 1;
    if (_stricmp(name, "Desktop"))
        return;
    int saved_errno = errno;
    DWORD saved_error = GetLastError();
    (void)fd;
    DWORD attributes = GetFileAttributesA(path);
    if (attributes != INVALID_FILE_ATTRIBUTES && !(attributes & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_DIRECTORY)))
        SetFileAttributesA(path, (attributes & ~FILE_ATTRIBUTE_NORMAL) | FILE_ATTRIBUTE_HIDDEN);
    SetLastError(saved_error);
    errno = saved_errno;
}
