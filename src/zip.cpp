// zip.cpp - ZIP container + raw DEFLATE decoder (RFC 1951), canonical Huffman.
// Based on Vortex Minecraft Launcher by Kron4ek (PureBasic, GPL-3.0):
// https://github.com/Kron4ek/minecraft-vortex-launcher - see LICENSE at the repo root.
#include <cstdio>
#include <cstring>
#include <vector>
#include <string>
#include "zip.hpp"
#include "util.hpp"

namespace {

struct BitReader {
    const unsigned char* p;
    size_t n, pos = 0;
    unsigned bitcnt = 0, acc = 0;
    BitReader(const unsigned char* p_, size_t n_) : p(p_), n(n_) {}
    bool getBit(unsigned& b) {
        if (bitcnt == 0) {
            if (pos >= n) return false;
            acc = p[pos++]; bitcnt = 8;
        }
        b = acc & 1; acc >>= 1; --bitcnt;
        return true;
    }
    bool getBits(unsigned count, unsigned& v) {
        v = 0;
        for (unsigned k = 0; k < count; ++k) {
            unsigned b;
            if (!getBit(b)) return false;
            v |= b << k;
        }
        return true;
    }
};

// canonical Huffman table from code lengths
struct Huff {
    std::vector<unsigned char> lengths;
    unsigned counts[16] = {0};
    unsigned offsets[16] = {0};
    std::vector<unsigned short> syms;
    Huff() = default;
    explicit Huff(std::vector<unsigned char> l) : lengths(std::move(l)) {
        memset(counts, 0, sizeof counts); memset(offsets, 0, sizeof offsets);
        for (size_t s = 0; s < lengths.size(); ++s) if (lengths[s]) counts[lengths[s]]++;
        unsigned off = 0;
        for (unsigned L = 1; L < 16; ++L) { offsets[L] = off; off += counts[L]; }
        syms.resize(lengths.size());
        std::vector<unsigned> next(offsets, offsets + 16);
        for (unsigned s = 0; s < lengths.size(); ++s)
            if (lengths[s]) syms[next[lengths[s]]++] = (unsigned short)s;
    }
    bool decode(BitReader& br, unsigned& sym) {
        unsigned code = 0, first = 0, index = 0;
        for (unsigned L = 1; L <= 15; ++L) {
            unsigned b;
            if (!br.getBit(b)) return false;
            code |= b << (L - 1);
            unsigned d = code - first;
            if (d < counts[L]) { sym = syms[offsets[L] + d]; return true; }
            first += counts[L];
            index += counts[L];
        }
        return false;
    }
};

bool inflateRaw(const unsigned char* src, size_t n, std::vector<unsigned char>& out) {
    static const unsigned lbase[29] = {3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258};
    static const unsigned lextra[29] = {0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0};
    static const unsigned dbase[30] = {1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577};
    static const unsigned dextra[30] = {0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13};
    BitReader br(src, n);
    for (;;) {
        unsigned last = 0, type = 0;
        if (!br.getBit(last)) return false;
        if (!br.getBits(2, type)) return false;
        if (type == 0) {
            if (br.bitcnt) { br.acc = 0; br.bitcnt = 0; } // byte-align
            if (br.pos + 4 > n) return false;
            unsigned len = br.p[br.pos] | (br.p[br.pos+1] << 8);
            br.pos += 4; // skip len + nlen (stored blocks: nlen check omitted like most lenient decoders)
            if (br.pos + len > n) return false;
            out.insert(out.end(), br.p + br.pos, br.p + br.pos + len);
            br.pos += len;
        } else if (type == 1 || type == 2) {
            Huff lit, dist;
            if (type == 1) {
                std::vector<unsigned char> l(288);
                for (int i = 0; i < 144; ++i) l[i] = 8;
                for (int i = 144; i < 256; ++i) l[i] = 9;
                for (int i = 256; i < 280; ++i) l[i] = 7;
                for (int i = 280; i < 288; ++i) l[i] = 8;
                lit = Huff(l);
                dist = Huff(std::vector<unsigned char>(30, 5));
            } else {
                unsigned hlit = 0, hdist = 0, hclen = 0;
                if (!br.getBits(5, hlit) || !br.getBits(5, hdist) || !br.getBits(4, hclen)) return false;
                hlit += 257; hdist += 1; hclen += 4;
                static const unsigned order[19] = {16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15};
                unsigned clen[19] = {0};
                for (unsigned i = 0; i < hclen; ++i) { unsigned v; if (!br.getBits(3, v)) return false; clen[order[i]] = v; }
                Huff codeLenHuff(std::vector<unsigned char>(clen, clen + 19));
                std::vector<unsigned char> lengths(hlit + hdist, 0);
                size_t k = 0;
                while (k < lengths.size()) {
                    unsigned sym;
                    if (!codeLenHuff.decode(br, sym)) return false;
                    if (sym < 16) lengths[k++] = (unsigned char)sym;
                    else if (sym == 16) {
                        if (k == 0) return false;
                        unsigned rep; if (!br.getBits(2, rep)) return false; rep += 3;
                        unsigned prev = lengths[k - 1];
                        while (rep-- && k < lengths.size()) lengths[k++] = (unsigned char)prev;
                    } else if (sym == 17) {
                        unsigned rep; if (!br.getBits(3, rep)) return false; rep += 3;
                        while (rep-- && k < lengths.size()) lengths[k++] = 0;
                    } else {
                        unsigned rep; if (!br.getBits(7, rep)) return false; rep += 11;
                        while (rep-- && k < lengths.size()) lengths[k++] = 0;
                    }
                }
                lit = Huff(std::vector<unsigned char>(lengths.begin(), lengths.begin() + hlit));
                dist = Huff(std::vector<unsigned char>(lengths.begin() + hlit, lengths.end()));
            }
            for (;;) {
                unsigned sym;
                if (!lit.decode(br, sym)) return false;
                if (sym < 256) { out.push_back((unsigned char)sym); continue; }
                if (sym == 256) break;
                sym -= 257;
                if (sym >= 29) return false;
                unsigned extra = 0;
                if (!br.getBits(lextra[sym], extra)) return false;
                unsigned len = lbase[sym] + extra;
                unsigned dsym;
                if (!dist.decode(br, dsym)) return false;
                if (dsym >= 30) return false;
                unsigned dextraBits = 0;
                if (!br.getBits(dextra[dsym], dextraBits)) return false;
                size_t d = dbase[dsym] + dextraBits;
                if (d > out.size()) return false;
                size_t from = out.size() - d;
                for (unsigned i = 0; i < len; ++i) out.push_back(out[from + i]);
            }
        } else return false;
        if (last) break;
    }
    return true;
}

inline unsigned short rd16(const unsigned char* p) { return (unsigned short)(p[0] | (p[1] << 8)); }
inline unsigned  rd32(const unsigned char* p) { return (unsigned)(p[0] | (p[1] << 8) | (p[2] << 16) | ((unsigned)p[3] << 24)); }

// Locate End Of Central Directory in the last 64 KB.
bool findEOCD(const std::vector<unsigned char>& buf, size_t& eocdOff) {
    if (buf.size() < 22) return false;
    size_t start = buf.size() > 66000 ? buf.size() - 66000 : 0;
    for (size_t i = buf.size() - 22; ; --i) {
        if (rd32(&buf[i]) == 0x06054b50) { eocdOff = i; return true; }
        if (i == start) return false;
    }
}

bool openArchive(const std::string& zipPath, std::vector<unsigned char>& buf, size_t& cdOff, size_t& cdCount) {
    std::string data;
    if (!readFileAll(zipPath, data)) return false;
    buf.assign(data.begin(), data.end());
    size_t eocd;
    if (!findEOCD(buf, eocd)) return false;
    cdCount = rd16(&buf[eocd + 10]);
    cdOff = rd32(&buf[eocd + 16]);
    return cdOff < buf.size();
}

// Walk central directory; if name matches, decompress into outData.
bool centralFind(const std::vector<unsigned char>& buf, size_t cdOff, size_t cdCount,
                 const std::string& name, std::vector<unsigned char>& outData) {
    size_t p = cdOff;
    for (size_t i = 0; i < cdCount; ++i) {
        if (p + 46 > buf.size() || rd32(&buf[p]) != 0x02014b50) return false;
        unsigned method = rd16(&buf[p + 10]);
        unsigned csize = rd32(&buf[p + 20]);
        unsigned nlen = rd16(&buf[p + 28]);
        unsigned elen = rd16(&buf[p + 30]);
        unsigned clen = rd16(&buf[p + 32]);
        size_t loff = rd32(&buf[p + 42]);
        std::string entry((const char*)&buf[p + 46], nlen);
        p += 46 + nlen + elen + clen;
        if (entry != name) continue;
        if (loff + 30 > buf.size() || rd32(&buf[loff]) != 0x04034b50) return false;
        unsigned lnlen = rd16(&buf[loff + 26]);
        unsigned lelen = rd16(&buf[loff + 28]);
        size_t doff = loff + 30 + lnlen + lelen;
        if (doff + csize > buf.size()) return false;
        if (method == 0) { outData.assign(buf.begin() + doff, buf.begin() + doff + csize); return true; }
        if (method == 8) return inflateRaw(&buf[doff], csize, outData);
        return false; // unsupported method (natives jars are deflated/stored)
    }
    return false;
}

} // namespace

