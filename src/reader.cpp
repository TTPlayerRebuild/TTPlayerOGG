#include "pictures.h"
#include <vorbis/vorbisfile.h>
#include <opusfile.h>
#include <cmath>
#include <climits>

namespace ttp::ogg {
namespace {
constexpr HRESULT not_vorbis = static_cast<HRESULT>(0x8bda0007);
constexpr GUID stream_read{0x83e2bbbf,0x4fec,0x4e00,{0xa6,0x2c,0x62,0x61,0xfe,0x2b,0xa2,0xd4}};
constexpr GUID stream_callback{0x1717a4a7,0xa3dc,0x416b,{0xa2,0x97,0x72,0x16,0x74,0x9b,0xd7,0xbf}};
class OggReader final : public Reader, public Metadata, public Thumbnail {
    LONG references_{1};
    ComPtr<IStream> stream_;
    ComPtr<IUnknown> extended_read_;
    ComPtr<Metadata> content_;
    OggVorbis_File vorbis_{};
    OggOpusFile *opus_{};
    bool is_opus_{};
    Bytes suffix_;
    std::vector<float> opus_pcm_;
    WAVEFORMATEX format_{};
    bool opened_{}, dirty_{}, seekable_{}, remote_{};
    DWORD duration_{}, mode_{}, serial_{};
    std::wstring path_;
    std::string vendor_;
    std::vector<Tag> tags_;
    std::vector<Cover> covers_;
    HRESULT io_error_{S_OK}, pending_error_{S_OK};
    HANDLE event_{CreateEventW(nullptr, TRUE, FALSE, nullptr)};

