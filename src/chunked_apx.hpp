#pragma once
#include "apx.hpp"

namespace aps {
constexpr size_t singleBlobLimit = 64 * 1024 * 1024;
constexpr size_t partSize = 16 * 1024 * 1024;
constexpr size_t batchLimit = 2 * maxBytes;

struct RemotePart {
    std::string sha;
    size_t size;
};
using RemoteParts = std::map<std::string, RemotePart>;

inline std::string partName(size_t index, size_t count) {
    return "part-" + std::to_string(index + 1) + "-of-" + std::to_string(count);
}

inline size_t chunkedSize(RemoteParts const& parts) {
    require(!parts.empty() && parts.size() <= maxBytes / partSize, "Invalid APX part count");
    size_t total = 0;
    for (size_t index = 0; index < parts.size(); ++index) {
        auto part = parts.find(partName(index, parts.size()));
        require(part != parts.end(), "Missing or misnamed APX part");
        auto size = part->second.size;
        require(size > 0 && size <= partSize, "Invalid APX part size");
        require(index + 1 == parts.size() || size == partSize, "Truncated APX part");
        total += size;
    }
    require(total >= 8, "Rebuilt APX is empty or truncated");
    return total;
}

inline std::vector<ByteView> splitAPX(ByteView bytes) {
    require(bytes.size() >= 8 && bytes.size() <= maxBytes, "Invalid APX size for upload");
    std::vector<ByteView> parts;
    for (size_t offset = 0; offset < bytes.size(); offset += partSize) {
        parts.push_back(bytes.subspan(offset, std::min(partSize, bytes.size() - offset)));
    }
    return parts;
}

inline void appendPart(Bytes& rebuilt, ByteView bytes, size_t expectedSize) {
    require(bytes.size() == expectedSize, "Downloaded APX part size mismatch");
    rebuilt.insert(rebuilt.end(), bytes.begin(), bytes.end());
}
}
