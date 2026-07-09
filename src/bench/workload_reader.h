#pragma once
// workload_reader.h — load the generator's binary workload (the same .bin the
// benchmark harness replays) into a flat vector of decoded messages. Header-only,
// engine-agnostic: the four standalone micro-benchmarks share this so their
// numbers describe the exact same message stream as the harness.
//
// On-disk format (see benchmark/workload/generator.cpp):
//   header : [u64 magic][u32 version][u32 count]                    (16 bytes)
//   record : [u8 type][u8 side][u8 ioc][u8 pad][u32 qty]
//            [u64 sequence_number][u64 order_id][i64 price][i64 reserved]  (40 bytes)

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace bench {

// One decoded workload message. For a NEW, price/qty are the order's; for a
// MODIFY they are the new price/qty; for a CANCEL they echo the original.
struct WMsg {
    std::uint8_t  type;       // 0 = new, 1 = cancel, 2 = modify
    std::uint8_t  side;       // 0 = buy, 1 = sell
    std::uint8_t  ioc;        // 1 if immediate-or-cancel (new orders only)
    std::uint32_t qty;
    std::uint64_t seq;
    std::uint64_t order_id;   // 1-based, dense
    std::int64_t  price;      // ticks
};

inline bool load_workload(const std::string& path, std::vector<WMsg>& out) {
    constexpr std::uint64_t MAGIC = 0x4D4542575F303031ULL;  // "MEBW_001"

    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) { std::perror("fopen"); return false; }

    std::uint64_t magic = 0;
    std::uint32_t ver = 0, count = 0;
    bool ok = std::fread(&magic, 8, 1, f) == 1
           && std::fread(&ver,   4, 1, f) == 1
           && std::fread(&count, 4, 1, f) == 1;
    if (!ok || magic != MAGIC) {
        std::fprintf(stderr, "bad workload header in %s\n", path.c_str());
        std::fclose(f);
        return false;
    }

    out.clear();
    out.reserve(count);
    for (std::uint32_t i = 0; ok && i < count; ++i) {
        std::uint8_t  hdr[4];
        std::uint32_t qty;
        std::uint64_t seq, oid;
        std::int64_t  price, reserved;
        ok = std::fread(hdr,       4, 1, f) == 1
          && std::fread(&qty,      4, 1, f) == 1
          && std::fread(&seq,      8, 1, f) == 1
          && std::fread(&oid,      8, 1, f) == 1
          && std::fread(&price,    8, 1, f) == 1
          && std::fread(&reserved, 8, 1, f) == 1;
        if (ok) out.push_back({ hdr[0], hdr[1], hdr[2], qty, seq, oid, price });
    }
    std::fclose(f);
    if (!ok) std::fprintf(stderr, "short read from %s\n", path.c_str());
    return ok;
}

}  // namespace bench
