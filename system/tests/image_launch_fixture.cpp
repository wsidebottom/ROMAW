/*+===================================================================
  File:      image_launch_fixture.cpp

  Summary:   Install original test launcher resources into a new HFS fixture.

  Classes:   None.

  Functions: main.

  Origin:    Executor mounted-image launch regression fixture.

  Copyright and Legal notices: See COPYING; libhfs retains its upstream license.
===================================================================+*/
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
extern "C" {
#include "../../disk-images/third_party/hfsutils/hfs.h"
void romaw_hfs_medium(const char*, uint64_t, uint64_t);
}

int main(int argc, char** argv)
{
    assert(argc == 2);
    std::filesystem::path work(argv[1]);
    auto image = (work / "Launchers.hfv").string();
    constexpr size_t size = 2 * 1024 * 1024;
    {
        std::ofstream output(image, std::ios::binary);
        std::vector<char> zeros(size);
        output.write(zeros.data(), zeros.size());
        assert(output.good());
    }
    romaw_hfs_medium(image.c_str(), 0, size);
    assert(!hfs_format(image.c_str(), 0, 0, "Mount Handoff", 0, nullptr));
    auto* volume = hfs_mount(image.c_str(), 0, HFS_MODE_RDWR);
    assert(volume);
    for (const char* name : {"ContinueLauncher", "ReplaceLauncher"}) {
        std::ifstream input(work / (std::string(name) + ".rsrc"), std::ios::binary);
        std::vector<char> bytes((std::istreambuf_iterator<char>(input)), {});
        assert(!bytes.empty());
        auto* file = hfs_create(volume, (":" + std::string(name)).c_str(), "APPL", "Mtst");
        assert(file && !hfs_setfork(file, 1));
        assert(hfs_write(file, bytes.data(), bytes.size()) == bytes.size());
        assert(!hfs_close(file));
    }
    assert(!hfs_umount(volume));
    return 0;
}