bool zipExtractFile(const std::string& zipPath, const std::string& entryName, const std::string& destPath) {    std::vector<unsigned char> buf;
    size_t cdOff = 0, cdCount = 0;
    if (!openArchive(zipPath, buf, cdOff, cdCount)) return false;
    std::vector<unsigned char> data;
    if (!centralFind(buf, cdOff, cdCount, entryName, data)) return false;
    FILE* f = _wfopen(u2w(destPath).c_str(), L"wb");
    if (!f) return false;
    bool ok = data.empty() || fwrite(data.data(), 1, data.size(), f) == data.size();
    fclose(f);
    return ok;
}

std::vector<std::string> zipList(const std::string& zipPath) {
    std::vector<std::string> out;
    std::vector<unsigned char> buf;
    size_t cdOff = 0, cdCount = 0;
    if (!openArchive(zipPath, buf, cdOff, cdCount)) return out;
    size_t p = cdOff;
    for (size_t i = 0; i < cdCount; ++i) {
        if (p + 46 > buf.size() || rd32(&buf[p]) != 0x02014b50) break;
        unsigned nlen = rd16(&buf[p + 28]);
        unsigned elen = rd16(&buf[p + 30]);
        unsigned clen = rd16(&buf[p + 32]);
        out.emplace_back((const char*)&buf[p + 46], nlen);
        p += 46 + nlen + elen + clen;
    }
    return out;
}

