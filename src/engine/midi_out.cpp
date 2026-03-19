#include "engine/midi_out.hpp"
#include <stdexcept>
#include <vector>
#include <string>
#include <cstring>

// ══════════════════════════════════════════════════════════
//  WINDOWS 実装 — WinMM（winmm.lib、OS 同梱）
// ══════════════════════════════════════════════════════════
#ifdef _WIN32

MidiOut::MidiOut()  = default;
MidiOut::~MidiOut() { close(); }

std::vector<std::string> MidiOut::list_devices() {
    std::vector<std::string> result;
    UINT num = midiOutGetNumDevs();
    for (UINT i = 0; i < num; ++i) {
        MIDIOUTCAPSA caps{};
        if (midiOutGetDevCapsA(i, &caps, sizeof(caps)) == MMSYSERR_NOERROR)
            result.emplace_back(caps.szPname);
    }
    return result;
}

void MidiOut::open(int port) {
    if (open_) close();

    UINT id = (port < 0) ? MIDI_MAPPER
                         : static_cast<UINT>(port);

    MMRESULT r = midiOutOpen(&handle_, id, 0, 0, CALLBACK_NULL);
    if (r != MMSYSERR_NOERROR)
        throw std::runtime_error("MidiOut: midiOutOpen failed");

    open_ = true;
    // 初期マスター音量設定は send_short で CC7 を送る (Sequencer 任せ)
}

void MidiOut::close() {
    if (!open_) return;
    all_notes_off();
    midiOutReset(handle_);
    midiOutClose(handle_);
    handle_ = nullptr;
    open_   = false;
}

void MidiOut::send_short(uint8_t status, uint8_t d0, uint8_t d1) {
    if (!open_) return;
    DWORD msg = static_cast<DWORD>(status)
              | (static_cast<DWORD>(d0) << 8)
              | (static_cast<DWORD>(d1) << 16);
    midiOutShortMsg(handle_, msg);
}

void MidiOut::send_sysex(const uint8_t* data, size_t len) {
    if (!open_ || len == 0) return;
    MIDIHDR hdr{};
    hdr.lpData          = reinterpret_cast<LPSTR>(const_cast<uint8_t*>(data));
    hdr.dwBufferLength  = static_cast<DWORD>(len);
    hdr.dwBytesRecorded = static_cast<DWORD>(len);
    midiOutPrepareHeader(handle_, &hdr, sizeof(hdr));
    midiOutLongMsg(handle_, &hdr, sizeof(hdr));
    // 送信完了を待つ（簡易実装）
    while (!(hdr.dwFlags & MHDR_DONE)) Sleep(1);
    midiOutUnprepareHeader(handle_, &hdr, sizeof(hdr));
}

void MidiOut::all_notes_off() {
    if (!open_) return;
    for (uint8_t ch = 0; ch < 16; ++ch) {
        send_short(0xB0u | ch, 123, 0);   // All Notes Off
    }
}

void MidiOut::reset_controllers() {
    if (!open_) return;
    for (uint8_t ch = 0; ch < 16; ++ch) {
        send_short(0xB0u | ch, 121, 0);   // Reset All Controllers
    }
}

// ══════════════════════════════════════════════════════════
//  macOS 実装 — CoreMIDI
// ══════════════════════════════════════════════════════════
#elif defined(__APPLE__)

MidiOut::MidiOut()  = default;
MidiOut::~MidiOut() { close(); }

static void core_midi_proc(const MIDINotification*, void*) {}

std::vector<std::string> MidiOut::list_devices() {
    std::vector<std::string> result;
    ItemCount n = MIDIGetNumberOfDestinations();
    for (ItemCount i = 0; i < n; ++i) {
        MIDIEndpointRef ep = MIDIGetDestination(i);
        CFStringRef name   = nullptr;
        MIDIObjectGetStringProperty(ep, kMIDIPropertyName, &name);
        if (name) {
            char buf[256] = {};
            CFStringGetCString(name, buf, sizeof(buf), kCFStringEncodingUTF8);
            CFRelease(name);
            result.emplace_back(buf);
        }
    }
    return result;
}

