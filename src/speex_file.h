#pragma once
#include "comments.h"
#include <ogg/ogg.h>
#include <speex/speex.h>
#include <speex/speex_stereo.h>
namespace ttp::ogg {
// Sequential Ogg/Speex decoder with bounded page/packet storage. Seek replays
// from BOS, preserving all CELP, stereo and enhancement state exactly.
class SpeexFile {
public:
    using Read = int (*)(void*, unsigned char*, int);
private:
    void* opaque_{};
    Read read_{};
    ogg_sync_state sync_{};
    ogg_stream_state stream_{};
    bool sync_ready_{}, stream_ready_{}, eos_{}, origin_known_{};
    void* decoder_{};
    SpeexBits bits_{};
    bool bits_ready_{};
    SpeexStereoState stereo_ = SPEEX_STEREO_STATE_INIT;
    int mode_{}, frame_size_{}, frames_per_packet_{}, extra_headers_{}, packet_count_{}, fallback_delay_{};
    uint64_t raw_frames_{}, skip_{}, produced_{};
    int64_t last_granule_{-1};
    std::vector<double> pcm_;
    size_t at_{};
    std::vector<float> frame_;
    void header(const ogg_packet&);
    void comments(const ogg_packet&);
    void decode(const ogg_packet&);
    bool pump();
public:
    DWORD rate{}, channels{}, serial{}, bitrate{};
    uint64_t total{};
    std::string vendor;
    std::vector<Tag> tags;
    Bytes suffix;
    SpeexFile();
    ~SpeexFile();
    void open(void*, Read, const Bytes& initial);
    DWORD read(BYTE*, DWORD frames);
    void discard(uint64_t frames);
    uint64_t position() const { return produced_ - (pcm_.size() - at_) / channels; }
    static uint64_t scan_total(IStream*, DWORD serial);
};
}
