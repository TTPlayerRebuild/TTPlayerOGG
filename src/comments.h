#pragma once
#include "common.h"
namespace ttp::ogg {
struct Tag {
    std::wstring key, value;
    std::string original;
    bool changed{};
};
Bytes comment_packet(const std::string &vendor, const std::vector<Tag> &tags,
                     bool opus = false, const Bytes &suffix = {});
// Replaces only the first selected logical stream's comment packet. Audio
// lacing, payload, granule positions, other streams and trailing bytes survive.
void rewrite_comments(IStream *source, IStream *destination, DWORD serial, const Bytes &packet);
// Original ABI saves on final Release. This guarded copy keeps a rollback
// image; it cannot make in-place writes atomic across power loss.
HRESULT save_comments(ComPtr<IStream> &source, DWORD mode, const std::wstring &path,
                      DWORD serial, const Bytes &packet) noexcept;
} // namespace ttp::ogg