void MidiOut::open(int port) {
    if (open_) close();

    ItemCount n = MIDIGetNumberOfDestinations();
    if (n == 0) throw std::runtime_error("MidiOut: no MIDI destinations");

    ItemCount idx = (port < 0 || static_cast<ItemCount>(port) >= n)
                  ? 0 : static_cast<ItemCount>(port);
    dest_ = MIDIGetDestination(idx);

    MIDIClientCreate(CFSTR("midi-player"), core_midi_proc, nullptr, &client_);
    MIDIOutputPortCreate(client_, CFSTR("output"), &port_ref_);
    open_ = true;
}

void MidiOut::close() {
    if (!open_) return;
    all_notes_off();
    MIDIPortDispose(port_ref_);
    MIDIClientDispose(client_);
    client_   = 0;
    port_ref_ = 0;
    dest_     = 0;
    open_     = false;
}

void MidiOut::send_short(uint8_t status, uint8_t d0, uint8_t d1) {
    if (!open_) return;
    uint8_t buf[3]  = {status, d0, d1};
    uint8_t type    = status & 0xF0u;
    int     len     = (type == 0xC0u || type == 0xD0u) ? 2 : 3;

    MIDIPacketList pktList;
    MIDIPacket*    pkt = MIDIPacketListInit(&pktList);
    pkt = MIDIPacketListAdd(&pktList, sizeof(pktList), pkt, 0, len, buf);
    if (pkt) MIDISend(port_ref_, dest_, &pktList);
}

void MidiOut::send_sysex(const uint8_t* data, size_t len) {
    if (!open_ || len == 0) return;
    // CoreMIDI の MIDIPacketList サイズ上限回避（大きいなら分割が必要）
    std::vector<uint8_t> pkt_buf(sizeof(MIDIPacketList) + len + 16);
    MIDIPacketList* pl  = reinterpret_cast<MIDIPacketList*>(pkt_buf.data());
    MIDIPacket*    pkt  = MIDIPacketListInit(pl);
    pkt = MIDIPacketListAdd(pl, pkt_buf.size(), pkt, 0, len, data);
    if (pkt) MIDISend(port_ref_, dest_, pl);
}

void MidiOut::all_notes_off() {
    if (!open_) return;
    for (uint8_t ch = 0; ch < 16; ++ch)
        send_short(0xB0u | ch, 123, 0);
}

void MidiOut::reset_controllers() {
    if (!open_) return;
    for (uint8_t ch = 0; ch < 16; ++ch)
        send_short(0xB0u | ch, 121, 0);
}

// ══════════════════════════════════════════════════════════
//  Linux 実装 — ALSA Sequencer（x64 / ARM 共通）
// ══════════════════════════════════════════════════════════
#else

MidiOut::MidiOut()  = default;
MidiOut::~MidiOut() { close(); }

std::vector<std::string> MidiOut::list_devices() {
    std::vector<std::string> result;
    snd_seq_t* seq = nullptr;
    if (snd_seq_open(&seq, "default", SND_SEQ_OPEN_OUTPUT, 0) < 0) return result;

    snd_seq_client_info_t* cinfo;
    snd_seq_port_info_t*   pinfo;
    snd_seq_client_info_alloca(&cinfo);
    snd_seq_port_info_alloca(&pinfo);

    snd_seq_client_info_set_client(cinfo, -1);
    while (snd_seq_query_next_client(seq, cinfo) >= 0) {
        int client = snd_seq_client_info_get_client(cinfo);
        snd_seq_port_info_set_client(pinfo, client);
        snd_seq_port_info_set_port(pinfo, -1);
        while (snd_seq_query_next_port(seq, pinfo) >= 0) {
            unsigned caps = snd_seq_port_info_get_capability(pinfo);
            if ((caps & SND_SEQ_PORT_CAP_WRITE) &&
                (caps & SND_SEQ_PORT_CAP_SUBS_WRITE)) {
                result.emplace_back(snd_seq_port_info_get_name(pinfo));
            }
        }
    }
    snd_seq_close(seq);
    return result;
}

