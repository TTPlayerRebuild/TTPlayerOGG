#include "comments.h"
#include <ogg/ogg.h>
#include <shlwapi.h>
#include <array>

namespace ttp::ogg {
namespace {
constexpr size_t max_comment_bytes = 64 * 1024 * 1024;
DWORD le32(const BYTE *p) { return DWORD(p[0]) | DWORD(p[1]) << 8 | DWORD(p[2]) << 16 | DWORD(p[3]) << 24; }
void set_le32(BYTE *p, DWORD n) { for (unsigned i=0;i<4;++i) p[i] = BYTE(n >> (8*i)); }
void append32(Bytes &b, DWORD n) { for (unsigned i=0;i<4;++i) b.push_back(BYTE(n >> (8*i))); }
void add_string(Bytes &b, const std::string &s) {
    require(s.size() <= max_comment_bytes - 4 && b.size() <= max_comment_bytes - s.size() - 4,
            HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE));
    append32(b, DWORD(s.size())); b.insert(b.end(), s.begin(), s.end());
}
struct Page {
    Bytes header, body;
    DWORD serial() const { return le32(header.data()+14); }
    DWORD sequence() const { return le32(header.data()+18); }
    bool bos() const { return (header[5] & 2) != 0; }
    bool eos() const { return (header[5] & 4) != 0; }
    void checksum() {
        ogg_page p{header.data(),long(header.size()),body.data(),long(body.size())};
        ogg_page_checksum_set(&p);
    }
    void write(IStream *out) const {
        write_exact(out, header.data(), DWORD(header.size()));
        if (!body.empty()) write_exact(out, body.data(), DWORD(body.size()));
    }
};
Page read_page(IStream *in, uint64_t available) {
    require(available >= 27);
    Page p; p.header.resize(27); read_exact(in,p.header.data(),27);
    require(!memcmp(p.header.data(),"OggS",4) && p.header[4] == 0 && (p.header[5] & ~7) == 0);
    const size_t segments = p.header[26];
    require(available >= 27+segments);
    p.header.resize(27+segments);
    if (segments) read_exact(in,p.header.data()+27,DWORD(segments));
    size_t length{}; for(size_t i=27;i<p.header.size();++i) length += p.header[i];
    require(available >= p.header.size()+length);
    p.body.resize(length); if(length) read_exact(in,p.body.data(),DWORD(length));
    const DWORD crc = le32(p.header.data()+22); p.checksum();
    require(le32(p.header.data()+22) == crc, HRESULT_FROM_WIN32(ERROR_CRC));
    return p;
}
void copy_bytes(IStream *in,IStream *out,uint64_t bytes) {
    std::array<BYTE,64*1024> buffer{};
    while(bytes) {
        const DWORD n = DWORD(std::min<uint64_t>(bytes,buffer.size()));
        read_exact(in,buffer.data(),n); write_exact(out,buffer.data(),n); bytes-=n;
    }
}
void write_fragment(const Page &original,size_t first,size_t end,size_t offset,
                    DWORD &sequence,IStream *out,bool prefix) {
    if(first==end) return;
    Page p;
    p.header.assign(original.header.begin(),original.header.begin()+27);
    p.header[26]=BYTE(end-first);
    p.header.insert(p.header.end(),original.header.begin()+27+first,original.header.begin()+27+end);
    size_t bytes{}; bool completed{};
    for(size_t i=first;i<end;++i) { bytes+=original.header[27+i]; completed |= original.header[27+i]<255; }
    require(offset<=original.body.size() && bytes<=original.body.size()-offset);
    p.body.assign(original.body.begin()+offset,original.body.begin()+offset+bytes);
    if(prefix) {
        p.header[5] &= 3; // comment is not the end of a valid Vorbis stream
        std::fill(p.header.begin()+6,p.header.begin()+14,completed ? BYTE(0) : BYTE(255));
    } else p.header[5] &= 4; // suffix starts after the completed comment packet
    set_le32(p.header.data()+18,sequence++); p.checksum(); p.write(out);
}
void write_comment(IStream *out,DWORD serial,DWORD &sequence,const Bytes &packet) {
    // An exact multiple of 255 requires a terminating zero lacing value.
    const size_t segments=packet.size()/255+1;
    size_t at{},offset{};
    while(at<segments) {
        const size_t n=std::min<size_t>(255,segments-at);
        Page page; page.header.assign(27+n,0);
        memcpy(page.header.data(),"OggS",4); page.header[5]=at?1:0;
        const bool last=at+n==segments;
        std::fill(page.header.begin()+6,page.header.begin()+14,last?BYTE(0):BYTE(255));
        set_le32(page.header.data()+14,serial); set_le32(page.header.data()+18,sequence++);
        page.header[26]=BYTE(n);
        size_t length{};
        for(size_t i=0;i<n;++i) {
            const BYTE lace=at+i+1==segments?BYTE(packet.size()%255):BYTE(255);
            page.header[27+i]=lace; length+=lace;
        }
        page.body.assign(packet.begin()+offset,packet.begin()+offset+length);
        page.checksum(); page.write(out); at+=n; offset+=length;
    }
}
struct Temporary {
    std::wstring name;
    ComPtr<IStream> stream;
    bool keep{};
    explicit Temporary(const std::wstring &directory) {
        wchar_t path[MAX_PATH]{};
        require(GetTempFileNameW(directory.c_str(),L"ogg",0,path)!=0,HRESULT_FROM_WIN32(GetLastError()));
        name=path;
        const auto hr=SHCreateStreamOnFileEx(path,STGM_READWRITE|STGM_SHARE_EXCLUSIVE,
                                           FILE_ATTRIBUTE_NORMAL,FALSE,nullptr,stream.put());
        if(FAILED(hr)) { DeleteFileW(path); throw Failure{hr}; }
    }
    ~Temporary() { stream.reset(); if(!keep) DeleteFileW(name.c_str()); }
};
} // namespace
Bytes comment_packet(const std::string &vendor,const std::vector<Tag> &tags,bool opus,const Bytes &suffix) {
    Bytes out = opus ? Bytes{'O','p','u','s','T','a','g','s'} : Bytes{3,'v','o','r','b','i','s'};
    add_string(out,vendor);
    require(out.size() <= max_comment_bytes - 4, HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE));
    const size_t count_at=out.size(); append32(out,0); DWORD count{};
    for(const auto &tag:tags) {
        // Original setter changes first duplicate; serialization removes empty values.
        if(tag.value.empty()) continue;
        const auto raw=tag.changed ? utf8(tag.key)+"="+utf8(tag.value) : tag.original;
        add_string(out,raw); ++count;
    }
    set_le32(out.data()+count_at,count);
    const size_t extra = opus ? suffix.size() : 1;
    require(extra <= max_comment_bytes && out.size() <= max_comment_bytes - extra,
            HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE));
    if (opus) out.insert(out.end(), suffix.begin(), suffix.end());
    else out.push_back(1);
    return out;
}
void rewrite_comments(IStream *source,IStream *destination,DWORD serial,const Bytes &packet) {
    require(source && destination && packet.size()>=8 && packet.size()<=max_comment_bytes,E_INVALIDARG);
    seek(source,0); seek(destination,0);
    const uint64_t total=stream_size(source);
    uint64_t position{};
    // Preserve a bounded leading tag/prefix, rather than deleting ID3 metadata.
    bool located{};
    while(position<total && position<=16*1024*1024) {
        const size_t n=size_t(std::min<uint64_t>(65536,total-position));
        auto data=read_at(source,position,n);
        size_t i{};
        for(;i+4<=data.size();++i) if(!memcmp(data.data()+i,"OggS",4)) { located=true; break; }
        if(located) { if(i) write_exact(destination,data.data(),DWORD(i)); position+=i; break; }
        require(data.size()>3);
        const DWORD advance=DWORD(data.size()-3); write_exact(destination,data.data(),advance); position+=advance;
    }
    require(located);
    bool selected{},done{},inserted{},continuation{};
    unsigned packet_index{};
    DWORD sequence{},expected{};
    while(position<total) {
        seek(source,position);
        if(total-position<4) { require(done); copy_bytes(source,destination,total-position); position=total; break; }
        BYTE marker[4]; read_exact(source,marker,4); seek(source,position);
        if(memcmp(marker,"OggS",4)) { require(done); copy_bytes(source,destination,total-position); position=total; break; }
        Page page=read_page(source,total-position);
        position+=page.header.size()+page.body.size();
        if(page.serial()!=serial || done) { page.write(destination); continue; }
        if(!selected) {
            require(page.bos() && !(page.header[5]&1)); selected=true;
            sequence=expected=page.sequence();
        }
        require(page.sequence()==expected++ && bool(page.header[5]&1)==continuation);
        const size_t segments=page.header[26];
        size_t offset{},suffix_start=segments,suffix_offset{};
        bool touched{};
        for(size_t i=0;i<segments;++i) {
            const BYTE lace=page.header[27+i];
            if(packet_index==1) {
                touched=true;
                if(!inserted) {
                    write_fragment(page,0,i,0,sequence,destination,true);
                    write_comment(destination,serial,sequence,packet); inserted=true;
                }
            }
            offset+=lace;
            continuation=lace==255;
            if(!continuation) {
                if(packet_index==1) { suffix_start=i+1; suffix_offset=offset; }
                ++packet_index;
            }
        }
        if(touched) write_fragment(page,suffix_start,segments,suffix_offset,sequence,destination,false);
        else { set_le32(page.header.data()+18,sequence++); page.checksum(); page.write(destination); }
        if(page.eos()) { require(packet_index>=3 && !continuation); done=true; }
    }
    require(selected && inserted && done);
    ULARGE_INTEGER size{}; size.QuadPart=tell(destination); check(destination->SetSize(size));
    check(destination->Commit(STGC_DEFAULT));
}
HRESULT save_comments(ComPtr<IStream> &source,DWORD mode,const std::wstring &path,DWORD serial,const Bytes &packet) noexcept {
    return protect([&]() -> HRESULT {
        require(source && !path.empty(),E_INVALIDARG);
        const auto slash=path.find_last_of(L"\\/");
        const std::wstring directory=slash==std::wstring::npos?L".":path.substr(0,slash+1);
        if((mode&3)!=STGM_READWRITE) {
            // Match 60553619: drop our read handle before reopening for edits.
            // Another host owner may still deny writes; fail without modifying data.
            source.reset();
            using Create=HRESULT(WINAPI *)(const wchar_t *,DWORD,IStream **);
            auto host=GetModuleHandleW(L"soundcore.dll"); if(!host) host=GetModuleHandleW(nullptr);
            const auto create=reinterpret_cast<Create>(GetProcAddress(host,"CreateStreamOnFile"));
            HRESULT hr=create?create(path.c_str(),STGM_READWRITE|STGM_SHARE_DENY_WRITE,source.put()):E_NOTIMPL;
            if(FAILED(hr)) hr=SHCreateStreamOnFileEx(path.c_str(),STGM_READWRITE|STGM_SHARE_DENY_WRITE,
                                                   FILE_ATTRIBUTE_NORMAL,FALSE,nullptr,source.put());
            check(hr);
        }
        IStream *destination=source.p;
        Temporary replacement(directory),backup(directory);
        rewrite_comments(source.p,replacement.stream.p,serial,packet);
        const uint64_t old_size=stream_size(source.p);
        seek(source.p,0); copy_bytes(source.p,backup.stream.p,old_size); check(backup.stream->Commit(STGC_DEFAULT));
        auto overwrite=[&](IStream *from,uint64_t bytes) {
            seek(from,0); seek(destination,0); copy_bytes(from,destination,bytes);
            ULARGE_INTEGER size{}; size.QuadPart=bytes;
            check(destination->SetSize(size)); check(destination->Commit(STGC_DEFAULT));
        };
        const auto result=protect([&]() -> HRESULT {
            overwrite(replacement.stream.p,stream_size(replacement.stream.p)); return S_OK;
        });
        if(FAILED(result)) {
            const auto restored=protect([&]() -> HRESULT { overwrite(backup.stream.p,old_size); return S_OK; });
            if(FAILED(restored)) {
                backup.keep=true; replacement.keep=true;
                OutputDebugStringW((L"ttp_ogg: rollback failed; original backup: "+backup.name+L"\n").c_str());
            }
        }
        return result;
    });
}
} // namespace ttp::ogg