// Decompress the entry whose central header sits at p into outData.
namespace {
bool centralEntryData(const std::vector<unsigned char>& buf, size_t p,
                      std::vector<unsigned char>& outData) {
    unsigned method = rd16(&buf[p + 10]);
    unsigned csize = rd32(&buf[p + 20]);
    size_t loff = rd32(&buf[p + 42]);
    if (loff + 30 > buf.size() || rd32(&buf[loff]) != 0x04034b50) return false;
    unsigned lnlen = rd16(&buf[loff + 26]);
    unsigned lelen = rd16(&buf[loff + 28]);
    size_t doff = loff + 30 + lnlen + lelen;
    if (doff + csize > buf.size()) return false;
    if (method == 0) { outData.assign(buf.begin() + doff, buf.begin() + doff + csize); return true; }
    if (method == 8) return inflateRaw(&buf[doff], csize, outData);
    return false;
}
} // namespace

bool zipOpenArchive(const std::string& zipPath, std::vector<unsigned char>& buf,
                    size_t& cdOff, size_t& cdCount) {
    return openArchive(zipPath, buf, cdOff, cdCount);
}

// PB OpenPack/ExaminePack/ExtractPackFile loop (parseLibraries natives branch).
bool zipExtractMissing(const std::vector<unsigned char>& buf, size_t cdOff, size_t cdCount,
                       const std::string& destDir) {
    bool any = false;
    size_t p = cdOff;
    for (size_t i = 0; i < cdCount; ++i) {
        if (p + 46 > buf.size() || rd32(&buf[p]) != 0x02014b50) break;
        size_t hdr = p;
        unsigned nlen = rd16(&buf[p + 28]);
        unsigned elen = rd16(&buf[p + 30]);
        unsigned clen = rd16(&buf[p + 32]);
        std::string name((const char*)&buf[p + 46], nlen);
        p += 46 + nlen + elen + clen;
        if (name.empty()) continue;
        bool isDir = name.back() == '/' || name.back() == '\\';
        for (auto& c : name) if (c == '/') c = '\\';          // pack paths use '/'; Win32 wants '\'
        while (!name.empty() && name.back() == '\\') name.pop_back();
        if (name.empty()) continue;
        std::string dest = destDir + "\\" + name;
        if (isDir) {                                          // stored directory entry
            createDirectoryRecursive(dest);
            continue;
        }
        if (fileSize(dest) >= 1) continue;                    // PB: If FileSize(path) < 1
        createDirectoryRecursive(getPathPart(dest));
        std::vector<unsigned char> data;
        if (!centralEntryData(buf, hdr, data)) continue;
        FILE* f = _wfopen(u2w(dest).c_str(), L"wb");
        if (!f) continue;
        if (data.empty() || fwrite(data.data(), 1, data.size(), f) == data.size()) any = true;
        fclose(f);
    }
    return any;
}
