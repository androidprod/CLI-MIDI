#include "parser/smf_parser.hpp"
#include "parser/tempo_map.hpp"
#include "parser/event_merger.hpp"
#include "engine/midi_out.hpp"
#include "engine/sequencer.hpp"
#include "ui/tui.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <chrono>

// ══════════════════════════════════════════════════════════
//  CLI 設定
// ══════════════════════════════════════════════════════════

struct CliConfig {
    std::string filename;
    int         midi_port    = -1;    // -1 = デフォルト
    float       tempo_scale  = 1.0f;
    int         volume       = 80;
    bool        no_tui       = false;
    bool        list_devices = false;
};

static void print_help(const char* prog) {
    std::printf(
        "Usage: %s [options] <file.mid>\n"
        "\n"
        "Options:\n"
        "  -o, --output <port>   MIDI OUT port number (default: first device)\n"
        "  -t, --tempo  <scale>  Tempo scale factor   (e.g. 1.5 = 150%%)\n"
        "  -v, --volume <0-100>  Initial volume        (default: 80)\n"
        "      --no-tui          Headless playback\n"
        "      --list-devices    Show available MIDI OUT devices\n"
        "  -h, --help            Show this help\n"
        "\n"
        "Keyboard shortcuts (TUI mode):\n"
        "  [SPACE]       Play / Pause\n"
        "  [<] [>]       Seek -5s / +5s\n"
        "  [+] [-]       Tempo +10%% / -10%%\n"
        "  [0-9]         Volume (0=0%% … 9=90%%)\n"
        "  [Q]           Quit\n",
        prog
    );
}

static CliConfig parse_args(int argc, char* argv[]) {
    CliConfig cfg;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "-h" || a == "--help") {
            print_help(argv[0]);
            std::exit(0);
        } else if (a == "--list-devices") {
            cfg.list_devices = true;
        } else if (a == "--no-tui") {
            cfg.no_tui = true;
        } else if ((a == "-o" || a == "--output") && i + 1 < argc) {
            cfg.midi_port = std::atoi(argv[++i]);
        } else if ((a == "-t" || a == "--tempo") && i + 1 < argc) {
            cfg.tempo_scale = static_cast<float>(std::atof(argv[++i]));
        } else if ((a == "-v" || a == "--volume") && i + 1 < argc) {
            cfg.volume = std::atoi(argv[++i]);
        } else if (a[0] == '-') {
            std::fprintf(stderr, "Unknown option: %s\n", a.c_str());
            std::exit(1);
        } else {
            cfg.filename = a;
        }
    }
    return cfg;
}

// ══════════════════════════════════════════════════════════
//  ヘッドレスモード（TUI なし）
// ══════════════════════════════════════════════════════════

static void run_headless(Sequencer& seq, PlayerState& state) {
    seq.play();
    std::printf("Playing… Press Enter to stop.\n");

    // エンターキー待ち
    std::thread input_thread([&] {
        std::getchar();
        seq.stop();
    });

    while (state.playing.load()) {
        uint64_t pos = state.pos_us.load();
        uint64_t dur = state.duration_us.load();
        int pct = (dur > 0)
            ? static_cast<int>(100ULL * pos / dur)
            : 0;
        std::printf("\r  %u:%02u / %u:%02u  [%3d%%]",
            static_cast<unsigned>(pos / 1'000'000ULL / 60),
            static_cast<unsigned>(pos / 1'000'000ULL % 60),
            static_cast<unsigned>(dur / 1'000'000ULL / 60),
            static_cast<unsigned>(dur / 1'000'000ULL % 60),
            pct);
        std::fflush(stdout);
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    std::puts("\nDone.");
    if (input_thread.joinable()) input_thread.detach();
}

// ══════════════════════════════════════════════════════════
//  main
// ══════════════════════════════════════════════════════════

int main(int argc, char* argv[]) {
    CliConfig cfg = parse_args(argc, argv);

    // ── MIDI デバイス列挙 ───────────────────────────────
    if (cfg.list_devices) {
        MidiOut probe;
        auto devices = probe.list_devices();
        if (devices.empty()) {
            std::puts("No MIDI OUT devices found.");
        } else {
            std::puts("Available MIDI OUT devices:");
            for (int i = 0; i < static_cast<int>(devices.size()); ++i)
                std::printf("  [%d] %s\n", i, devices[i].c_str());
        }
        return 0;
    }

    // ── ファイル確認 ────────────────────────────────────
    if (cfg.filename.empty()) {
        print_help(argv[0]);
        return 1;
    }

    try {
        // ── SMF パース ──────────────────────────────────
        SmfParser parser;
        parser.parse(cfg.filename);

        const auto& hdr = parser.header;
        std::printf("Format: %u  Tracks: %u  TPQN: %u\n",
                    hdr.format, hdr.num_tracks, hdr.ticks_per_qn);

        // ── テンポマップ構築 ─────────────────────────────
        TempoMap tmap(hdr.ticks_per_qn);

        // Format 1: テンポ情報はトラック 0 に集約されている
        for (const auto& track : parser.tracks) {
            for (const auto& ev : track) {
                if (ev.is_meta && ev.meta_type == 0x51u &&
                    ev.meta_data.size() >= 3) {
                    uint32_t tempo =
                        (static_cast<uint32_t>(ev.meta_data[0]) << 16) |
                        (static_cast<uint32_t>(ev.meta_data[1]) <<  8) |
                         static_cast<uint32_t>(ev.meta_data[2]);
                    tmap.add_tempo(ev.abs_tick, tempo);
                }
            }
        }

        // ── abs_us を全イベントに適用 ─────────────────────
        for (auto& track : parser.tracks)
            for (auto& ev : track)
                ev.abs_us = tmap.tick_to_us(ev.abs_tick);

        // ── マルチトラック統合 ────────────────────────────
        auto merged = merge_tracks(parser.tracks);

        // ── MIDI OUT 初期化 ───────────────────────────────
        MidiOut midi_out;
        auto devices = midi_out.list_devices();
        if (devices.empty()) {
            std::fputs("ERROR: No MIDI OUT devices found.\n"
                       "  Windows: Microsoft GS Wavetable Synth should always be present.\n"
                       "  Linux:   sudo apt install timidity / fluidsynth\n", stderr);
            return 1;
        }

        std::printf("MIDI OUT: [%d] %s\n",
                    cfg.midi_port < 0 ? 0 : cfg.midi_port,
                    cfg.midi_port < 0
                        ? devices[0].c_str()
                        : (cfg.midi_port < static_cast<int>(devices.size())
                            ? devices[static_cast<size_t>(cfg.midi_port)].c_str() : "?"));

        midi_out.open(cfg.midi_port);

        // ── シーケンサー設定 ──────────────────────────────
        Sequencer seq(midi_out);
        seq.load(std::move(merged));

        PlayerState& state = seq.state();
        state.tempo_scale.store(cfg.tempo_scale);
        state.volume_pct.store(cfg.volume);

        // ── 再生 ─────────────────────────────────────────
        if (cfg.no_tui) {
            run_headless(seq, state);
        } else {
            // ファイル名のベース部分だけ表示
            std::string basename = cfg.filename;
            auto pos = basename.find_last_of("/\\");
            if (pos != std::string::npos) basename = basename.substr(pos + 1);

            TuiConfig tui_cfg;
            tui_cfg.filename = basename;

            Tui tui(tui_cfg, seq, state);
            tui.run();
        }

        midi_out.close();
        return 0;

    } catch (const std::exception& e) {
        std::fprintf(stderr, "Error: %s\n", e.what());
        return 1;
    }
}
