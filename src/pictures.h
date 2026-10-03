#pragma once
#include "comments.h"
namespace ttp::ogg {
inline constexpr DWORD max_picture_bytes = 16 * 1024 * 1024;
inline constexpr DWORD max_pictures = 64;
struct Cover {
    std::wstring mime, description;
    Bytes data;
    DWORD type{3};
    size_t tag_index{};
    size_t mime_index{SIZE_MAX}; // Legacy COVERARTMIME paired with COVERART.
    Picture view{}; // One descriptor per entry; another PictureAt does not invalidate it.
};
std::vector<Cover> read_pictures(const std::vector<Tag> &tags);
Tag picture_tag(const Picture &picture);
} // namespace ttp::ogg
