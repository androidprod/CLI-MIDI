#pragma once
#include <cstdint>
#include <vector>

// テンポ変更エントリ
struct TempoChange {
    uint64_t tick;
    uint32_t tempo_us;   // 1拍あたりのマイクロ秒（500000 = 120BPM）
};

// ─────────────────────────────────────────────
//  テンポマップ — tick → μs 変換
// ─────────────────────────────────────────────
class TempoMap {
public:
    uint16_t                 tpqn;
    std::vector<TempoChange> changes;

    explicit TempoMap(uint16_t tpqn_) : tpqn(tpqn_) {
        changes.push_back({0, 500000});  // デフォルト 120BPM
    }

    // テンポ変更を登録（内部ソート済み）
    void add_tempo(uint64_t tick, uint32_t tempo_us);

    // 絶対 tick → 絶対マイクロ秒
    uint64_t tick_to_us(uint64_t tick) const;

    // 指定 tick 時点の BPM
    uint32_t bpm_at(uint64_t tick) const;
};
