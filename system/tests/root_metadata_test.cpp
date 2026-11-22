/*+===================================================================
  File:      root_metadata_test.cpp

  Summary:   Checks isolated .romaw profile paths, root metadata and installation settings.

  Classes:   Used: std::ofstream, std::pair, std::string, std::vector

  Functions: main

  Origin:    Existing repository source in executor/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* SPDX-License-Identifier: MIT */
#include "../../runtime/user_profile.cpp"
#include <cassert>

static void write_record(const fs::path& path, const RootBytes& bytes)
{
    std::ofstream output(path, std::ios::binary);
    output.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    output.close();
    assert(output);
}

static RootBytes record(unsigned char marker)
{
    RootBytes bytes(88, marker);
    auto put = [&bytes](size_t p, uint32_t word) {
        for (int shift = 24; shift >= 0; shift -= 8)
            bytes[p++] = word >> shift;
    };
    put(0, 0x00051607);
    put(4, 0x00020000);
    bytes[24] = 0;
    bytes[25] = 2;
    put(26, 9);
    put(30, 50);
    put(34, 32);
    put(38, 2);
    put(42, 82);
    put(46, 6);
    return bytes;
}

static fs::path metadata(const fs::path& root)
{
    char* text = romaw_root_metadata_path(root.string().c_str());
    assert(text);
    fs::path result(text);
    free(text);
    return result;
}

static void check_profile_files(const fs::path& fixture)
{
    auto directory = fixture / ".romaw", installation = fixture / "installation";
    fs::create_directory(directory);
    fs::create_directory(installation);
    for (const char* extension : {"prefs", "ftm"}) {
        std::string name = std::string("ROMAW.") + extension;
        auto current = directory / name;
        const auto saved = record(0x21), installed = record(0x32), master = record(0x54);
        write_record(current, saved);
        write_record(installation / name, installed);
        write_record(installation / (name + ".master"), master);
        char path[32768], error[1024];
        assert(romaw_profile_path(name.c_str(), path, sizeof path, error, sizeof error));
        assert(fs::u8path(path) == current);
        assert(romaw_profile_migrate(installation.u8string().c_str(), error, sizeof error));
        assert(read_root_metadata(current) == saved);
        fs::remove(current);
        for (const auto& candidate : {std::pair{name, installed}, std::pair{name + ".master", master}}) {
            assert(romaw_profile_migrate(installation.u8string().c_str(), error, sizeof error));
            assert(read_root_metadata(current) == candidate.second);
            assert(read_root_metadata(installation / candidate.first) == candidate.second);
            fs::remove(current);
            fs::remove(installation / candidate.first);
        }
    }
    puts("Profile files: current settings retained, installation/template precedence passed");
}

static void profile_home(const fs::path& home)
{
#ifdef _WIN32
    assert(SetEnvironmentVariableW(L"USERPROFILE", home.c_str()));
#else
    assert(!setenv("HOME", home.c_str(), 1));
#endif
}

static void check_profile_directory(const fs::path& fixture)
{
    auto home = fixture / fs::u8path("profile-caf\xc3\xa9");
    fs::create_directory(home);
    profile_home(home);
    const auto saved = record(0x21);
    auto current = home / ".romaw";
    assert(profile() == current);
    assert(fs::is_directory(current / "cache"));
    assert(std::distance(fs::directory_iterator(home), fs::directory_iterator{}) == 1);

    char path[32768], error[1024];
    assert(romaw_profile_path("ROMAW.prefs", path, sizeof path, error, sizeof error));
    assert(fs::u8path(path) == current / "ROMAW.prefs");
    assert(!fs::exists(current / "ROMAW.prefs"));
    write_record(current / "ROMAW.prefs", saved);
    assert(profile() == current);
    assert(romaw_profile_path("ROMAW.prefs", path, sizeof path, error, sizeof error));
    assert(read_root_metadata(current / "ROMAW.prefs") == saved);
    assert(!romaw_profile_path("../other", path, sizeof path, error, sizeof error));
    assert(error[0]);
    profile_home(fixture);
    puts("Profile directory: direct creation, Unicode paths and existing settings passed");
}