void MidiOut::open(int /*port*/) {
    if (open_) close();

    if (snd_seq_open(&seq_, "default", SND_SEQ_OPEN_OUTPUT, 0) < 0)
        throw std::runtime_error("MidiOut: snd_seq_open failed");

    snd_seq_set_client_name(seq_, "midi-player");
    seq_port_ = snd_seq_create_simple_port(
        seq_, "output",
        SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_SUBS_READ,
        SND_SEQ_PORT_TYPE_MIDI_GENERIC | SND_SEQ_PORT_TYPE_APPLICATION);

    if (seq_port_ < 0) {
        snd_seq_close(seq_);
        throw std::runtime_error("MidiOut: create port failed");
    }

    // Auto-connect to the first available MIDI synth
    snd_seq_client_info_t* cinfo;
    snd_seq_port_info_t*   pinfo;
    snd_seq_client_info_alloca(&cinfo);
    snd_seq_port_info_alloca(&pinfo);

    snd_seq_client_info_set_client(cinfo, -1);
    bool connected = false;
    while (!connected && snd_seq_query_next_client(seq_, cinfo) >= 0) {
        int client = snd_seq_client_info_get_client(cinfo);
        if (client == SND_SEQ_CLIENT_SYSTEM) continue;
        snd_seq_port_info_set_client(pinfo, client);
        snd_seq_port_info_set_port(pinfo, -1);
        while (snd_seq_query_next_port(seq_, pinfo) >= 0) {
            unsigned caps = snd_seq_port_info_get_capability(pinfo);
            if ((caps & SND_SEQ_PORT_CAP_WRITE) &&
                (caps & SND_SEQ_PORT_CAP_SUBS_WRITE)) {
                snd_seq_connect_to(seq_, seq_port_, client,
                                   snd_seq_port_info_get_port(pinfo));
                connected = true;
                break;
            }
        }
    }
    open_ = true;
}

void MidiOut::close() {
    if (!open_) return;
    all_notes_off();
    snd_seq_close(seq_);
    seq_      = nullptr;
    seq_port_ = -1;
    open_     = false;
}

void MidiOut::send_short(uint8_t status, uint8_t d0, uint8_t d1) {
    if (!open_ || !seq_) return;
    snd_seq_event_t ev{};
    snd_seq_ev_clear(&ev);
    snd_seq_ev_set_direct(&ev);
    snd_seq_ev_set_source(&ev, static_cast<unsigned char>(seq_port_));
    snd_seq_ev_set_subs(&ev);

    uint8_t type = status & 0xF0u;
    uint8_t ch   = status & 0x0Fu;

    switch (type) {
        case 0x80u:
            snd_seq_ev_set_noteoff(&ev, ch, d0, d1); break;
        case 0x90u:
            if (d1 == 0) snd_seq_ev_set_noteoff(&ev, ch, d0, 0);
            else         snd_seq_ev_set_noteon(&ev, ch, d0, d1);
            break;
        case 0xA0u:
            snd_seq_ev_set_keypress(&ev, ch, d0, d1); break;
        case 0xB0u:
            snd_seq_ev_set_controller(&ev, ch, d0, d1); break;
        case 0xC0u:
            snd_seq_ev_set_pgmchange(&ev, ch, d0); break;
        case 0xD0u:
            snd_seq_ev_set_chanpress(&ev, ch, d0); break;
        case 0xE0u: {
            int pb = (static_cast<int>(d1) << 7) | d0;
            snd_seq_ev_set_pitchbend(&ev, ch, pb - 8192); break;
        }
        default: return;
    }
    snd_seq_event_output_direct(seq_, &ev);
}

void MidiOut::send_sysex(const uint8_t* data, size_t len) {
    if (!open_ || !seq_ || len == 0) return;
    snd_seq_event_t ev{};
    snd_seq_ev_clear(&ev);
    snd_seq_ev_set_direct(&ev);
    snd_seq_ev_set_source(&ev, static_cast<unsigned char>(seq_port_));
    snd_seq_ev_set_subs(&ev);
    snd_seq_ev_set_sysex(&ev, static_cast<unsigned int>(len),
                          const_cast<uint8_t*>(data));
    snd_seq_event_output_direct(seq_, &ev);
}

void MidiOut::all_notes_off() {
    if (!open_) return;
    for (uint8_t ch = 0; ch < 16; ++ch)
        send_short(0xB0u | ch, 123, 0);
}

void MidiOut::reset_controllers() {
    if (!open_) return;
    for (uint8_t ch = 0; ch < 16; ++ch)
        send_short(0xB0u | ch, 121, 0);
}

#endif
