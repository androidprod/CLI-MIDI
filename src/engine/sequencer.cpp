#include "engine/sequencer.hpp"

#include <algorithm>
#include <chrono>
#include <thread>
#include <cstdint>

using Clock = std::chrono::high_resolution_clock;
using TP    = Clock::time_point;
using us_t  = std::chrono::microseconds;

// ─────────────────────────────────────────────
//  ctor / dtor
// ─────────────────────────────────────────────

Sequencer::Sequencer(MidiOut& midi_out) : midi_out_(midi_out) {}

Sequencer::~Sequencer() {
    quit_ = true;
    cv_.notify_all();
    if (thread_.joinable()) thread_.join();
}

// ─────────────────────────────────────────────
//  ロード
// ─────────────────────────────────────────────

void Sequencer::load(std::vector<MidiEvent> events) {
    events_ = std::move(events);

    // 総演奏時間 = 最後のイベントの abs_us
    uint64_t dur = 0;
    for (const auto& e : events_)
        if (e.abs_us > dur) dur = e.abs_us;
    state_.duration_us.store(dur);
    state_.finished.store(false);
}

// ─────────────────────────────────────────────
//  ハイブリッドスリープ (精密タイミング)
//  deadline - 2ms まで sleep_for, 残りはバジースピン
// ─────────────────────────────────────────────

void Sequencer::precise_sleep_until(TP deadline) {
    constexpr auto busy_threshold = std::chrono::milliseconds(2);
    auto now = Clock::now();
    if (deadline - now > busy_threshold)
        std::this_thread::sleep_until(deadline - busy_threshold);
    // バジースピン
    while (Clock::now() < deadline)
        std::this_thread::yield();
}

// ─────────────────────────────────────────────
//  操作 API（UIスレッドから呼び出す）
// ─────────────────────────────────────────────

void Sequencer::play() {
    if (state_.playing.load()) return;
    state_.clear_notes();
    state_.playing.store(true);
    state_.paused.store(false);
    state_.finished.store(false);
    state_.pos_us.store(0);

    quit_ = false;
    thread_ = std::thread(&Sequencer::run, this);
}

void Sequencer::pause() {
    state_.paused.store(true);
    midi_out_.all_notes_off();
}

void Sequencer::resume() {
    state_.paused.store(false);
    cv_.notify_all();
}

void Sequencer::stop() {
    state_.playing.store(false);
    state_.paused.store(false);
    quit_ = true;
    cv_.notify_all();
    if (thread_.joinable()) thread_.join();
    midi_out_.all_notes_off();
    midi_out_.reset_controllers();
    state_.clear_notes();
    state_.pos_us.store(0);
    state_.finished.store(false);
}

void Sequencer::seek(uint64_t target_us) {
    state_.seek_target_us.store(target_us);
    state_.seek_requested.store(true);
    // ポーズ中でも目覚めさせる
    cv_.notify_all();
}

void Sequencer::seek_relative(double seconds) {
    int64_t cur  = static_cast<int64_t>(state_.pos_us.load());
    int64_t delta = static_cast<int64_t>(seconds * 1'000'000.0);
    int64_t target = cur + delta;
    if (target < 0) target = 0;
    auto dur = static_cast<int64_t>(state_.duration_us.load());
    if (target > dur) target = dur;
    seek(static_cast<uint64_t>(target));
}

void Sequencer::set_tempo_scale(float scale) {
    if (scale < 0.1f) scale = 0.1f;
    if (scale > 4.0f) scale = 4.0f;
    state_.tempo_scale.store(scale);
    // シーククラッシュを避けるため、現在位置で time_base を再計算させる
    seek(state_.pos_us.load());
}

void Sequencer::set_volume(int vol_pct) {
    if (vol_pct < 0)   vol_pct = 0;
    if (vol_pct > 100) vol_pct = 100;
    state_.volume_pct.store(vol_pct);
    // CC#7 (Volume) を全チャンネルに送信
    uint8_t val = static_cast<uint8_t>(vol_pct * 127 / 100);
    for (uint8_t ch = 0; ch < 16; ++ch)
        midi_out_.send_short(0xB0u | ch, 7, val);
}

// ─────────────────────────────────────────────
//  オーディオスレッド メインループ
// ─────────────────────────────────────────────