    static size_t read_callback(void *out, size_t size, size_t count, void *opaque) noexcept {
        auto &self = *static_cast<OggReader *>(opaque);
        if (!size || !count) return 0;
        if (size > MAXDWORD || count > MAXDWORD / size) {
            self.io_error_ = E_INVALIDARG; return 0;
        }
        ULONG got{};
        HRESULT hr;
        if (self.extended_read_) {
            // Current original 60552873 -> 83E2BBBF... slot 14, 60 s timeout.
            using Read = HRESULT(STDMETHODCALLTYPE *)(IUnknown *, void *, ULONG, ULONG *, HANDLE, DWORD);
            auto **v = *reinterpret_cast<void ***>(self.extended_read_.p);
            hr = reinterpret_cast<Read>(v[14])(self.extended_read_.p, out,
                      ULONG(size * count), &got, self.event_, 60000);
        } else hr = self.stream_->Read(out, ULONG(size * count), &got);
        if (FAILED(hr)) { self.io_error_ = hr; return 0; }
        if (got > size * count) { self.io_error_ = STG_E_READFAULT; return 0; }
        return got / size;
    }
    static int seek_callback(void *opaque, ogg_int64_t offset, int origin) noexcept {
        auto &self = *static_cast<OggReader *>(opaque);
        if (!self.seekable_ || origin < SEEK_SET || origin > SEEK_END) return -1;
        LARGE_INTEGER n{}; n.QuadPart = offset;
        const HRESULT hr = self.stream_->Seek(n, DWORD(origin), nullptr);
        return SUCCEEDED(hr) ? 0 : -1;
    }
    static long tell_callback(void *opaque) noexcept {
        auto &self = *static_cast<OggReader *>(opaque);
        LARGE_INTEGER z{}; ULARGE_INTEGER position{};
        if (!self.seekable_ || FAILED(self.stream_->Seek(z, STREAM_SEEK_CUR, &position))) return -1;
        // vorbisfile's Windows callback ABI has a signed 32-bit long.
        if (position.QuadPart > LONG_MAX) return -1;
        return long(position.QuadPart);
    }
    static int opus_read(void *opaque,unsigned char *data,int bytes) noexcept {
        auto &self=*static_cast<OggReader *>(opaque);
        if(bytes<0) return -1;
        const auto got=read_callback(data,1,size_t(bytes),opaque);
        return FAILED(self.io_error_)?-1:int(got);
    }
    static opus_int64 opus_tell(void *opaque) noexcept {
        auto &self=*static_cast<OggReader *>(opaque);
        LARGE_INTEGER z{}; ULARGE_INTEGER p{};
        if(!self.seekable_ || FAILED(self.stream_->Seek(z,STREAM_SEEK_CUR,&p)) || p.QuadPart>INT64_MAX) return -1;
        return opus_int64(p.QuadPart);
    }
    Bytes identify() {
        struct Sync { ogg_sync_state s{}; Sync(){check(ogg_sync_init(&s)?E_OUTOFMEMORY:S_OK);} ~Sync(){ogg_sync_clear(&s);} } sync;
        Bytes prefix;
        // Validate an Ogg BOS page and its first packet, not a substring or the
        // filename. Initial bytes are handed to the decoder, including on pipes.
        while(prefix.size()<16*1024*1024) {
            ogg_page page{};
            int status{};
            while((status=ogg_sync_pageout(&sync.s,&page))!=0) {
                if(status<0) continue;
                if(!ogg_page_bos(&page) || ogg_page_continued(&page) || !page.header[26]) continue;
                if(page.body_len>=19 && !memcmp(page.body,"OpusHead",8)) { is_opus_=true; return prefix; }
                if(page.body_len>=30 && !memcmp(page.body,"\1vorbis",7)) { is_opus_=false; return prefix; }
            }
            char *buffer=ogg_sync_buffer(&sync.s,4096); require(buffer,E_OUTOFMEMORY);
            const size_t n=read_callback(buffer,1,4096,this);
            if(!n) { check(io_error_); throw Failure{not_vorbis}; }
            prefix.insert(prefix.end(),buffer,buffer+n);
            require(ogg_sync_wrote(&sync.s,long(n))==0);
        }
        throw Failure{not_vorbis};
    }
    void close_decoder() noexcept {
        if (opus_) { op_free(opus_); opus_=nullptr; }
        if (opened_ && !is_opus_) ov_clear(&vorbis_);
        opened_=false;
    }
    void fallback_content() {
        if (content_ || !stream_) return;
        const auto create = standard_content();
        if (!create) return;
        LARGE_INTEGER z{}; ULARGE_INTEGER position{};
        const bool restore = SUCCEEDED(stream_->Seek(z, STREAM_SEEK_CUR, &position));
        ULONGLONG bytes{};
        const auto hr = create(stream_.p, 0, content_.put(), &bytes);
        if (FAILED(hr)) content_.reset();
        if (restore) { LARGE_INTEGER p{}; p.QuadPart = position.QuadPart; stream_->Seek(p, STREAM_SEEK_SET, nullptr); }
    }
    DWORD capabilities() const noexcept {
        DWORD bits = (remote_ ? 1u : 0u) | (seekable_ ? 2u : 0u);
        if (!remote_ && seekable_ && !path_.empty()) {
            const DWORD a = GetFileAttributesW(path_.c_str());
            if (a != INVALID_FILE_ATTRIBUTES && !(a & (FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_DIRECTORY))) bits |= 4 | 16;
        }
        return bits;
    }
    void load_tags() {
        const auto *v = is_opus_?nullptr:ov_comment(&vorbis_,0);
        const auto *o = is_opus_?op_tags(opus_,0):nullptr;
        if (v || o) {
            const char *vendor=o?o->vendor:v->vendor;
            if (vendor) vendor_=vendor;
            if(o) {
                int n{}; const auto *suffix=opus_tags_get_binary_suffix(o,&n);
                if(suffix && n>0) suffix_.assign(suffix,suffix+n);
            }
            const int count=o?o->comments:v->comments;
            char **values=o?o->user_comments:v->user_comments;
            const int *lengths=o?o->comment_lengths:v->comment_lengths;
            for (int i = 0; i < count; ++i) {
                const char *p = values[i];
                if (!p || !*p || lengths[i] < 0) continue;
                std::string raw(p, size_t(lengths[i]));
                const auto value = wide(raw);
                const auto eq = value.find(L'=');
                Tag tag;
                if (eq == std::wstring::npos || eq == 0) { tag.key = L"comment"; tag.value = value; }
                else { tag.key = value.substr(0, eq); tag.value = value.substr(eq + 1); }
                tag.original = std::move(raw);
                tags_.push_back(std::move(tag));
            }
        }
        covers_=read_pictures(tags_);
        if (tags_.empty()) fallback_content();
    }
    Bytes packet(const std::vector<Tag> &tags) const { return comment_packet(vendor_,tags,is_opus_,suffix_); }
    void change_tags(std::vector<Tag> updated) {
        packet(updated);
        auto pictures=read_pictures(updated);
        tags_.swap(updated); covers_.swap(pictures); dirty_=true;
    }
    ~OggReader() {
        close_decoder();
        if (dirty_ && stream_) {
            const HRESULT hr = protect([&]() -> HRESULT {
                extended_read_.reset();
                content_.reset();
                return save_comments(stream_, mode_, path_, serial_, packet(tags_));
            });
            if (FAILED(hr)) {
                wchar_t message[120];
                swprintf_s(message, L"ttp_ogg: metadata save failed (0x%08lX); rollback attempted.\n", ULONG(hr));
                OutputDebugStringW(message);
            }
        }
        if (event_) CloseHandle(event_);
    }
  public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (same(iid, IID_IUnknown) || same(iid, iid_reader)) *out = static_cast<Reader *>(this);
        else if (same(iid, iid_metadata)) *out = static_cast<Metadata *>(this);
        else if (same(iid, iid_thumbnail)) *out = static_cast<Thumbnail *>(this);
        else return E_NOINTERFACE;
        AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&references_); }
    ULONG STDMETHODCALLTYPE Release() override {
        const auto n = InterlockedDecrement(&references_); if (!n) delete this; return n;
    }
    HRESULT STDMETHODCALLTYPE Open(IStream *stream, DWORD) override {
        if (!stream) return E_POINTER;
        if (stream_) return E_UNEXPECTED;
        const HRESULT hr = protect([&]() -> HRESULT {
            io_error_ = pending_error_ = S_OK;
            duration_ = mode_ = serial_ = 0;
            path_.clear(); vendor_.clear(); format_ = {};
            suffix_.clear(); covers_.clear(); tags_.clear(); opus_pcm_.clear();
            dirty_ = seekable_ = remote_ = false;
            stream_ = ComPtr<IStream>(stream, true);
            STATSTG st{};
            if (SUCCEEDED(stream_->Stat(&st, STATFLAG_DEFAULT))) {
                struct Free { wchar_t *p; ~Free() { CoTaskMemFree(p); } } name{st.pwcsName};
                mode_ = st.grfMode;
                if (st.pwcsName) path_ = st.pwcsName;
                remote_ = path_.find(L"://") != std::wstring::npos;
            }
            LARGE_INTEGER z{}; ULARGE_INTEGER p{};
            seekable_ = SUCCEEDED(stream_->Seek(z, STREAM_SEEK_CUR, &p));
            stream_->QueryInterface(stream_read, reinterpret_cast<void **>(extended_read_.put()));
            auto initial=identify();
            unsigned channels{}; DWORD rate{};
            if(is_opus_) {
                OpusFileCallbacks callbacks{opus_read,seekable_?seek_callback:nullptr,seekable_?opus_tell:nullptr,nullptr};
                int error{};
                opus_=op_open_callbacks(this,&callbacks,initial.data(),initial.size(),&error);
                if(!opus_) return FAILED(io_error_)?io_error_:not_vorbis;
                opened_=true; seekable_=op_seekable(opus_)!=0;
                const auto *head=op_head(opus_,0);
                require(head && head->channel_count>0 && head->channel_count<=8 && head->mapping_family<=1,E_NOTIMPL);
                channels=unsigned(head->channel_count); rate=48000;
                const auto frames=op_pcm_total(opus_,-1);
                duration_=frames>0?DWORD(std::min<ogg_int64_t>(frames/48,MAXDWORD)):0;
                serial_=op_serialno(opus_,0);
                require(op_set_gain_offset(opus_,OP_HEADER_GAIN,0)==0);
                opus_pcm_.resize(8192*8);
            } else {
                ov_callbacks callbacks{read_callback,seek_callback,nullptr,tell_callback};
                if(ov_open_callbacks(this,&vorbis_,reinterpret_cast<const char *>(initial.data()),long(initial.size()),callbacks)<0)
                    return FAILED(io_error_)?io_error_:not_vorbis;
                opened_=true; seekable_=ov_seekable(&vorbis_)!=0;
                const auto *info=ov_info(&vorbis_,0);
                require(info && info->channels>0 && info->channels<=255 && info->rate>0);
                require(uint64_t(info->rate)*info->channels*8<=MAXDWORD);
                channels=unsigned(info->channels); rate=DWORD(info->rate);
                const double duration=ov_time_total(&vorbis_,-1)*1000.0;
                duration_=duration>0?DWORD(std::min(duration,double(MAXDWORD))):0;
                serial_=DWORD(ov_serialnumber(&vorbis_,0));
            }
            format_.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
            format_.wBitsPerSample = 64;
            format_.nChannels = WORD(channels);
            format_.nSamplesPerSec = rate;
            format_.nBlockAlign = WORD(channels * 8);
            format_.nAvgBytesPerSec = format_.nSamplesPerSec * format_.nBlockAlign;
            load_tags();
            return S_OK;
        });
        if (FAILED(hr)) { close_decoder(); extended_read_.reset(); content_.reset(); stream_.reset(); tags_.clear(); }
        return hr;
    }
    HRESULT STDMETHODCALLTYPE Capabilities(DWORD *out) override {
        if (!out) return E_POINTER; *out = capabilities(); return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Duration(DWORD *out) override {
        if (!out) return E_POINTER; *out = duration_; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Format(WAVEFORMATEX **out) override {
        if (!out) return E_POINTER; *out = nullptr;
        if (!opened_) return E_UNEXPECTED;
        auto *p = static_cast<WAVEFORMATEX *>(CoTaskMemAlloc(sizeof(format_)));
        if (!p) return E_OUTOFMEMORY; *p = format_; *out = p; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE BufferSize(DWORD *out) override {
        if (!out) return E_POINTER; *out = format_.nBlockAlign * 1024; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE CodecName(wchar_t **out) override { return text(is_opus_?L"OPUS|Opus Audio":L"Ogg|Vorbis/Ogg Audio", out); }
    HRESULT STDMETHODCALLTYPE Bitrate(DWORD *out) override {
        if (!out) return E_POINTER;
        if (!opened_) { *out = 0; return E_UNEXPECTED; }
        const long rate = is_opus_?op_bitrate(opus_,-1):ov_bitrate(&vorbis_, -1); *out = rate > 0 ? DWORD(rate) : 0; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE BitDepth(WORD *out) override {
        if (!out) return E_POINTER; *out = 16; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetCallback(IUnknown *callback) override {
        ComPtr<IUnknown> extension;
        if (stream_ && SUCCEEDED(stream_->QueryInterface(stream_callback, reinterpret_cast<void **>(extension.put()))) && extension) {
            using Set = HRESULT(STDMETHODCALLTYPE *)(IUnknown *, IUnknown *);
            auto **v = *reinterpret_cast<void ***>(extension.p);
            reinterpret_cast<Set>(v[4])(extension.p, callback);
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Start() override { return S_OK; }
    HRESULT STDMETHODCALLTYPE Stop() override { return S_OK; }
    HRESULT STDMETHODCALLTYPE Read(Buffer *target) override {
        if (!target) return E_POINTER;
        if (!opened_) return E_UNEXPECTED;
        return protect([&]() -> HRESULT {
            check(target->SetLength(0));
            if (FAILED(pending_error_)) return pending_error_;
            DWORD capacity{}, length{}; BYTE *data{};
            check(target->Capacity(&capacity)); check(target->Data(&data, &length));
            const DWORD align = format_.nBlockAlign;
            require(data && capacity >= align, HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER));
            capacity -= capacity % align;
            DWORD used{}; unsigned holes{};
            while (used < capacity) {
                float **pcm{}; int section{};
                const int frames=int(std::min<DWORD>((capacity-used)/align,8192));
                const long n = is_opus_?op_read_float(opus_,opus_pcm_.data(),frames*format_.nChannels,&section):
                    ov_read_float(&vorbis_, &pcm,frames,&section);
                if (n == OV_HOLE && ++holes <= 128) continue;
                // A chained stream may change format even when a tiny buffer
                // cannot hold one frame of the next link. Do not report EOF.
                if(is_opus_) {
                    const auto *head=op_head(opus_,-1);
                    if(head && (head->channel_count!=format_.nChannels || head->mapping_family>1)) {
                        pending_error_=E_NOTIMPL; break;
                    }
                }
                if (n <= 0) {
                    if (FAILED(io_error_)) pending_error_ = io_error_;
                    else if (n < 0) pending_error_ = HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
                    break;
                }
                holes = 0;
                const auto *info = is_opus_?nullptr:ov_info(&vorbis_, -1);
                if (!is_opus_ && (!info || info->channels != format_.nChannels || DWORD(info->rate) != format_.nSamplesPerSec)) {
                    pending_error_ = E_NOTIMPL; break;
                }
                constexpr unsigned surround[6]{0,2,1,5,3,4};
                constexpr unsigned opus_order[8][8]{{0},{0,1},{0,2,1},{0,1,2,3},
                    {0,2,1,3,4},{0,2,1,5,3,4},{0,2,1,6,5,3,4},{0,2,1,7,5,6,3,4}};
                // Keep original Vorbis 6055299A parity. Opus family 1 follows
                // Vorbis ordering and is mapped to Windows 3.0/quad/5.x/6.1/7.1.
                for (long frame = 0; frame < n; ++frame) {
                    for (unsigned channel = 0; channel < format_.nChannels; ++channel) {
                        const auto source=is_opus_?opus_order[format_.nChannels-1][channel]:
                            format_.nChannels==6?surround[channel]:channel;
                        const double value=is_opus_?opus_pcm_[size_t(frame)*format_.nChannels+source]:pcm[source][frame];
                        std::memcpy(data + used, &value, sizeof(value)); used += sizeof(value);
                    }
                }
            }
            check(target->SetLength(used));
            return used ? S_OK : (FAILED(pending_error_) ? pending_error_ : S_FALSE);
        });
    }
    HRESULT STDMETHODCALLTYPE Seek(DWORD *position) override {
        if (!position) return E_POINTER;
        if (!opened_) return E_UNEXPECTED;
        io_error_ = S_OK;
        const int result = is_opus_?op_pcm_seek(opus_,ogg_int64_t(*position)*48):ov_time_seek(&vorbis_, double(*position) / 1000.0);
        if (result) return FAILED(io_error_) ? io_error_ : E_FAIL;
        pending_error_ = S_OK; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Count(DWORD *out) override {
        if (!out) return E_POINTER;
        if (tags_.empty() && content_) return content_->Count(out);
        *out = DWORD(tags_.size()); return S_OK;
    }
    HRESULT STDMETHODCALLTYPE At(DWORD index, wchar_t **key, wchar_t **value) override {
        if (!key || !value) return E_POINTER; *key = nullptr; *value = nullptr;
        if (index >= tags_.size()) return content_ ? content_->At(index, key, value) : E_INVALIDARG;
        const auto hr = text(tags_[index].key, key); if (FAILED(hr)) return hr;
        const auto result = text(tags_[index].value, value);
        if (FAILED(result)) { CoTaskMemFree(*key); *key = nullptr; } return result;
    }
    HRESULT STDMETHODCALLTYPE Get(const char *key, wchar_t **value) override {
        if (!key || !value) return E_POINTER; *value = nullptr;
        return protect([&]() -> HRESULT {
            const auto name = wide(key);
            for (const auto &tag : tags_) if (!_wcsicmp(name.c_str(), tag.key.c_str())) return text(tag.value, value);
            // Hosts use ReplayGain's -18 LUFS reference. Opus R128 tags use
            // -23 LUFS (Q7.8 dB). Header gain is already in the decoded PCM.
            // Expose an adapter only when a real ReplayGain field is absent;
            // do not serialize it or apply a second normalization in libopusfile.
            if(is_opus_ && (!_stricmp(key,"replaygain_track_gain") || !_stricmp(key,"replaygain_album_gain"))) {
                const wchar_t *r128=!_stricmp(key,"replaygain_track_gain")?L"R128_TRACK_GAIN":L"R128_ALBUM_GAIN";
                for(const auto &tag:tags_) if(!_wcsicmp(tag.key.c_str(),r128)) {
                    wchar_t *end{}; const long gain=wcstol(tag.value.c_str(),&end,10);
                    if(end!=tag.value.c_str() && !*end && gain>=-32768 && gain<=32767) {
                        wchar_t buffer[48]; swprintf_s(buffer,L"%+.8f dB",double(gain)/256.0+5.0); return text(buffer,value);
                    }
                }
            }
            return tags_.empty() && content_ ? content_->Get(key, value) : E_INVALIDARG;
        });
    }
    HRESULT STDMETHODCALLTYPE Set(const char *key, const wchar_t *value) override {
        if (!key) return E_POINTER;
        if (!(capabilities() & 4)) return E_ACCESSDENIED;
        return protect([&]() -> HRESULT {
            const auto name = wide(key);
            require(!name.empty() && name.find(L'=') == std::wstring::npos, E_INVALIDARG);
            const std::wstring replacement = value ? value : L"";
            // Allocate before changing state so an OOM cannot leave a half edit.
            auto updated = tags_;
            auto found = std::find_if(updated.begin(), updated.end(), [&](const Tag &t) { return !_wcsicmp(t.key.c_str(), name.c_str()); });
            if (found != updated.end()) { found->value = replacement; found->changed = true; }
            else updated.push_back({name, replacement, {}, true});
            // Apply the same format bounds now rather than discovering them at Release.
            change_tags(std::move(updated));
            fallback_content();
            if (content_) content_->Set(key, value);
            return S_OK;
        });
    }
    HRESULT STDMETHODCALLTYPE PictureCount(DWORD *out) override {
        if(!out) return E_POINTER; *out=DWORD(covers_.size()); return S_OK;
    }
    HRESULT STDMETHODCALLTYPE MaximumBytes(DWORD *out) override {
        if(!out) return E_POINTER; *out=max_picture_bytes; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE MaximumCount(DWORD *out) override {
        if(!out) return E_POINTER; *out=max_pictures; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE PictureAt(DWORD index,const Picture **out) override {
        if(!out) return E_POINTER; *out=nullptr;
        if(index>=covers_.size()) return E_INVALIDARG;
        auto &p=covers_[index];
        p.view={sizeof(Picture),p.mime.c_str(),p.description.c_str(),DWORD(p.data.size()),p.data.data(),p.type};
        *out=&p.view; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE AddPicture(const Picture *picture) override {
        if(!picture) return E_POINTER;
        if(picture->size<sizeof(Picture)) return E_INVALIDARG;
        if(!(capabilities()&16)) return E_ACCESSDENIED;
        return protect([&]() -> HRESULT {
            require(covers_.size()<max_pictures,E_INVALIDARG);
            if(picture->type==1 || picture->type==2)
                for(const auto &p:covers_) require(p.type!=picture->type,E_INVALIDARG);
            auto updated=tags_; updated.push_back(picture_tag(*picture));
            change_tags(std::move(updated)); return S_OK;
        });
    }
    HRESULT STDMETHODCALLTYPE RemovePicture(DWORD index) override {
        if(!(capabilities()&16)) return E_ACCESSDENIED;
        if(index>=covers_.size()) return E_INVALIDARG;
        return protect([&]() -> HRESULT {
            auto updated=tags_;
            const auto &cover=covers_[index];
            updated[cover.tag_index].value.clear(); updated[cover.tag_index].changed=true;
            if(cover.mime_index!=SIZE_MAX) { updated[cover.mime_index].value.clear(); updated[cover.mime_index].changed=true; }
            change_tags(std::move(updated)); return S_OK;
        });
    }
    HRESULT STDMETHODCALLTYPE ReplacePicture(DWORD index,const Picture *picture,DWORD mask) override {
        if(!picture) return E_POINTER;
        if(!(capabilities()&16)) return E_ACCESSDENIED;
        if(index>=covers_.size() || picture->size<sizeof(Picture) || (mask&~15u)) return E_INVALIDARG;
        if(!mask) return S_OK;
        return protect([&]() -> HRESULT {
            const auto &old=covers_[index];
            Picture merged{sizeof(Picture),(mask&1)?picture->mime:old.mime.c_str(),
                (mask&2)?picture->description:old.description.c_str(),
                (mask&8)?picture->bytes:DWORD(old.data.size()),(mask&8)?picture->data:old.data.data(),
                (mask&4)?picture->type:old.type};
            if(merged.type==1 || merged.type==2) for(size_t i=0;i<covers_.size();++i)
                require(i==index || covers_[i].type!=merged.type,E_INVALIDARG);
            auto updated=tags_; updated[old.tag_index]=picture_tag(merged);
            if(old.mime_index!=SIZE_MAX) { updated[old.mime_index].value.clear(); updated[old.mime_index].changed=true; }
            change_tags(std::move(updated)); return S_OK;
        });
    }
};
} // namespace
HRESULT make_reader(void **out) {
    if (!out) return E_POINTER; *out = nullptr;
    return protect([&]() -> HRESULT { *out = static_cast<Reader *>(new OggReader); return S_OK; });
}
} // namespace ttp::ogg
