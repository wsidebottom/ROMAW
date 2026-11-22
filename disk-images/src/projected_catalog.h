/*+===================================================================
  File:      projected_catalog.h

  Summary:   Build immutable classic HFS catalogs for large direct-access images.

  Classes:   ProjectedRecord, ProjectedTree.

  Functions: Internal catalog serialization helpers.

  Origin:    Original adapter using the HFS disk layouts documented by the
             bundled hfsutils serializers and SuperMario HFS interfaces.

  Copyright and Legal notices: See COPYING.
===================================================================+*/

/* SPDX-License-Identifier: GPL-2.0-or-later */
static void projection_word(unsigned char* p, unsigned value)
{
    p[0] = (unsigned char)(value >> 8);
    p[1] = (unsigned char)value;
}

static void projection_long(unsigned char* p, uint32_t value)
{
    projection_word(p, value >> 16);
    projection_word(p + 2, value);
}

struct ProjectedRecord {
    uint32_t parent;
    std::string name;
    std::vector<unsigned char> bytes;

    ProjectedRecord(uint32_t parent, const std::string& name, size_t body) : parent(parent), name(name)
    {
        unsigned key_size = (8 + (unsigned)name.size()) & ~1u;
        bytes.resize(key_size + body);
        bytes[0] = (unsigned char)(6 + name.size());
        projection_long(bytes.data() + 2, parent);
        bytes[6] = (unsigned char)name.size();
        memcpy(bytes.data() + 7, name.data(), name.size());
    }

    unsigned char* body()
    {
        return bytes.data() + ((bytes[0] + 2) & ~1u);
    }
};

struct ProjectedTree {
    std::vector<std::array<unsigned char, 512>> nodes{1};

    explicit ProjectedTree(std::vector<ProjectedRecord>& records)
    {
        std::sort(records.begin(), records.end(), [](const auto& a, const auto& b) {
            if (a.parent != b.parent)
                return a.parent < b.parent;
            size_t n = std::min(a.name.size(), b.name.size());
            for (size_t i = 0; i < n; ++i)
                if (hfs_charorder[(unsigned char)a.name[i]] != hfs_charorder[(unsigned char)b.name[i]])
                    return hfs_charorder[(unsigned char)a.name[i]] < hfs_charorder[(unsigned char)b.name[i]];
            return a.name.size() < b.name.size();
        });
        std::vector<std::vector<unsigned char>> level;
        for (auto& record : records)
            level.push_back(std::move(record.bytes));
        uint32_t first_leaf = 0, last_leaf = 0, root = 0;
        unsigned height = 0;
        while (!level.empty()) {
            ++height;
            uint32_t first = (uint32_t)nodes.size(), previous = 0;
            std::vector<std::vector<unsigned char>> parents;
            for (size_t at = 0; at < level.size();) {
                std::array<unsigned char, 512> node{};
                unsigned offset = 14, count = 0;
                auto first_key = level[at];
                while (at < level.size() && offset + level[at].size() + 2 * (count + 2) <= 512) {
                    projection_word(node.data() + 510 - 2 * count, offset);
                    memcpy(node.data() + offset, level[at].data(), level[at].size());
                    offset += (unsigned)level[at++].size();
                    ++count;
                }
                check(count, "Projected catalog record exceeds node size");
                projection_word(node.data() + 510 - 2 * count, offset);
                node[8] = height == 1 ? 255 : 0;
                node[9] = (unsigned char)height;
                projection_word(node.data() + 10, count);
                uint32_t id = (uint32_t)nodes.size();
                projection_long(node.data() + 4, previous);
                if (previous)
                    projection_long(nodes[previous].data(), id);
                nodes.push_back(node);
                std::vector<unsigned char> index(42);
                index[0] = 37;
                memcpy(index.data() + 1, first_key.data() + 1, first_key[0]);
                projection_long(index.data() + 38, id);
                parents.push_back(std::move(index));
                previous = id;
            }
            if (height == 1) {
                first_leaf = first;
                last_leaf = previous;
            }
            if (parents.size() == 1) {
                root = first;
                break;
            }
            level = std::move(parents);
        }
        auto& header = nodes[0];
        header[8] = 1;
        projection_word(header.data() + 10, 3);
        projection_word(header.data() + 510, 14);
        projection_word(header.data() + 508, 120);
        projection_word(header.data() + 506, 248);
        projection_word(header.data() + 504, 504);
        projection_word(header.data() + 14, height);
        projection_long(header.data() + 16, root);
        projection_long(header.data() + 20, (uint32_t)records.size());
        projection_long(header.data() + 24, first_leaf);
        projection_long(header.data() + 28, last_leaf);
        projection_word(header.data() + 32, 512);
        projection_word(header.data() + 34, 37);
        uint32_t first_map = (uint32_t)nodes.size();
        size_t map_bytes = 256;
        while (nodes.size() > map_bytes * 8) {
            nodes.emplace_back();
            map_bytes += 492;
        }
        if (nodes.size() > first_map)
            projection_long(nodes[0].data(), first_map);
        std::vector<unsigned char> bitmap(map_bytes);
        for (size_t i = 0; i < nodes.size(); ++i)
            bitmap[i / 8] |= (unsigned char)(128 >> (i % 8));
        memcpy(nodes[0].data() + 248, bitmap.data(), 256);
        size_t offset = 256;
        for (size_t i = first_map; i < nodes.size(); ++i) {
            auto& map = nodes[i];
            projection_long(map.data(), i + 1 < nodes.size() ? (uint32_t)i + 1 : 0);
            map[8] = 2;
            projection_word(map.data() + 10, 1);
            projection_word(map.data() + 510, 14);
            projection_word(map.data() + 508, 506);
            memcpy(map.data() + 14, bitmap.data() + offset, 492);
            offset += 492;
        }
        projection_long(nodes[0].data() + 36, (uint32_t)nodes.size());
    }
};