void Sequencer::run() {
    if (events_.empty()) {
        state_.playing.store(false);
        state_.finished.store(true);
        if (on_finished) on_finished();
        return;
    }

    // 初期音量設定
    uint8_t vol = static_cast<uint8_t>(state_.volume_pct.load() * 127 / 100);
    for (uint8_t ch = 0; ch < 16; ++ch)
        midi_out_.send_short(0xB0u | ch, 7, vol);

    size_t idx = 0;
    // time_base: 「位置 0μs 時点」に対応する wall-clock 時刻
    TP time_base = Clock::now();
    state_.time_base_us.store(std::chrono::time_point_cast<std::chrono::microseconds>(time_base).time_since_epoch().count(), std::memory_order_relaxed);

    while (!quit_.load()) {
        // ────── ポーズ処理 ──────
        if (state_.paused.load()) {
            std::unique_lock<std::mutex> lk(cv_mtx_);
            cv_.wait(lk, [this] {
                return !state_.paused.load() ||
                        state_.seek_requested.load() ||
                        quit_.load();
            });
        }
        if (quit_.load()) break;

        // ────── シーク処理 ──────
        if (state_.seek_requested.load()) {
            uint64_t target = state_.seek_target_us.load();
            state_.seek_requested.store(false);

            midi_out_.all_notes_off();
            state_.clear_notes();

            auto it = std::lower_bound(
                events_.begin(), events_.end(), target,
                [](const MidiEvent& ev, uint64_t us) {
                    return ev.abs_us < us;
                });
            idx = static_cast<size_t>(it - events_.begin());

            state_.pos_us.store(target);

            float scale = state_.tempo_scale.load();
            auto scaled_us = static_cast<int64_t>(
                static_cast<double>(target) / scale);
            time_base = Clock::now() - us_t(scaled_us);
            state_.time_base_us.store(std::chrono::time_point_cast<std::chrono::microseconds>(time_base).time_since_epoch().count(), std::memory_order_relaxed);
        }

        if (idx >= events_.size()) {
            state_.playing.store(false);
            state_.finished.store(true);
            if (on_finished) on_finished();
            break;
        }

        // ────── 次のイベントの時刻を計算 ──────
        const MidiEvent& ev = events_[idx];
        float scale = state_.tempo_scale.load();
        auto event_scaled_us = static_cast<int64_t>(
            static_cast<double>(ev.abs_us) / scale);
        TP deadline = time_base + us_t(event_scaled_us);
        TP now = Clock::now();

        // 未来のイベントなら待機
        if (now < deadline) {
            constexpr auto slice = std::chrono::milliseconds(5);
            while ((now = Clock::now()) < deadline) {
                if (quit_.load() || state_.paused.load() ||
                    state_.seek_requested.load())
                    goto next_iter;
                TP next_check = now + slice;
                if (next_check > deadline) next_check = deadline;
                precise_sleep_until(next_check);
            }
        }

        // ────── 発火時刻が来ているイベントをまとめて処理（和音の高速化） ──────
        while (idx < events_.size()) {
            const MidiEvent& batch_ev = events_[idx];
            auto batch_scaled_us = static_cast<int64_t>(
                static_cast<double>(batch_ev.abs_us) / scale);
            TP batch_deadline = time_base + us_t(batch_scaled_us);
            
            if (batch_deadline > now) break; // まだ発火時刻ではない

            state_.pos_us.store(batch_ev.abs_us, std::memory_order_relaxed);

            if (batch_ev.is_sysex) {
                midi_out_.send_sysex(batch_ev.sysex_data.data(), batch_ev.sysex_data.size());
            } else if (!batch_ev.is_meta) {
                uint8_t type = batch_ev.status & 0xF0u;
                midi_out_.send_short(batch_ev.status, batch_ev.data[0], batch_ev.data[1]);
                
                if (type == 0x90u && batch_ev.data[1] > 0)
                    state_.note_channel[batch_ev.data[0] & 0x7Fu].store(batch_ev.channel + 1, std::memory_order_relaxed);
                else if (type == 0x80u || (type == 0x90u && batch_ev.data[1] == 0))
                    state_.note_channel[batch_ev.data[0] & 0x7Fu].store(0, std::memory_order_relaxed);
            }
            ++idx;
            
            if (quit_.load() || state_.paused.load() || state_.seek_requested.load())
                break;
        }

        next_iter:;
    }

    state_.playing.store(false);
}
