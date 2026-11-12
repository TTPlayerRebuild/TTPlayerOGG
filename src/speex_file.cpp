#include "speex_file.h"
#include <speex/speex_callbacks.h>
namespace ttp::ogg {
namespace {
DWORD little(const BYTE* p) { return DWORD(p[0]) | DWORD(p[1]) << 8 | DWORD(p[2]) << 16 | DWORD(p[3]) << 24; }
constexpr size_t max_pcm = 8 * 1024 * 1024;
}
SpeexFile::SpeexFile() { require(ogg_sync_init(&sync_) == 0, E_OUTOFMEMORY); sync_ready_ = true; }
SpeexFile::~SpeexFile() {
    if (decoder_) speex_decoder_destroy(decoder_);
    if (bits_ready_) speex_bits_destroy(&bits_);
    if (stream_ready_) ogg_stream_clear(&stream_);
    if (sync_ready_) ogg_sync_clear(&sync_);
}
void SpeexFile::header(const ogg_packet& packet) {
    require(packet.bytes >= 80 && !memcmp(packet.packet, "Speex   ", 8));
    const auto* p = packet.packet;
    require(little(p + 28) <= 1 && little(p + 32) >= 80 && little(p + 32) <= DWORD(packet.bytes));
    rate = little(p + 36); mode_ = int(little(p + 40)); channels = little(p + 48);
    require(rate >= 1000 && rate <= 192000 && mode_ >= 0 && mode_ <= 2 && channels >= 1 && channels <= 2);
    const auto mode = speex_lib_get_mode(mode_); require(mode != nullptr);
    require(little(p + 44) == DWORD(mode->bitstream_version), HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED));
    frames_per_packet_ = int(little(p + 64)); if (!frames_per_packet_) frames_per_packet_ = 1;
    extra_headers_ = int(little(p + 68));
    require(frames_per_packet_ > 0 && frames_per_packet_ <= 64 && extra_headers_ >= 0 && extra_headers_ <= 256);
    decoder_ = speex_decoder_init(mode); require(decoder_, E_OUTOFMEMORY);
    require(speex_decoder_ctl(decoder_, SPEEX_GET_FRAME_SIZE, &frame_size_) == 0 && frame_size_ > 0 && frame_size_ <= 640);
    require(little(p + 56) == DWORD(frame_size_));
    int sample_rate = int(rate), enhancement = 1;
    require(speex_decoder_ctl(decoder_, SPEEX_SET_SAMPLING_RATE, &sample_rate) == 0);
    require(speex_decoder_ctl(decoder_, SPEEX_SET_ENH, &enhancement) == 0);
    // A one-page file has no non-EOS granule anchor separating priming and
    // padding. Use the mode's encoder delay, not an invented PCM frame.
    auto encoder = speex_encoder_init(mode); require(encoder, E_OUTOFMEMORY);
    auto result = speex_encoder_ctl(encoder, SPEEX_GET_LOOKAHEAD, &fallback_delay_);
    speex_encoder_destroy(encoder); require(!result && fallback_delay_ >= 0);
    SpeexCallback callback{}; callback.callback_id = SPEEX_INBAND_STEREO;
    callback.func = speex_std_stereo_request_handler; callback.data = &stereo_;
    require(speex_decoder_ctl(decoder_, SPEEX_SET_HANDLER, &callback) == 0);
    speex_bits_init(&bits_); bits_ready_ = true;
    frame_.resize(size_t(frame_size_) * 2);
    const auto nominal = int32_t(little(p + 52)); bitrate = nominal > 0 ? DWORD(nominal) : 0;
}
void SpeexFile::comments(const ogg_packet& packet) {
    require(packet.bytes >= 8 && packet.bytes <= 16 * 1024 * 1024);
    size_t at{}; const auto size = size_t(packet.bytes);
    auto number = [&] { require(size - at >= 4); auto n = little(packet.packet + at); at += 4; return n; };
    auto string = [&] { auto n = number(); require(n <= size - at); std::string s(reinterpret_cast<char*>(packet.packet + at), n); at += n; return s; };
    vendor = string(); auto count = number(); require(count <= (size - at) / 4);
    for (DWORD i = 0; i < count; ++i) {
        auto raw = string(); auto value = wide(raw); auto eq = value.find(L'=');
        Tag tag; tag.original = std::move(raw);
        if (eq == value.npos || !eq) { tag.key = L"comment"; tag.value = value; }
        else { tag.key = value.substr(0, eq); tag.value = value.substr(eq + 1); }
        tags.push_back(std::move(tag));
    }
    suffix.assign(packet.packet + at, packet.packet + size);
}
void SpeexFile::decode(const ogg_packet& packet) {
    require(packet.bytes > 0 && packet.bytes <= 1024 * 1024);
    speex_bits_read_from(&bits_, reinterpret_cast<char*>(packet.packet), int(packet.bytes));
    int frames{};
    for (; frames < frames_per_packet_; ++frames) {
        const auto status = speex_decode(decoder_, &bits_, frame_.data());
        if (status == -1) break;
        require(status == 0 && speex_bits_remaining(&bits_) >= 0);
        if (channels == 2) speex_decode_stereo(frame_.data(), frame_size_, &stereo_);
        require(pcm_.size() + size_t(frame_size_) * channels <= max_pcm / sizeof(double), HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE));
        for (size_t i = 0; i < size_t(frame_size_) * channels; ++i) pcm_.push_back(double(frame_[i]) / 32768.0);
        raw_frames_ += frame_size_;
    }
    require(frames > 0);
}
bool SpeexFile::pump() {
    if (eos_) return false;
    pcm_.clear(); at_ = 0;
    for (;;) {
        ogg_page page{}; int status = ogg_sync_pageout(&sync_, &page);
        require(status >= 0, HRESULT_FROM_WIN32(ERROR_CRC));
        if (!status) {
            auto p = ogg_sync_buffer(&sync_, 8192); require(p, E_OUTOFMEMORY);
            const auto got = read_(opaque_, reinterpret_cast<unsigned char*>(p), 8192);
            require(got >= 0, STG_E_READFAULT); require(got != 0, HRESULT_FROM_WIN32(ERROR_HANDLE_EOF));
            require(ogg_sync_wrote(&sync_, got) == 0); continue;
        }
        if (!stream_ready_) {
            if (!ogg_page_bos(&page) || ogg_page_continued(&page) || page.body_len < 80 || memcmp(page.body, "Speex   ", 8)) continue;
            serial = DWORD(ogg_page_serialno(&page)); require(ogg_stream_init(&stream_, int(serial)) == 0, E_OUTOFMEMORY); stream_ready_ = true;
        }
        if (DWORD(ogg_page_serialno(&page)) != serial) continue;
        const auto granule = ogg_page_granulepos(&page); const bool end = ogg_page_eos(&page) != 0;
        require(stream_.body_fill >= stream_.body_returned && page.body_len >= 0 &&
            uint64_t(stream_.body_fill - stream_.body_returned) + uint64_t(page.body_len) <= 16 * 1024 * 1024 + 65536,
            HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE));
        require(ogg_stream_pagein(&stream_, &page) == 0);
        ogg_packet packet{};
        while ((status = ogg_stream_packetout(&stream_, &packet)) != 0) {
            require(status > 0);
            if (!packet_count_) header(packet);
            else if (packet_count_ == 1) comments(packet);
            else if (packet_count_ > 1 + extra_headers_) decode(packet);
            ++packet_count_;
        }
        if (!pcm_.empty()) {
            if (!origin_known_ && (granule >= 0 || end)) {
                if (end) skip_ = uint64_t(fallback_delay_);
                else { require(uint64_t(granule) <= raw_frames_); skip_ = raw_frames_ - uint64_t(granule); }
                origin_known_ = true;
            }
            if (origin_known_) {
                const auto skip = std::min<uint64_t>(skip_, pcm_.size() / channels);
                at_ = size_t(skip) * channels; skip_ -= skip;
                if (end) {
                    require(granule >= 0 && uint64_t(granule) >= produced_);
                    const auto remaining = uint64_t(granule) - produced_;
                    require(remaining <= (pcm_.size() - at_) / channels);
                    pcm_.resize(at_ + size_t(remaining) * channels); total = uint64_t(granule);
                }
                produced_ += (pcm_.size() - at_) / channels;
            }
        }
        if (granule >= 0 && raw_frames_) { require(last_granule_ <= granule); last_granule_ = granule; }
        if (end) { require(packet_count_ >= 2 && granule >= 0); eos_ = true; total = uint64_t(granule); }
        if (at_ < pcm_.size() && origin_known_) return true;
        if (end) return false;
        // Keep unanchored data only until the next complete page/anchor.
        if (origin_known_) { pcm_.clear(); at_ = 0; }
    }
}
void SpeexFile::open(void* opaque, Read read, const Bytes& initial) {
    opaque_ = opaque; read_ = read;
    auto p = ogg_sync_buffer(&sync_, long(initial.size())); require(p, E_OUTOFMEMORY);
    memcpy(p, initial.data(), initial.size()); require(ogg_sync_wrote(&sync_, long(initial.size())) == 0);
    pump(); require(packet_count_ >= 2 && decoder_);
}
DWORD SpeexFile::read(BYTE* out, DWORD frames) {
    if (!frames) return 0;
    if (at_ == pcm_.size() && !pump()) return 0;
    const auto n = std::min<size_t>(frames, (pcm_.size() - at_) / channels);
    memcpy(out, pcm_.data() + at_, n * channels * 8);
    at_ += n * channels; return DWORD(n);
}
void SpeexFile::discard(uint64_t frames) {
    std::vector<BYTE> buffer(1024 * channels * 8);
    while (frames) { const auto got = read(buffer.data(), DWORD(std::min<uint64_t>(frames, 1024))); require(got, STG_E_SEEKERROR); frames -= got; }
}
uint64_t SpeexFile::scan_total(IStream* input, DWORD selected) {
    ogg_sync_state sync{}; require(ogg_sync_init(&sync) == 0, E_OUTOFMEMORY);
    struct Clear { ogg_sync_state* p; ~Clear() { ogg_sync_clear(p); } } guard{&sync};
    seek(input, 0); bool end{}; uint64_t total{};
    for (;;) {
        ogg_page page{}; auto status = ogg_sync_pageout(&sync, &page); require(status >= 0, HRESULT_FROM_WIN32(ERROR_CRC));
        if (status) {
            if (DWORD(ogg_page_serialno(&page)) == selected && ogg_page_eos(&page)) {
                const auto n = ogg_page_granulepos(&page); require(n >= 0); total = uint64_t(n); end = true;
            } else if (end && ogg_page_bos(&page) && page.body_len >= 8 && !memcmp(page.body, "Speex   ", 8))
                require(false, HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED)); // no silent truncation of chains
            continue;
        }
        auto p = ogg_sync_buffer(&sync, 8192); require(p, E_OUTOFMEMORY); ULONG got{};
        check(input->Read(p, 8192, &got)); require(got <= 8192); if (!got) break;
        require(ogg_sync_wrote(&sync, got) == 0);
    }
    require(end && sync.fill == sync.returned, HRESULT_FROM_WIN32(ERROR_HANDLE_EOF)); return total;
}
}
