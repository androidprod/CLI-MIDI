#pragma once
#include <cstdint>
#include <string>
#include <vector>

// ─────────────────────────────────────────────
//  MIDI Event（パース後・テンポマップ適用後の共通構造体）
// ─────────────────────────────────────────────
struct MidiEvent {
    uint64_t abs_tick = 0;   // トラック内絶対 tick
    uint64_t abs_us   = 0;   // マイクロ秒絶対時刻（テンポマップ後に設定）
    uint8_t  status   = 0;   // ステータスバイト
    uint8_t  data[2]  = {};  // データバイト [0]=note/cc, [1]=velocity/value
    uint8_t  channel  = 0;   // チャンネル番号 (0-15)

    // メタイベント
    bool     is_meta   = false;
    uint8_t  meta_type = 0;
    std::vector<uint8_t> meta_data;

    // SysEx
    bool     is_sysex = false;
    std::vector<uint8_t> sysex_data;

    // ソート用（小さいほど優先）
    bool operator>(const MidiEvent& o) const { return abs_us > o.abs_us; }
};

// SMF ヘッダ情報
struct SmfHeader {
    uint16_t format       = 0;    // 0, 1, 2
    uint16_t num_tracks   = 0;
    uint16_t ticks_per_qn = 480;  // TPQN
};

// ─────────────────────────────────────────────
//  SMF パーサー
// ─────────────────────────────────────────────
class SmfParser {
public:
    SmfHeader                           header;
    std::vector<std::vector<MidiEvent>> tracks;   // トラック別イベント列

    void parse(const std::string& filename);

private:
    std::vector<uint8_t> buf_;
    size_t               pos_ = 0;

    // ────── プリミティブ読み取り ──────
    uint8_t  read_u8();
    uint16_t read_u16();
    uint32_t read_u32();
    uint32_t read_vlq();           // Variable-Length Quantity
    void     skip(size_t n);
    void     expect_tag(const char* tag);

    // ────── チャンク解析 ──────
    void parse_header_chunk();
    void parse_track_chunk(std::vector<MidiEvent>& events);
};