static void projected_catalog(romaw_image_set& set, DirectDevice& device, const fs::path& root,
                              const std::vector<std::pair<fs::path, romaw_image_set::Item*>>& items,
                              const std::string& volume_name)
{
    struct Entry {
        fs::path alias;
        uint32_t id, parent, children = 0;
        romaw_image_set::Item* item;
    };

    std::map<fs::path, Entry> entries;
    std::map<fs::path, std::set<std::string>> siblings;
    uint32_t next = 16, files = 0, directories = 0, root_files = 0, root_directories = 0;
    for (auto& item : items) {
        fs::path alias = ".";
        uint32_t parent_id = 1, id = 2;
        if (item.first != ".") {
            auto parent = item.first.parent_path();
            if (parent.empty())
                parent = ".";
            auto found = entries.find(parent);
            check(found != entries.end() && found->second.item->info.directory, "Missing projected catalog parent");
            check(++found->second.children <= 65535, "Projected directory exceeds classic entry limit");
            parent_id = found->second.id;
            id = next++;
            alias =
                (found->second.alias / guest_component(item.first.filename(), siblings[parent], id)).lexically_normal();
        }
        entries.emplace(item.first, Entry{alias, id, parent_id, 0, item.second});
        set.guest_paths[(root / item.first).lexically_normal()] = alias;
        if (item.second->info.directory) {
            ++directories;
            if (parent_id == 2)
                ++root_directories;
        }
        else {
            if (parent_id == 2)
                ++root_files;
            ++files;
            device.forks.emplace(id, item.second);
        }
    }
    std::vector<ProjectedRecord> records;
    records.reserve(items.size() + directories);
    for (const auto& pair : entries) {
        const auto& entry = pair.second;
        const auto& info = entry.item->info;
        std::string name = entry.id == 2 ? volume_name : hfs_name(entry.alias.filename()).substr(1);
        records.emplace_back(entry.parent, name, info.directory ? 70 : 102);
        auto* body = records.back().body();
        body[0] = info.directory ? 1 : 2;
        if (info.directory) {
            projection_word(body + 4, entry.children);
            projection_long(body + 6, entry.id);
            memcpy(body + 22, info.finder_info, 32);
            records.emplace_back(entry.id, "", 46);
            body = records.back().body();
            body[0] = 3;
            projection_long(body + 10, entry.parent);
            body[14] = (unsigned char)name.size();
            memcpy(body + 15, name.data(), name.size());
        }
        else {
            check(info.data_size <= INT32_MAX && info.resource_size <= INT32_MAX,
                  "Image fork exceeds classic File Manager size limit");
            memcpy(body + 4, info.finder_info, 16);
            memcpy(body + 56, info.finder_info + 16, 16);
            projection_long(body + 20, entry.id);
            projection_long(body + 26, (uint32_t)info.data_size);
            projection_long(body + 30, (uint32_t)info.data_size);
            projection_long(body + 36, (uint32_t)info.resource_size);
            projection_long(body + 40, (uint32_t)info.resource_size);
        }
    }
    ProjectedTree tree(records);
    check(tree.nodes.size() <= 524288, "Projected catalog exceeds 256 MiB metadata limit");
    uint64_t catalog_size = tree.nodes.size() * 512;
    device.size = std::max<uint64_t>(8 * 1024 * 1024, catalog_size * 2 + 1024 * 1024);
    unsigned block_size = (unsigned)(1 + ((device.size / 512 - 6) >> 16)) * 512;
    unsigned blocks = (unsigned)((device.size - 19 * 512 - 1024) / block_size);
    unsigned ext_blocks = (512 + block_size - 1) / block_size;
    unsigned cat_blocks = (unsigned)((catalog_size + block_size - 1) / block_size);
    check(blocks <= 65535 && ext_blocks + cat_blocks <= blocks, "Invalid projected catalog allocation geometry");
    std::array<unsigned char, 512> mdb{};
    projection_word(mdb.data(), 0x4244);
    projection_word(mdb.data() + 12, root_files);
    projection_word(mdb.data() + 14, 3);
    projection_word(mdb.data() + 18, blocks);
    projection_long(mdb.data() + 20, block_size);
    projection_long(mdb.data() + 24, block_size);
    projection_word(mdb.data() + 28, 19);
    projection_long(mdb.data() + 30, next);
    projection_word(mdb.data() + 34, blocks - ext_blocks - cat_blocks);
    mdb[36] = (unsigned char)volume_name.size();
    memcpy(mdb.data() + 37, volume_name.data(), volume_name.size());
    projection_word(mdb.data() + 82, root_directories);
    projection_long(mdb.data() + 84, files);
    projection_long(mdb.data() + 88, directories - 1);
    projection_long(mdb.data() + 130, 512);
    projection_word(mdb.data() + 136, ext_blocks);
    projection_long(mdb.data() + 146, (uint32_t)catalog_size);
    projection_word(mdb.data() + 150, ext_blocks);
    projection_word(mdb.data() + 152, cat_blocks);
    device.metadata[2] = mdb;
    device.metadata[device.size / 512 - 2] = mdb;
    for (unsigned i = 0; i < ext_blocks + cat_blocks; ++i)
        device.metadata[3 + i / 4096][(i / 8) % 512] |= (unsigned char)(128 >> (i % 8));
    std::vector<ProjectedRecord> empty;
    ProjectedTree extents(empty);
    projection_word(extents.nodes[0].data() + 34, 7);
    device.metadata[19] = extents.nodes[0];
    uint64_t catalog_start = 19 + uint64_t(ext_blocks) * block_size / 512;
    for (size_t i = 0; i < tree.nodes.size(); ++i)
        device.metadata[catalog_start + i] = tree.nodes[i];
}
