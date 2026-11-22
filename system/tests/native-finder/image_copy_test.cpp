/*+===================================================================
  File:      image_copy_test.cpp

  Summary:   Verify a direct mounted-image copy preserves both forks and Finder metadata.

  Classes:   Uses finder::Copies and finder::CopyJob.

  Functions: wmain

  Origin:    Executor native Finder regression fixture.

  Copyright and Legal notices: See COPYING.
===================================================================+*/
#include "finder/image_copy.h"
#include "romaw_images.h"
#include <iostream>
#include <stdexcept>

int wmain(int argc, wchar_t** argv)
{
    using namespace finder;
    try {
        if (argc != 5)
            throw std::runtime_error("Expected image, MacRoman guest path, partition, and empty destination folder");
        fs::path source(argv[1]), destination(argv[4]);
        if (!fs::create_directory(destination))
            throw std::runtime_error("Choose a new destination folder");
        std::string guest;
        for (const wchar_t* p = argv[2]; *p; ++p) {
            if (*p > 127)
                throw std::runtime_error("This command-line probe requires an ASCII guest name");
            guest += char(*p);
        }
        unsigned partition = unsigned(std::stoul(argv[3]));
        char error[1024]{}, path[32768]{};
        auto* reader = romaw_images_open_direct_utf8(source.u8string().c_str(), error, sizeof error);
        if (!reader)
            throw std::runtime_error(error);
        std::shared_ptr<romaw_image_set> owned(reader, [](auto* set) { romaw_images_close(set, 0); });
        if (!romaw_images_guest_path_utf8(reader, partition, guest.c_str(), path, sizeof path, error, sizeof error))
            throw std::runtime_error(error);
        romaw_image_item info{};
        if (!romaw_images_stat_utf8(reader, path, &info) || info.directory)
            throw std::runtime_error("Select a file for the copy probe");
        Copies copies;
        auto job = copy_image_items(copies, {{source, partition, guest}}, destination);
        job->worker.join();
        if (job->state != CopyJob::complete)
            throw std::runtime_error("Image copy did not complete");
        auto target = job->destinations.at(0);
        auto mac = mac_file(target);
        if (fs::file_size(target) != info.data_size || mac.fork.size() != info.resource_size ||
            job->done != info.data_size + info.resource_size || job->items != 0)
            throw std::runtime_error("Copied fork sizes or progress disagree with source catalog");
        auto type = uint32_t(info.finder_info[0]) << 24 | uint32_t(info.finder_info[1]) << 16 |
                    uint32_t(info.finder_info[2]) << 8 | info.finder_info[3];
        if (mac.type != type)
            throw std::runtime_error("Finder type changed during copy");
        std::cout << "PASS: direct image copy, both forks and Finder metadata: " << target.u8string() << '\n';
    }
    catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
