#pragma once
#include "parser/smf_parser.hpp"
#include "engine/midi_out.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>
#include <functional>

// ─────────────────────────────────────────────
//  プレイヤー共有状態（スレッド間 atomic 共有）
// ─────────────────────────────────────────────
struct PlayerState {
    std::atomic<bool>     playing   {false};
    std::atomic<bool>     paused    {false};
    std::atomic<bool>     finished  {false};
    std::atomic<uint64_t> pos_us    {0};       // 現在再生位置(μs)
    std::atomic<uint64_t> duration_us{0};      // 総時間(μs)
    std::atomic<float>    tempo_scale{1.0f};   // テンポ倍率
    std::atomic<int>      volume_pct {80};      // 音量 0-100 (Master volume CC7)
    std::atomic<bool>     seek_requested{false};
    std::atomic<uint64_t> seek_target_us{0};
    
    // UIの滑らかな描画(ウォーターフォール)用
    std::atomic<int64_t>  time_base_us{0};

    // [note 0-127] = channel+1 (0=off) — TUI ピアノロール用
    std::atomic<uint8_t>  note_channel[128] = {};

    void clear_notes() {
        for (auto& n : note_channel) n.store(0, std::memory_order_relaxed);
    }
};

// ─────────────────────────────────────────────
//  シーケンサー（オーディオスレッドで動作）
// ─────────────────────────────────────────────
class Sequencer {
public:
    explicit Sequencer(MidiOut& midi_out);
    ~Sequencer();

    // MIDI イベント列をロード（merge_tracks 後の sorted vector）
    void load(std::vector<MidiEvent> events);

    void play();
    void pause();
    void resume();
    void stop();

    // μs 単位でシーク（再生中でも動作）
    void seek(uint64_t target_us);

    // +/- 方向にシーク (seconds)
    void seek_relative(double seconds);

    // テンポ倍率変更
    void set_tempo_scale(float scale);

    // 音量 (0-100)
    void set_volume(int vol_pct);

    PlayerState& state() { return state_; }
    const std::vector<MidiEvent>& get_events() const { return events_; }

    // 再生完了コールバック
    std::function<void()> on_finished;

private:
    void run();    // オーディオスレッドのメインループ

    // high_resolution_clock を使った精密スリープ
    // deadline までバジースピン + sleep_for のハイブリッド
    using Clock = std::chrono::high_resolution_clock;
    using TP    = Clock::time_point;
    void precise_sleep_until(TP deadline);

    std::vector<MidiEvent> events_;
    MidiOut&               midi_out_;
    PlayerState            state_;

    std::thread             thread_;
    std::mutex              cv_mtx_;
    std::condition_variable cv_;

    std::atomic<bool> quit_ {false};
};
