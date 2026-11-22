/*+===================================================================
  File:      image_copy.h

  Summary:   Manage Finder catalog items and stream both forks between mounted images and host folders.

  Classes:   Declared: ImageCatalogItem, ImageCopySource; uses CopyJob and Copies.

  Functions: Interface declarations; see below.

  Origin:    Executor native Finder direct image integration.

  Copyright and Legal notices: See COPYING.
===================================================================+*/

#ifndef FINDER_IMAGE_COPY_H
#define FINDER_IMAGE_COPY_H
#include "finder/files.h"
#include "finder/image_volumes.h"

namespace finder {
struct ImageCatalogItem {
    uint32_t parent, id;
    std::string name;
};

uint32_t create_image_folder(int16_t reference, uint32_t parent);
bool image_copy_private(int16_t reference, uint32_t id);

inline FileSize image_file_size(const FinderImageEntry& entry)
{
    return {uint64_t(entry.data_size) + entry.resource_size, uint64_t(entry.data_allocated) + entry.resource_allocated};
}

// Mounted File Manager calls stay on the Finder thread. Each advance has a
// small work budget and returns when a complete total is available.
class ImageFolderSize {
  public:
    ImageFolderSize(int16_t reference, uint32_t directory);
    bool advance();

    FileSize size() const
    {
        return total;
    }

  private:
    struct Frame {
        uint32_t directory, children, next = 1;
    };

    int16_t reference;
    uint32_t root;
    std::vector<Frame> pending;
    FileSize total;
    bool started = false;
};

std::string rename_image_item(int16_t reference, const ImageCatalogItem& item, const std::wstring& name);
std::string image_catalog_name(const std::wstring& name);
void label_image_item(int16_t reference, const ImageCatalogItem& item, unsigned label);
void stationery_image_item(int16_t reference, const ImageCatalogItem& item, bool stationery);
// The caller confirms permanent deletion before handing over the selected catalog identities.
void delete_image_items(int16_t reference, const std::vector<ImageCatalogItem>& items);

struct ImageCopySource {
    fs::path source;
    unsigned partition;
    std::string guest;
    int16_t reference = 0;
    ImageCatalogItem catalog{};
};

std::shared_ptr<CopyJob> copy_image_items(Copies& copies, const std::vector<ImageCopySource>& sources,
                                          const fs::path& destination, const std::wstring& stationery_name = {});
// Advance these File Manager writes with Copies::tick on the Finder UI thread.
std::shared_ptr<CopyJob> copy_into_image(Copies& copies, const std::vector<fs::path>& host_sources,
                                         const std::vector<ImageCopySource>& image_sources, int16_t reference,
                                         uint32_t directory, const fs::path& destination_key,
                                         TransferKind kind = TransferKind::copy,
                                         const std::string& stationery_name = {});

}
#endif
