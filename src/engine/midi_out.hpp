#pragma once

// ─────────────────────────────────────────────
//  プラットフォーム別 MIDI OUT ラッパー
//  Windows : WinMM  (winmm.lib  — OS 同梱)
//  macOS   : CoreMIDI
//  Linux   : ALSA   (libasound)
// ─────────────────────────────────────────────

#include <cstdint>
#include <string>
#include <vector>
#include <stdexcept>

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#  include <mmsystem.h>
#elif defined(__APPLE__)
#  include <CoreMIDI/CoreMIDI.h>
#  include <CoreFoundation/CoreFoundation.h>
#else
#  include <alsa/asoundlib.h>
#endif

class MidiOut {
public:
    MidiOut();
    ~MidiOut();

    // 利用可能なデバイス一覧（名前のリスト）
    std::vector<std::string> list_devices();

    // port = -1 → デフォルト（最初のデバイス）
    void open(int port = -1);
    void close();
    bool is_open() const { return open_; }

    // 短いMIDIメッセージ（Note On/Off / CC / PB 等）
    void send_short(uint8_t status, uint8_t d0 = 0, uint8_t d1 = 0);

    // SysEx 送信
    void send_sysex(const uint8_t* data, size_t len);

    // 全チャンネルのノートオフ + コントローラーリセット
    void all_notes_off();
    void reset_controllers();

private:
    bool open_ = false;

#ifdef _WIN32
    HMIDIOUT handle_ = nullptr;
#elif defined(__APPLE__)
    MIDIClientRef  client_ = 0;
    MIDIPortRef    port_ref_ = 0;
    MIDIEndpointRef dest_ = 0;
#else
    // ALSA sequencer
    snd_seq_t* seq_     = nullptr;
    int        seq_port_ = -1;
    int        dest_client_ = -1;
    int        dest_port_   = -1;
#endif
};
