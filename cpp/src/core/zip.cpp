// Crit Launcher Native — DEFLATE (RFC 1951) decoder + ZIP directory reader.
#include "core/zip.h"

#include "platform/platform.h"

namespace crit {

namespace {

struct Huffman {
    uint16_t counts[16] = {0};
    std::vector<uint16_t> symbols;

    void Build(const uint8_t* lengths, int n) {
        for (int i = 0; i < 16; i++) counts[i] = 0;
        for (int i = 0; i < n; i++) counts[lengths[i]]++;
        counts[0] = 0;
        uint16_t offs[16] = {0};
        for (int i = 1; i < 16; i++) offs[i] = (uint16_t)(offs[i - 1] + counts[i - 1]);
        symbols.assign((size_t)(offs[15] + counts[15]), 0);
        for (int i = 0; i < n; i++)
            if (lengths[i]) symbols[offs[lengths[i]]++] = (uint16_t)i;
    }
};

struct BitReader {
    const uint8_t* data;
    size_t size;
    size_t pos = 0;
    uint32_t bitBuf = 0;
    int bitCount = 0;

    bool Need(int bits) {
        while (bitCount < bits) {
            if (pos >= size) return false;
            bitBuf |= (uint32_t)data[pos++] << bitCount;
            bitCount += 8;
        }
        return true;
    }
    int Bits(int n) {
        if (n == 0) return 0;
        if (!Need(n)) return -1;
        int v = (int)(bitBuf & ((1u << n) - 1));
        bitBuf >>= n;
        bitCount -= n;
        return v;
    }
    int Decode(const Huffman& h) {
        int code = 0, first = 0, index = 0;
        for (int len = 1; len < 16; len++) {
            int b = Bits(1);
            if (b < 0) return -1;
            code |= b;
            int count = h.counts[len];
            if (code - first < count) return h.symbols[(size_t)(index + code - first)];
            index += count;
            first = (first + count) << 1;
            code <<= 1;
        }
        return -1;
    }
};

const uint16_t kLenBase[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
                               35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
const uint8_t kLenExtra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
const uint16_t kDistBase[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
                                257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
const uint8_t kDistExtra[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

bool InflateBlock(BitReader& br, std::vector<uint8_t>& out) {
    static Huffman fixedLit, fixedDist;
    static bool fixedInit = false;
    if (!fixedInit) {
        uint8_t litLen[288];
        int i = 0;
        for (; i < 144; i++) litLen[i] = 8;
        for (; i < 256; i++) litLen[i] = 9;
        for (; i < 280; i++) litLen[i] = 7;
        for (; i < 288; i++) litLen[i] = 8;
        fixedLit.Build(litLen, 288);
        uint8_t distLen[30];
        for (i = 0; i < 30; i++) distLen[i] = 5;
        fixedDist.Build(distLen, 30);
        fixedInit = true;
    }
    for (;;) {
        int final = br.Bits(1);
        if (final < 0) return false;
        int type = br.Bits(2);
        if (type < 0) return false;
        if (type == 0) {
            // stored
            br.bitBuf = 0;
            br.bitCount = 0;
            if (br.pos + 4 > br.size) return false;
            uint16_t len = (uint16_t)(br.data[br.pos] | (br.data[br.pos + 1] << 8));
            br.pos += 4;
            if (br.pos + len > br.size) return false;
            out.insert(out.end(), br.data + br.pos, br.data + br.pos + len);
            br.pos += len;
            if (final) return true;
            continue;
        }
        Huffman lit, dist;
        if (type == 1) {
            lit = fixedLit;
            dist = fixedDist;
        } else if (type == 2) {
            int hlit = br.Bits(5);
            int hdist = br.Bits(5);
            int hclen = br.Bits(4);
            if (hlit < 0 || hdist < 0 || hclen < 0) return false;
            hlit += 257;
            hdist += 1;
            hclen += 4;
            static const uint8_t order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
            uint8_t cls[19] = {0};
            for (int i = 0; i < hclen; i++) {
                int v = br.Bits(3);
                if (v < 0) return false;
                cls[order[i]] = (uint8_t)v;
            }
            Huffman codeLen;
            codeLen.Build(cls, 19);
            std::vector<uint8_t> lengths((size_t)(hlit + hdist), 0);
            int idx = 0;
            while (idx < hlit + hdist) {
                int sym = br.Decode(codeLen);
                if (sym < 0) return false;
                if (sym < 16) {
                    lengths[(size_t)idx++] = (uint8_t)sym;
                } else if (sym == 16) {
                    int rep = br.Bits(2);
                    if (rep < 0 || idx == 0) return false;
                    uint8_t prev = lengths[(size_t)(idx - 1)];
                    for (int k = 0; k < rep + 3 && idx < hlit + hdist; k++) lengths[(size_t)idx++] = prev;
                } else if (sym == 17) {
                    int rep = br.Bits(3);
                    if (rep < 0) return false;
                    for (int k = 0; k < rep + 3 && idx < hlit + hdist; k++) lengths[(size_t)idx++] = 0;
                } else if (sym == 18) {
                    int rep = br.Bits(7);
                    if (rep < 0) return false;
                    for (int k = 0; k < rep + 11 && idx < hlit + hdist; k++) lengths[(size_t)idx++] = 0;
                } else {
                    return false;
                }
            }
            lit.Build(lengths.data(), hlit);
            dist.Build(lengths.data() + hlit, hdist);
        } else {
            return false;
        }
        for (;;) {
            int sym = br.Decode(lit);
            if (sym < 0) return false;
            if (sym < 256) {
                out.push_back((uint8_t)sym);
            } else if (sym == 256) {
                break;
            } else {
                sym -= 257;
                if (sym >= 29) return false;
                int len = kLenBase[sym] + br.Bits(kLenExtra[sym]);
                int dsym = br.Decode(dist);
                if (dsym < 0 || dsym >= 30) return false;
                int d = kDistBase[dsym] + br.Bits(kDistExtra[dsym]);
                if ((size_t)d > out.size()) return false;
                size_t start = out.size() - (size_t)d;
                for (int k = 0; k < len; k++) out.push_back(out[start + (size_t)k]);
            }
        }
        if (final) return true;
    }
}

}  // namespace

bool InflateRaw(const uint8_t* in, size_t inLen, std::vector<uint8_t>& out) {
    BitReader br{in, inLen};
    return InflateBlock(br, out);
}

bool InflateZlib(const uint8_t* in, size_t inLen, std::vector<uint8_t>& out) {
    if (inLen < 2) return false;
    uint8_t cmf = in[0], flg = in[1];
    if ((cmf & 0x0f) != 8) return false;
    if (((uint32_t)cmf * 256 + flg) % 31 != 0) return false;
    if (flg & 0x20) return false;  // preset dictionary unsupported
    return InflateRaw(in + 2, inLen - 2, out);
}

// ---------------------------------------------------------------- zip
static uint32_t Rd32(const uint8_t* p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
static uint16_t Rd16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }

bool ZipArchive::Open(const string& path) {
    data_ = ReadFileBytes(path);
    if (data_.size() < 22) return false;
    // find EOCD
    size_t eocd = string::npos;
    for (size_t i = data_.size() - 22; i + 4 <= data_.size(); i--) {
        if (Rd32(&data_[i]) == 0x06054b50) {
            eocd = i;
            break;
        }
        if (i == 0) break;
    }
    if (eocd == string::npos) return false;
    uint16_t count = Rd16(&data_[eocd + 10]);
    uint32_t cdOffset = Rd32(&data_[eocd + 16]);
    size_t pos = cdOffset;
    for (uint16_t i = 0; i < count && pos + 46 <= data_.size(); i++) {
        if (Rd32(&data_[pos]) != 0x02014b50) break;
        ZipEntry e;
        e.method = Rd16(&data_[pos + 10]);
        e.compressedSize = Rd32(&data_[pos + 20]);
        e.uncompressedSize = Rd32(&data_[pos + 24]);
        uint16_t nameLen = Rd16(&data_[pos + 28]);
        uint16_t extraLen = Rd16(&data_[pos + 30]);
        uint16_t commentLen = Rd16(&data_[pos + 32]);
        e.localOffset = Rd32(&data_[pos + 42]);
        e.name.assign((const char*)&data_[pos + 46], nameLen);
        index_[e.name] = entries_.size();
        entries_.push_back(e);
        pos += 46 + nameLen + extraLen + commentLen;
    }
    return !entries_.empty();
}

bool ZipArchive::Has(const string& entryName) const { return index_.count(entryName) > 0; }

bool ZipArchive::Read(const string& entryName, std::vector<uint8_t>& out) {
    auto it = index_.find(entryName);
    if (it == index_.end()) return false;
    const ZipEntry& e = entries_[it->second];
    size_t p = e.localOffset;
    if (p + 30 > data_.size() || Rd32(&data_[p]) != 0x04034b50) return false;
    uint16_t nameLen = Rd16(&data_[p + 26]);
    uint16_t extraLen = Rd16(&data_[p + 28]);
    size_t start = p + 30 + nameLen + extraLen;
    if (start + e.compressedSize > data_.size()) return false;
    if (e.method == 0) {
        out.assign(data_.begin() + start, data_.begin() + start + e.compressedSize);
        return true;
    }
    if (e.method == 8) return InflateRaw(&data_[start], e.compressedSize, out);
    return false;
}

}  // namespace crit
