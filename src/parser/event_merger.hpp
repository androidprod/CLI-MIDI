#pragma once
#include "smf_parser.hpp"
#include <vector>
#include <queue>
#include <functional>

// ─────────────────────────────────────────────
//  全トラックの MidiEvent を abs_us 順にマージ
//  シーク用にランダムアクセス可能な vector で返す
// ─────────────────────────────────────────────
inline std::vector<MidiEvent> merge_tracks(
    const std::vector<std::vector<MidiEvent>>& tracks)
{
    // (abs_us, track_index, event_index)
    struct Item {
        uint64_t us;
        size_t   track;
        size_t   idx;
        bool operator>(const Item& o) const { return us > o.us; }
    };

    std::priority_queue<Item, std::vector<Item>, std::greater<Item>> pq;
    for (size_t t = 0; t < tracks.size(); ++t) {
        if (!tracks[t].empty())
            pq.push({tracks[t][0].abs_us, t, 0});
    }

    std::vector<MidiEvent> merged;
    merged.reserve(8192);

    while (!pq.empty()) {
        auto item = pq.top(); pq.pop();
        merged.push_back(tracks[item.track][item.idx]);
        size_t next_idx = item.idx + 1;
        if (next_idx < tracks[item.track].size())
            pq.push({tracks[item.track][next_idx].abs_us, item.track, next_idx});
    }
    return merged;
}
