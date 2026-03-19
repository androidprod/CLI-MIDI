#include "parser/tempo_map.hpp"
#include <algorithm>

void TempoMap::add_tempo(uint64_t tick, uint32_t tempo_us) {
    // 同一 tick があれば更新、なければ挿入してソート
    for (auto& c : changes) {
        if (c.tick == tick) {
            c.tempo_us = tempo_us;
            return;
        }
    }
    changes.push_back({tick, tempo_us});
    std::sort(changes.begin(), changes.end(),
              [](const TempoChange& a, const TempoChange& b) {
                  return a.tick < b.tick;
              });
}

// 絶対 tick → 絶対マイクロ秒
// テンポ変更をまたぐ場合は区間ごとに積算
uint64_t TempoMap::tick_to_us(uint64_t target_tick) const {
    uint64_t us        = 0;
    uint64_t prev_tick = 0;
    uint32_t cur_tempo = 500000;   // デフォルト 120 BPM

    for (const auto& c : changes) {
        if (c.tick >= target_tick) break;
        uint64_t seg = c.tick - prev_tick;
        us        += (seg * cur_tempo) / tpqn;
        prev_tick  = c.tick;
        cur_tempo  = c.tempo_us;
    }
    // 最後のテンポ区間の残り
    uint64_t seg = target_tick - prev_tick;
    us += (seg * cur_tempo) / tpqn;
    return us;
}

// 指定 tick 時点現在の BPM
uint32_t TempoMap::bpm_at(uint64_t tick) const {
    uint32_t tempo = 500000;
    for (const auto& c : changes) {
        if (c.tick > tick) break;
        tempo = c.tempo_us;
    }
    return (tempo > 0) ? (60'000'000u / tempo) : 120u;
}
