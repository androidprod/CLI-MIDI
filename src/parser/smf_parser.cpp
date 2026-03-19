#include "parser/smf_parser.hpp"

#include <fstream>
#include <stdexcept>
#include <cstring>
#include <algorithm>
#include <string>

// ────────────────────────────────────────────
//  バイト単位プリミティブ（ARM/big-endian 安全）
// ────────────────────────────────────────────

uint8_t SmfParser::read_u8() {
    if (pos_ >= buf_.size())
        throw std::runtime_error("SMF: unexpected end of data");
    return buf_[pos_++];
}

uint16_t SmfParser::read_u16() {
    uint8_t hi = read_u8();
    uint8_t lo = read_u8();
    return static_cast<uint16_t>((hi << 8) | lo);
}

uint32_t SmfParser::read_u32() {
    uint8_t b0 = read_u8(), b1 = read_u8(),
            b2 = read_u8(), b3 = read_u8();
    return (static_cast<uint32_t>(b0) << 24) |
           (static_cast<uint32_t>(b1) << 16) |
           (static_cast<uint32_t>(b2) <<  8) |
            static_cast<uint32_t>(b3);
}

uint32_t SmfParser::read_vlq() {
    uint32_t value = 0;
    for (int i = 0; i < 4; ++i) {
        uint8_t b = read_u8();
        value = (value << 7) | (b & 0x7Fu);
        if (!(b & 0x80u)) break;
    }
    return value;
}

void SmfParser::skip(size_t n) {
    if (pos_ + n > buf_.size())
        throw std::runtime_error("SMF: skip out of range");
    pos_ += n;
}

void SmfParser::expect_tag(const char* tag) {
    for (int i = 0; i < 4; ++i) {
        uint8_t b = read_u8();
        if (b != static_cast<uint8_t>(tag[i]))
            throw std::runtime_error(
                std::string("SMF: expected tag '") + tag + "' not found");
    }
}

// ────────────────────────────────────────────
//  ヘッダチャンク
// ────────────────────────────────────────────

void SmfParser::parse_header_chunk() {
    expect_tag("MThd");
    uint32_t len = read_u32();
    if (len < 6)
        throw std::runtime_error("SMF: header chunk too short");

    header.format     = read_u16();
    header.num_tracks = read_u16();

    uint16_t time_div = read_u16();
    if (time_div & 0x8000u) {
        // SMPTE タイムコード — 標準 TPQN でフォールバック
        header.ticks_per_qn = 480;
    } else {
        header.ticks_per_qn = time_div == 0 ? 480 : time_div;
    }

    // 余分なヘッダバイトをスキップ
    if (len > 6) skip(len - 6);
}

// ────────────────────────────────────────────
//  トラックチャンク
// ────────────────────────────────────────────

void SmfParser::parse_track_chunk(std::vector<MidiEvent>& events) {
    // MTrk チャンクまで読み飛ばし（不明チャンクは飛ばす）
    while (pos_ + 8 <= buf_.size()) {
        char    tag_buf[5] = {0};
        for (int i = 0; i < 4; ++i)
            tag_buf[i] = static_cast<char>(read_u8());
        uint32_t chunk_len = read_u32();

        if (std::strcmp(tag_buf, "MTrk") != 0) {
            skip(chunk_len);
            continue;
        }

        size_t   chunk_end = pos_ + chunk_len;
        uint64_t abs_tick  = 0;
        uint8_t  running   = 0;   // ランニングステータス

        while (pos_ < chunk_end) {
            uint32_t delta = read_vlq();
            abs_tick += delta;

            uint8_t first = read_u8();

            // ────── メタイベント ──────
            if (first == 0xFFu) {
                uint8_t  meta_type = read_u8();
                uint32_t meta_len  = read_vlq();

                MidiEvent ev;
                ev.abs_tick  = abs_tick;
                ev.is_meta   = true;
                ev.meta_type = meta_type;
                ev.meta_data.resize(meta_len);
                for (uint32_t i = 0; i < meta_len; ++i)
                    ev.meta_data[i] = read_u8();

                running = 0;   // メタはランニングステータスをリセット
                events.push_back(std::move(ev));
                continue;
            }

            // ────── SysEx ──────
            if (first == 0xF0u || first == 0xF7u) {
                uint32_t slen = read_vlq();
                MidiEvent ev;
                ev.abs_tick  = abs_tick;
                ev.is_sysex  = true;
                ev.status    = first;
                ev.sysex_data.resize(slen);
                for (uint32_t i = 0; i < slen; ++i)
                    ev.sysex_data[i] = read_u8();

                running = 0;
                events.push_back(std::move(ev));
                continue;
            }

            // ────── チャンネルメッセージ ──────
            uint8_t status;
            uint8_t d0;

            if (first & 0x80u) {
                status  = first;
                running = first;
                d0      = read_u8();
            } else {
                // ランニングステータス適用
                if (running == 0)
                    throw std::runtime_error("SMF: invalid running status");
                status = running;
                d0     = first;
            }

            MidiEvent ev;
            ev.abs_tick = abs_tick;
            ev.status   = status;
            ev.channel  = status & 0x0Fu;
            ev.data[0]  = d0;

            uint8_t type = status & 0xF0u;
            // 2バイトメッセージ
            if (type == 0x80u || type == 0x90u || type == 0xA0u ||
                type == 0xB0u || type == 0xE0u) {
                ev.data[1] = read_u8();
            }
            // 1バイト: 0xC0 Program Change / 0xD0 Channel Pressure

            events.push_back(std::move(ev));
        }

        pos_ = chunk_end;   // 念のため正確な位置へ
        return;
    }
    // MTrk が見つからなかった場合は空のままにする
}

// ────────────────────────────────────────────
//  メインエントリ
// ────────────────────────────────────────────

void SmfParser::parse(const std::string& filename) {
    std::ifstream f(filename, std::ios::binary);
    if (!f)
        throw std::runtime_error("Cannot open MIDI file: " + filename);

    f.seekg(0, std::ios::end);
    auto sz = static_cast<size_t>(f.tellg());
    f.seekg(0);
    buf_.resize(sz);
    if (!f.read(reinterpret_cast<char*>(buf_.data()),
                static_cast<std::streamsize>(sz)))
        throw std::runtime_error("Failed to read MIDI file: " + filename);

    pos_ = 0;
    parse_header_chunk();

    tracks.resize(header.num_tracks);
    for (uint16_t i = 0; i < header.num_tracks; ++i)
        parse_track_chunk(tracks[i]);
}
