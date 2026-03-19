#pragma once

#include "engine/sequencer.hpp"
#include "ui/piano_roll.hpp"
#include <string>
#include <atomic>
#include <thread>

// ─────────────────────────────────────────────
//  TUI — ANSI エスケープコード / プラットフォーム
//         別ターミナル raw モードで実装
//  Windows : SetConsoleMode + ReadConsoleInput
//  Linux/macOS : termios
// ─────────────────────────────────────────────

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#else
#  include <termios.h>
#  include <unistd.h>
#  include <sys/ioctl.h>
#endif

// キー定義
enum class Key {
    None,
    Space,
    ArrowLeft,
    ArrowRight,
    Plus,
    Minus,
    Zero, One, Two, Three, Four,
    Five, Six, Seven, Eight, Nine,
    Q,
    Unknown,
};

struct TuiConfig {
    std::string filename;      // 再生中ファイル名
    bool        headless = false;
};

class Tui {
public:
    explicit Tui(TuiConfig cfg, Sequencer& seq, PlayerState& state);
    ~Tui();

    // メインループ（ブロッキング; 終了まで戻らない）
    void run();

private:
    // ────── ターミナル制御 ──────
    void enter_raw_mode();
    void leave_raw_mode();
    void hide_cursor();
    void show_cursor();
    void clear_screen();
    void move_to(int row, int col);

    // ターミナルサイズ取得
    void get_term_size(int& rows, int& cols);

    // ────── 入力 ──────
    Key poll_key();           // ノンブロッキング

    // ────── 描画 ──────
    void render();
    std::string render_progress_bar(int width) const;
    std::string render_status() const;

    // ────── ハンドラ ──────
    void handle_key(Key k);

    // ────── メンバ ──────
    TuiConfig  cfg_;
    Sequencer& seq_;
    PlayerState& state_;
    PianoRoll  roll_;

    int term_rows_ = 24;
    int term_cols_ = 80;

    // アクティブノート（チャンネルごと）
    std::vector<ActiveNote> active_notes_;
    uint8_t note_on_channel_[128] = {};   // note → channel+1 (0=off)

    // 入出力状態
    bool running_ = true;

#ifdef _WIN32
    HANDLE hin_  = INVALID_HANDLE_VALUE;
    HANDLE hout_ = INVALID_HANDLE_VALUE;
    DWORD  saved_in_mode_  = 0;
    DWORD  saved_out_mode_ = 0;
#else
    struct termios saved_termios_;
#endif
};
