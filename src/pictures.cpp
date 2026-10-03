#include "pictures.h"
#include <opusfile.h>

namespace ttp::ogg {
namespace {
constexpr char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
std::string base64(const Bytes &data) {
    std::string out; out.reserve((data.size()+2)/3*4);
    for (size_t i=0;i<data.size();i+=3) {
        const unsigned n=unsigned(data[i])<<16 | (i+1<data.size()?unsigned(data[i+1])<<8:0)
                         | (i+2<data.size()?data[i+2]:0);
        out.push_back(alphabet[n>>18]); out.push_back(alphabet[(n>>12)&63]);
        out.push_back(i+1<data.size()?alphabet[(n>>6)&63]:'=');
        out.push_back(i+2<data.size()?alphabet[n&63]:'=');
    }
    return out;
}
Bytes unbase64(const std::string &text) {
    require(text.size()%4==0 && text.size() <= (size_t(max_picture_bytes)+2)/3*4);
    Bytes out; out.reserve(text.size()/4*3);
    for(size_t i=0;i<text.size();i+=4) {
        unsigned n{}, padding{};
        for(unsigned j=0;j<4;++j) {
            const char c=text[i+j];
            if(c=='=') { require(i+4==text.size() && j>=2); ++padding; n<<=6; }
            else { const char *p=c?strchr(alphabet,c):nullptr; require(p && !padding); n=(n<<6)|unsigned(p-alphabet); }
        }
        require(padding<=2 && (!padding || (n & ((1u<<(padding*8))-1))==0));
        out.push_back(BYTE(n>>16)); if(padding<2) out.push_back(BYTE(n>>8)); if(!padding) out.push_back(BYTE(n));
    }
    require(out.size()<=max_picture_bytes); return out;
}
void be32(Bytes &out,DWORD n) { for(int i=3;i>=0;--i) out.push_back(BYTE(n>>(i*8))); }
void string32(Bytes &out,const std::string &s) {
    require(s.size()<=1024*1024,E_INVALIDARG);
    be32(out,DWORD(s.size())); out.insert(out.end(),s.begin(),s.end());
}
struct Parsed {
    OpusPictureTag value{};
    ~Parsed() { opus_picture_tag_clear(&value); }
};
Bytes block(const Picture &p,const std::string &mime,const std::string &description,
            DWORD width=0,DWORD height=0,DWORD depth=0,DWORD colors=0) {
    Bytes data; be32(data,p.type); string32(data,mime); string32(data,description);
    be32(data,width); be32(data,height); be32(data,depth); be32(data,colors); be32(data,p.bytes);
    data.insert(data.end(),p.data,p.data+p.bytes); return data;
}
Cover from_parsed(const OpusPictureTag &p,size_t index) {
    Cover cover; cover.mime=wide(p.mime_type); cover.description=wide(p.description);
    cover.type=DWORD(p.type); cover.tag_index=index;
    cover.data.assign(p.data,p.data+p.data_length); return cover;
}
} // namespace
Tag picture_tag(const Picture &p) {
    require(p.size>=sizeof(Picture) && p.data && p.bytes && p.bytes<=max_picture_bytes && p.type<=20,E_INVALIDARG);
    const std::string description=p.description?utf8(p.description):"";
    const std::string mime=p.bytes>=8 && !memcmp(p.data,"\x89PNG\r\n\x1a\n",8)?"image/png":
        p.bytes>=3 && p.data[0]==255 && p.data[1]==216 && p.data[2]==255?"image/jpeg":
        p.bytes>=6 && (!memcmp(p.data,"GIF87a",6)||!memcmp(p.data,"GIF89a",6))?"image/gif":"";
    require(!mime.empty(),E_INVALIDARG);
    auto encoded=base64(block(p,mime,description)); Parsed parsed;
    require(opus_picture_tag_parse(&parsed.value,encoded.c_str())==0,E_INVALIDARG);
    const auto &v=parsed.value;
    require(v.width && v.height && v.format!=OP_PIC_FORMAT_UNKNOWN,E_INVALIDARG);
    encoded=base64(block(p,mime,description,v.width,v.height,v.depth,v.colors));
    return {L"METADATA_BLOCK_PICTURE",wide(encoded),{},true};
}
std::vector<Cover> read_pictures(const std::vector<Tag> &tags) {
    std::vector<Cover> out;
    std::vector<size_t> legacy_mimes;
    for(size_t i=0;i<tags.size();++i) if(!_wcsicmp(tags[i].key.c_str(),L"COVERARTMIME")) legacy_mimes.push_back(i);
    size_t legacy_index{};
    for(size_t i=0;i<tags.size();++i) {
        const auto &tag=tags[i];
        if(!_wcsicmp(tag.key.c_str(),L"METADATA_BLOCK_PICTURE")) {
            if(tag.value.size()>(size_t(max_picture_bytes)+1024*1024)*4/3+8) continue;
            const auto raw=utf8(tag.value); Parsed p;
            if(opus_picture_tag_parse(&p.value,raw.c_str())!=0) continue;
            if(!p.value.data_length || p.value.data_length>max_picture_bytes || p.value.format==OP_PIC_FORMAT_URL) continue;
            out.push_back(from_parsed(p.value,i));
        } else if(!_wcsicmp(tag.key.c_str(),L"COVERART")) {
            const size_t mime_index=legacy_index<legacy_mimes.size()?legacy_mimes[legacy_index]:SIZE_MAX;
            ++legacy_index;
            try {
                auto data=unbase64(utf8(tag.value));
                Picture picture{sizeof(Picture),L"",L"",DWORD(data.size()),data.data(),3};
                const auto encoded=picture_tag(picture); Parsed p;
                if(opus_picture_tag_parse(&p.value,utf8(encoded.value).c_str())!=0) continue;
                auto cover=from_parsed(p.value,i); cover.mime_index=mime_index; out.push_back(std::move(cover));
            } catch(const Failure &) { /* Malformed artwork must not prevent audio playback. */ }
        }
    }
    // Both hosts ask for index zero for the playback cover.
    std::stable_partition(out.begin(),out.end(),[](const Cover &p){return p.type==3;});
    return out;
}
} // namespace ttp::ogg
