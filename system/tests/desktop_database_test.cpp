/*+===================================================================
  File:      desktop_database_test.cpp

  Summary:   Verify classic disk layouts, persistence, deletion/rebuild and bad files.

  Classes:   None identified in this file.

  Functions: main

  Origin:    Existing repository source in executor/tests; original attribution and provenance comments are preserved
             below.

  Copyright and Legal notices: File-specific notices, where present, are retained below. License reference: COPYING.
===================================================================+*/

/* Verify classic disk layouts, persistence, deletion/rebuild and bad files. */
#include "../../files/desktop_database.cpp"
#include <cassert>
#include <iostream>

int main(int argc, char** argv)
{
    assert(argc == 2);
    fs::path root = argv[1];
    fs::create_directories(root);
    size_t size = 0;
    uint32_t tag = 0;
    assert(!romaw_desktop_database(argv[1], desktop_delete, nullptr, 0, nullptr, &size, &tag));
    assert(!romaw_desktop_database(argv[1], desktop_open, nullptr, 0, nullptr, &size, &tag) && !size);
    auto tree = read_file(root / "Desktop DB", 1024 * 1024);
    assert(tree.size() == 16384 && tree[8] == 1);
    assert(get(&tree[14 + 20], 2) == 37 && tree[14 + 36] == 255);
    assert(tree[120] == 2 && tree[121] == 10 && tree[122] == 1);
    assert(get(&tree[510], 2) == 14 && get(&tree[508], 2) == 120 && get(&tree[506], 2) == 248 && tree[248] == 128);
    unsigned char key[38] = {11, 1, 'T', 'E', 'S', 'T', 'A', 'P', 'P', 'L', 1};
    unsigned char art[1024];
    for (unsigned i = 0; i < sizeof art; ++i)
        art[i] = i;
    size = sizeof art;
    tag = 0x10203040;
    assert(!romaw_desktop_database(argv[1], desktop_put, key, 0, art, &size, &tag));
    auto fork = read_file(root / "Desktop DF", 2048);
    assert(fork == Bytes(art, art + sizeof art));
    unsigned char out[1028];
    memset(out, 0xc3, sizeof out);
    size = 7;
    tag = 0;
    assert(!romaw_desktop_database(argv[1], desktop_get, key, 0, out + 2, &size, &tag));
    assert(size == 7 && tag == 0x10203040 && !memcmp(out + 2, art, 7));
    assert(out[1] == 0xc3 && out[9] == 0xc3);
    size = 256;
    assert(romaw_desktop_database(argv[1], desktop_put, key, 0, art, &size, &tag) == -5030);
    unsigned char prefix[38] = {5, 1, 'T', 'E', 'S', 'T'}, value[10];
    size = sizeof value;
    assert(!romaw_desktop_database(argv[1], desktop_index, prefix, 1, value, &size, &tag));
    assert(!memcmp(prefix, key, 12) && get(value, 4) == 0x10203040 && get(value + 4, 4) == 0 &&
           get(value + 8, 2) == 1024);

    /* Build a three-level tree containing all three classic key types. */
    Records records = decode(read_file(root / "Desktop DB", 1024 * 1024));
    for (unsigned i = 0; i < 1200; ++i) {
        Bytes comment = {5, 3, 0, 0, 0, 0};
        put(comment.data() + 2, i + 16, 4);
        Bytes text(200, 0x41);
        text[0] = 199;
        records[comment] = text;
    }
    Bytes application = {7, 2, 'T', 'E', 'S', 'T', 255, 255};
    Bytes app(40, 0);
    put(app.data(), 0x456789ab, 4);
    put(app.data() + 4, 2, 4);
    app[8] = 3;
    memcpy(app.data() + 9, "App", 3);
    records[application] = app;
    tree = encode(records);
    assert(get(&tree[14], 2) >= 3 && decode(tree) == records);
    /* Independently traverse every emitted index and leaf through its on-disk
     child references, not decode's linked-leaf path. */
    unsigned leaves = 0, leaf_records = 0;
    auto visit = [&](auto&& self, unsigned number, unsigned height) -> void {
        assert(number && number < tree.size() / 512);
        auto node = tree.data() + number * 512;
        assert(node[9] == height);
        unsigned count = get(node + 10, 2);
        if (height == 1) {
            assert(node[8] == 255);
            ++leaves;
            leaf_records += count;
        }
        else {
            assert(!node[8]);
            for (unsigned i = 0; i < count; ++i) {
                auto record = node + get(node + 510 - i * 2, 2);
                assert(record[0] == 37);
                self(self, get(record + 38, 4), height - 1);
            }
        }
    };
    visit(visit, get(&tree[16], 4), get(&tree[14], 2));
    assert(leaves > 100 && leaf_records == records.size());
    write_file(root / "Desktop DB", tree);
    size = 0;
    assert(!romaw_desktop_database(argv[1], desktop_open, nullptr, 0, nullptr, &size, &tag) && size == 1);
    // The direct reader fetches individual nodes and icon ranges from the
    // original files, without opening locks or retaining a file snapshot.
    auto direct = [](void* opaque, unsigned file, uint64_t offset, void* output, size_t count) -> int {
        auto* directory = static_cast<fs::path*>(opaque);
        assert(count <= 32768);
        std::ifstream input(*directory / (file ? "Desktop DF" : "Desktop DB"), std::ios::binary);
        input.seekg(offset);
        return bool(input.read(static_cast<char*>(output), count));
    };
    size = sizeof art;
    tag = 0;
    assert(
        !romaw_desktop_database_read(direct, &root, tree.size(), fork.size(), desktop_get, key, 0, out, &size, &tag));
    assert(size == sizeof art && tag == 0x10203040 && !memcmp(out, art, size));
    unsigned char app_prefix[38] = {5, 2, 'T', 'E', 'S', 'T'}, app_record[40];
    size = sizeof app_record;
    assert(!romaw_desktop_database_read(direct, &root, tree.size(), fork.size(), desktop_index, app_prefix, 1,
                                        app_record, &size, &tag));
    assert(size == 40 && !memcmp(app_record, app.data(), size));
    assert(romaw_desktop_database_read(direct, &root, tree.size(), 0, desktop_get, key, 0, out, &size, &tag) == -1305);
    assert(romaw_desktop_database_read(direct, &root, tree.size(), fork.size(), desktop_put, key, 0, out, &size,
                                       &tag) == -44);
    for (size_t n : {size_t(0), size_t(511), tree.size() - 1}) {
        write_file(root / "Desktop DB", Bytes(tree.begin(), tree.begin() + n));
        assert(romaw_desktop_database(argv[1], desktop_open, nullptr, 0, nullptr, &size, &tag) == -1305);
    }
    write_file(root / "Desktop DB", tree);
    write_file(root / "Desktop DF", {});
    assert(romaw_desktop_database(argv[1], desktop_open, nullptr, 0, nullptr, &size, &tag) == -1305);
    fs::remove(root / "Desktop DB");
    assert(!romaw_desktop_database(argv[1], desktop_open, nullptr, 0, nullptr, &size, &tag) && !size);
    size = sizeof art;
    assert(!romaw_desktop_database(argv[1], desktop_put, key, 0, art, &size, &tag));
    size = sizeof art;
    assert(!romaw_desktop_database(argv[1], desktop_get, key, 0, out, &size, &tag) && !memcmp(out, art, size));
    assert(!romaw_desktop_database(argv[1], desktop_reset, nullptr, 0, nullptr, &size, &tag));
    assert(fs::file_size(root / "Desktop DF") == 0);
    assert(romaw_desktop_database(argv[1], desktop_get, key, 0, out, &size, &tag) == -413);
    write_file(root / "Desktop DB", tree);
    write_file(root / "Desktop DF", Bytes(art, art + sizeof art));
    fs::remove(root / "Desktop DF");
    assert(!romaw_desktop_database(argv[1], desktop_open, nullptr, 0, nullptr, &size, &tag) && !size);
    auto retained = decode(read_file(root / "Desktop DB", 1024 * 1024));
    assert(retained.size() == records.size() - 1 && retained.at(application) == app);
    for (const auto& entry : retained)
        assert(entry.first[1] != 1);
    std::cout << "PASS classic Desktop DB/DF layout, index traversal, color icon "
                 "persistence, truncation, reset and deletion/rebuild\n";
}
