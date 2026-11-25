// Crit Launcher Native — raw DEFLATE inflate + minimal ZIP reader (natives, jars).
#pragma once

#include <map>

#include "core/base.h"

namespace crit {

// zlib stream ("inflate") and raw deflate streams. Returns false on corrupt data.
bool InflateZlib(const uint8_t* in, size_t inLen, std::vector<uint8_t>& out);
bool InflateRaw(const uint8_t* in, size_t inLen, std::vector<uint8_t>& out);

struct ZipEntry {
    string name;
    uint16_t method = 0;
    uint32_t compressedSize = 0;
    uint32_t uncompressedSize = 0;
    uint32_t localOffset = 0;
};

// reads a zip file lazily; entries can be extracted on demand
class ZipArchive {
public:
    bool Open(const string& path);
    bool Read(const string& entryName, std::vector<uint8_t>& out);
    bool Has(const string& entryName) const;
    const std::vector<ZipEntry>& Entries() const { return entries_; }

private:
    std::vector<uint8_t> data_;
    std::map<string, size_t> index_;
    std::vector<ZipEntry> entries_;
};

}  // namespace crit