int main()
{
    auto fixture = fs::temp_directory_path() / "romaw-root-metadata-test";
    for (unsigned i = 0; !fs::create_directory(fixture); ++i)
        fixture = fs::temp_directory_path() / ("romaw-root-metadata-test-" + std::to_string(i));
#ifdef _WIN32
    wchar_t previous[32768];
    DWORD old = GetEnvironmentVariableW(L"USERPROFILE", previous, 32768);
    assert(SetEnvironmentVariableW(L"USERPROFILE", fixture.c_str()));
#else
    const char* old = getenv("HOME");
    std::string previous = old ? old : "";
    assert(!setenv("HOME", fixture.c_str(), 1));
#endif
    check_profile_directory(fixture);
    check_profile_files(fixture);
    auto drive = fixture / "drive", other = fixture / "other";
    fs::create_directory(drive);
    fs::create_directory(other);
    auto path = metadata(drive);
    assert(path == metadata(drive / "."));
    assert(path != metadata(other));
    auto relative = path.lexically_normal().lexically_relative((fixture / ".romaw/volume-metadata").lexically_normal());
    assert(!relative.empty() && !relative.is_absolute() && *relative.begin() != "..");
    assert(fs::is_empty(drive));
    unsigned char volume_info[32], restored[32];
    memset(volume_info, 0x24, sizeof volume_info);
    memset(restored, 0x56, sizeof restored);
    assert(romaw_volume_finder_info(drive.string().c_str(), restored, 0) == 0);
    assert(restored[0] == 0x56);
    assert(romaw_volume_finder_info(drive.string().c_str(), volume_info, 1) == 1);
    assert(romaw_volume_finder_info((drive / ".").string().c_str(), restored, 0) == 1);
    assert(!memcmp(volume_info, restored, sizeof restored));
    memset(volume_info, 0x78, sizeof volume_info);
    assert(romaw_volume_finder_info(drive.string().c_str(), volume_info, 1) == 1);
    assert(romaw_volume_finder_info(drive.string().c_str(), restored, 0) == 1);
    assert(!memcmp(volume_info, restored, sizeof restored));
    assert(romaw_volume_finder_info(other.string().c_str(), restored, 0) == 0);
    auto volume_path = path.parent_path() / "volume.finderinfo";
    write_record(volume_path, RootBytes(31, 0xff));
    memset(restored, 0x56, sizeof restored);
    assert(romaw_volume_finder_info(drive.string().c_str(), restored, 0) == -1);
    assert(restored[0] == 0x56);
    fs::remove(volume_path);
    fs::create_directory(volume_path);
    assert(romaw_volume_finder_info(drive.string().c_str(), volume_info, 1) == -1);
    assert(fs::is_directory(volume_path));
    fs::remove(volume_path);
    auto original = record(0x24), redundant = record(0x56);
    write_record(drive / "%%2F", original);
    write_record(drive / "._%%2F", redundant);
    assert(path == metadata(drive));
    assert(fs::is_empty(drive));
    assert(read_root_metadata(path) == original);
    assert(read_root_metadata(path.parent_path() / "legacy-root.appledouble") == original);
    assert(read_root_metadata(path.parent_path() / "legacy-sidecar.appledouble") == redundant);
    auto changed = record(0x78);
    write_record(path, changed);
    assert(metadata(drive) == path);
    assert(read_root_metadata(path) == changed);
    // A recreated, conflicting legacy record must not replace current data.
    write_record(drive / "%%2F", redundant);
    assert(metadata(drive) == path);
    assert(read_root_metadata(path) == changed);
    assert(read_root_metadata(drive / "%%2F") == redundant);
    fs::remove(drive / "%%2F");
    // Leave unrelated and truncated files on the drive untouched.
    write_record(other / "._%%2F", redundant);
    for (auto invalid :
         {RootBytes{'t', 'e', 'x', 't'}, RootBytes(25), RootBytes(original.begin(), original.end() - 1)}) {
        write_record(other / "%%2F", invalid);
        metadata(other);
        assert(read_root_metadata(other / "%%2F") == invalid);
        assert(read_root_metadata(other / "._%%2F") == redundant);
    }
    fs::remove(other / "%%2F");
    fs::remove(other / "._%%2F");
    fs::create_directory(other / "%%2F");
    metadata(other);
    assert(fs::is_directory(other / "%%2F"));
    fs::remove(other / "%%2F");
    // Exact stored identities prevent a hash collision from sharing records.
    auto identity = path.parent_path() / "volume.path";
    auto saved_identity = read_root_metadata(identity);
    write_record(identity, RootBytes{'w', 'r', 'o', 'n', 'g'});
    assert(!romaw_root_metadata_path(drive.string().c_str()));
    assert(read_root_metadata(path) == changed);
    write_record(identity, saved_identity);
    assert(!romaw_root_metadata_path("relative"));
    assert(errno == EINVAL);
    assert(!romaw_root_metadata_path(nullptr));
    assert(errno == EINVAL);
    // A blocked profile must fail without changing the source.
    auto blocked = fixture / "blocked";
    fs::create_directory(blocked);
    write_record(blocked / ".romaw", original);
#ifdef _WIN32
    assert(SetEnvironmentVariableW(L"USERPROFILE", blocked.c_str()));
#else
    assert(!setenv("HOME", blocked.c_str(), 1));
#endif
    write_record(other / "%%2F", original);
    assert(!romaw_root_metadata_path(other.string().c_str()));
    assert(read_root_metadata(other / "%%2F") == original);
#ifdef _WIN32
    assert(SetEnvironmentVariableW(L"USERPROFILE", old ? previous : nullptr));
#else
    assert(!(old ? setenv("HOME", previous.c_str(), 1) : unsetenv("HOME")));
#endif
    fs::remove_all(fixture);
    puts("Root metadata: isolated profile paths, installation settings, backups, conflicts and failures passed");
}
