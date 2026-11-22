/*+===================================================================
  File:      projection_test.cpp

  Summary:   Exercise large immutable catalogs without allocating or expanding source forks.

  Classes:   None.

  Functions: main.

  Origin:    Original Executor large-image and MFS regression fixture.

  Copyright and Legal notices: See COPYING.
===================================================================+*/

/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "../src/images.cpp"
#include <cassert>

int main(int argc, char** argv)
{
    assert(argc == 2);
    romaw_image_set set;
    set.direct = set.readonly = true;
    set.root = catalog_path(fs::u8path(argv[1]) / "unmaterialized");
    auto root = set.root / "volume-0";
    set.source = fs::u8path(argv[1]) / "original-image";
    set.volumes.push_back({2, native_text(root), "Projection Test"});
    image_directory(set, root);
    unsigned reads = 0;
    for (unsigned folder = 0; folder < 600; ++folder) {
        auto parent = root / ("Folder " + std::to_string(folder));
        image_directory(set, parent);
        for (unsigned file = 0; file < 20; ++file) {
            auto path = parent / ("Long sibling filename sharing the same prefix " + std::to_string(file));
            auto& item = set.items[path];
            item.info.data_size = folder == 0 ? 1024 * 1024 * 1024 : 19;
            item.info.resource_size = 13;
            memcpy(item.info.finder_info, "TEXTTEST", 8);
            item.data_range = [&reads, file](uint64_t offset, void* buffer, size_t size) {
                ++reads;
                auto* out = static_cast<unsigned char*>(buffer);
                for (size_t i = 0; i < size; ++i)
                    out[i] = (unsigned char)(offset + i + file);
            };
            item.resource_range = [&reads](uint64_t offset, void* buffer, size_t size) {
                ++reads;
                memset(buffer, (unsigned char)(offset + 17), size);
            };
        }
    }
    char error[1024];
    assert(romaw_images_device(&set, 0, error, sizeof error));
    assert(reads == 0 && !fs::exists(set.root));
    auto& device = *set.devices.at(0);
    assert(device.forks.size() == 12000 && device.ranges.empty());
    assert(device.size < 64 * 1024 * 1024);
    unsigned char projection_mdb[512];
    assert(romaw_images_device_read(&set, 0, 1024, projection_mdb, sizeof projection_mdb));
    assert(projection_mdb[12] == 0 && projection_mdb[13] == 0);
    assert(unsigned(projection_mdb[82]) * 256 + projection_mdb[83] == 600);
    romaw_hfs_ram(
        device.size,
        [](void* opaque, uint64_t offset, void* buffer, size_t size, int writing) -> int {
            return !writing && romaw_images_device_read(static_cast<romaw_image_set*>(opaque), 0, offset, buffer, size);
        },
        &set);
    auto* mounted = hfs_mount("Synthetic projection", 0, HFS_MODE_RDONLY);
    assert(mounted);
    for (const auto& pair : set.items) {
        if (pair.second.info.directory)
            continue;
        auto target = hfs_name(set.guest_paths.at(pair.first));
        hfsdirent entry;
        assert(!hfs_stat(mounted, target.c_str(), &entry));
        assert(entry.u.file.dsize == pair.second.info.data_size);
        unsigned char bytes[11];
        assert(romaw_images_device_fork_read(&set, 0, (uint32_t)entry.cnid, 0, entry.u.file.dsize - sizeof bytes, bytes,
                                             sizeof bytes) == 1);
        for (unsigned i = 1; i < sizeof bytes; ++i)
            assert(bytes[i] == (unsigned char)(bytes[0] + i));
        assert(romaw_images_device_fork_read(&set, 0, (uint32_t)entry.cnid, 1, 3, bytes, 10) == 1);
        assert(bytes[0] == 20 && bytes[9] == 20);
        assert(romaw_images_device_fork_read(&set, 0, (uint32_t)entry.cnid, 1, 13, bytes, 1) == -1);
    }
    assert(!hfs_umount(mounted));
    assert(reads == 24000 && !fs::exists(set.root));

    // Resolve actual guest aliases back to original virtual entries without I/O.
    for (const auto& entry : set.guest_paths) {
        char resolved[32768];
        auto guest = hfs_name(entry.second);
        assert(romaw_images_guest_path_utf8(&set, 0, guest.c_str(), resolved, sizeof resolved, error, sizeof error));
        assert(fs::u8path(resolved) == entry.first);
    }
    assert(reads == 24000 && !fs::exists(set.root));
    // Copy a small file through bounded range readers, preserving its resource fork.
    fs::create_directories(fs::u8path(argv[1]));
    auto selected = root / "Folder 1" / "Long sibling filename sharing the same prefix 0";
    auto exported = fs::u8path(argv[1]) / "streamed";
    fs::remove(exported);
    fs::remove(exported.parent_path() / "._streamed");
    uint64_t progress_done = 0;
    assert(romaw_images_export_progress_utf8(
        &set, selected.u8string().c_str(), exported.u8string().c_str(),
        [](void* opaque, uint64_t done, uint64_t total) -> int {
            auto& previous = *static_cast<uint64_t*>(opaque);
            assert(done >= previous && done - previous <= 65536 && total == 32);
            previous = done;
            return 1;
        },
        &progress_done, error, sizeof error));
    assert(progress_done == 32 && fs::file_size(exported) == 19);
    assert(fs::file_size(exported.parent_path() / "._streamed") > 32);
    auto cancelled = fs::u8path(argv[1]) / "cancelled";
    fs::remove(cancelled);
    assert(!romaw_images_export_progress_utf8(
        &set, selected.u8string().c_str(), cancelled.u8string().c_str(),
        [](void*, uint64_t, uint64_t) -> int { return 0; }, nullptr, error, sizeof error));
    assert(!fs::exists(cancelled) && !fs::exists(set.root));
    auto huge = root / "Folder 0" / "Long sibling filename sharing the same prefix 0";
    uint64_t largest_chunk = 0;
    assert(!romaw_images_export_progress_utf8(
        &set, huge.u8string().c_str(), cancelled.u8string().c_str(),
        [](void* opaque, uint64_t done, uint64_t total) -> int {
            auto& largest = *static_cast<uint64_t*>(opaque);
            assert(total == 1024 * 1024 * 1024 + 13 && done <= 65536);
            largest = std::max(largest, done);
            return done == 0;
        },
        &largest_chunk, error, sizeof error));
    assert(largest_chunk == 65536 && fs::file_size(cancelled) == 65536 && !fs::exists(set.root));

    /* A tiny MFS source includes a fragmented, preallocated data fork. */
    fs::create_directories(fs::u8path(argv[1]));
    auto source = fs::u8path(argv[1]) / "mfs.image";
    std::vector<unsigned char> raw(409600);
    auto* mdb = raw.data() + 1024;
    projection_word(mdb, 0xd2d7);
    projection_word(mdb + 12, 1);
    projection_word(mdb + 14, 5);
    projection_word(mdb + 16, 3);
    projection_word(mdb + 18, 792);
    projection_long(mdb + 20, 512);
    projection_word(mdb + 28, 8);
    mdb[36] = 3;
    memcpy(mdb + 37, "MFS", 3);
    auto fat = [&](unsigned block, unsigned next) {
        unsigned index = block - 2, at = 1088 + index * 3 / 2;
        if (index & 1) {
            raw[at] = (raw[at] & 240) | (unsigned char)(next >> 8);
            raw[at + 1] = (unsigned char)next;
        }
        else {
            raw[at] = (unsigned char)(next >> 4);
            raw[at + 1] = (raw[at + 1] & 15) | (unsigned char)(next << 4);
        }
    };
    fat(2, 4);
    fat(4, 1);
    auto* file = raw.data() + 5 * 512;
    file[0] = 128;
    memcpy(file + 2, "TEXTTEST", 8);
    projection_word(file + 22, 2);
    projection_long(file + 24, 600);
    projection_long(file + 28, 1024);
    file[50] = 4;
    memcpy(file + 51, "Test", 4);
    memset(raw.data() + 4096, 'A', 512);
    memset(raw.data() + 5120, 'B', 512);
    auto publish = [&] {
        std::ofstream output(source, std::ios::binary);
        output.write((const char*)raw.data(), raw.size());
        output.close();
        assert(output);
    };
    publish();
    auto* mfs = romaw_images_open_direct_utf8(source.u8string().c_str(), error, sizeof error);
    assert(mfs);
    unsigned char* bytes;
    size_t size;
    auto path = (fs::u8path(romaw_images_path(mfs, 0)) / "Test").u8string();
    assert(romaw_images_fork_utf8(mfs, path.c_str(), 0, &bytes, &size, error, sizeof error));
    assert(size == 600 && bytes[0] == 'A' && bytes[511] == 'A' && bytes[512] == 'B' && bytes[599] == 'B');
    free(bytes);
    romaw_images_close(mfs, 1);
    /* Partition streams can report their declared length even when the parent
       file is short. Detect damage at the APM boundary as well as the volume. */
    auto apm_source = fs::u8path(argv[1]) / "short-container.hda";
    std::vector<unsigned char> apm(4096 + raw.size());
    projection_word(apm.data(), 0x4552);
    projection_word(apm.data() + 2, 512);
    projection_long(apm.data() + 4, (uint32_t)(apm.size() / 512 + 1));
    projection_word(apm.data() + 512, 0x504d);
    projection_long(apm.data() + 516, 1);
    projection_long(apm.data() + 520, 8);
    projection_long(apm.data() + 524, (uint32_t)(raw.size() / 512 + 1));
    memcpy(apm.data() + 560, "Apple_HFS", 10);
    memcpy(apm.data() + 4096, raw.data(), raw.size());
    {
        std::ofstream output(apm_source, std::ios::binary);
        output.write((const char*)apm.data(), apm.size());
        output.close();
        assert(output);
    }
    mfs = romaw_images_open_direct_utf8(apm_source.u8string().c_str(), error, sizeof error);
    assert(mfs && strstr(romaw_images_warning(mfs), "incomplete"));
    romaw_images_close(mfs, 1);
    fat(4, 2);
    publish();
    assert(!romaw_images_open_direct_utf8(source.u8string().c_str(), error, sizeof error));
    fat(4, 1);
    raw.resize(5000);
    publish();
    assert(!romaw_images_open_direct_utf8(source.u8string().c_str(), error, sizeof error));
    puts("PASS: 12000-entry catalog, 20 GiB logical forks, aliases, map nodes, lazy ranges and bounded MFS failures");
}
