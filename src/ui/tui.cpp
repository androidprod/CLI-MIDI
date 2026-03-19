#include "ui/tui.hpp"

#include <cstdio>
#include <cstring>
#include <string>
#include <chrono>
#include <thread>
#include <algorithm>

// ══════════════════════════════════════════════════════════
//  プラットフォーム固有 helpers
// ══════════════════════════════════════════════════════════

#ifdef _WIN32

void Tui::enter_raw_mode() {
    hin_  = GetStdHandle(STD_INPUT_HANDLE);
    hout_ = GetStdHandle(STD_OUTPUT_HANDLE);

    GetConsoleMode(hin_,  &saved_in_mode_);
    GetConsoleMode(hout_, &saved_out_mode_);

    // 生入力モード (行入力・エコーを無効)
    DWORD in_mode = ENABLE_WINDOW_INPUT | ENABLE_MOUSE_INPUT | ENABLE_EXTENDED_FLAGS;
    SetConsoleMode(hin_, in_mode);

    // ANSI / VT100 有効化
    DWORD out_mode = saved_out_mode_
                   | ENABLE_PROCESSED_OUTPUT
                   | ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    SetConsoleMode(hout_, out_mode);

    // UTF-8 出力
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
}

void Tui::leave_raw_mode() {
    if (hin_  != INVALID_HANDLE_VALUE) SetConsoleMode(hin_,  saved_in_mode_);
    if (hout_ != INVALID_HANDLE_VALUE) SetConsoleMode(hout_, saved_out_mode_);
}

Key Tui::poll_key() {
    DWORD n = 0;
    if (!GetNumberOfConsoleInputEvents(hin_, &n) || n == 0) return Key::None;

    while (true) {
        if (!GetNumberOfConsoleInputEvents(hin_, &n) || n == 0) return Key::None;
        INPUT_RECORD rec{};
        DWORD read = 0;
        if (!ReadConsoleInputW(hin_, &rec, 1, &read) || read == 0) return Key::None;

        if (rec.EventType == WINDOW_BUFFER_SIZE_EVENT) {
            get_term_size(term_rows_, term_cols_);
            continue;
        }
        if (rec.EventType != KEY_EVENT || !rec.Event.KeyEvent.bKeyDown) continue;

        WORD  vk  = rec.Event.KeyEvent.wVirtualKeyCode;
        WCHAR wch = rec.Event.KeyEvent.uChar.UnicodeChar;

        switch (vk) {
            case VK_SPACE:    return Key::Space;
            case VK_LEFT:     return Key::ArrowLeft;
            case VK_RIGHT:    return Key::ArrowRight;
            case VK_ADD:      return Key::Plus;
            case VK_SUBTRACT: return Key::Minus;
            case VK_OEM_PLUS: return Key::Plus;
            case VK_OEM_MINUS:return Key::Minus;
            default: break;
        }
        if (wch >= L'0' && wch <= L'9')
            return static_cast<Key>(static_cast<int>(Key::Zero) + (wch - L'0'));
        if (wch == L'+') return Key::Plus;
        if (wch == L'-') return Key::Minus;
        if (wch == L'q' || wch == L'Q') return Key::Q;
        
        return Key::Unknown;
    }
}

void Tui::get_term_size(int& rows, int& cols) {
    CONSOLE_SCREEN_BUFFER_INFO csbi{};
    if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi)) {
        cols = csbi.srWindow.Right  - csbi.srWindow.Left + 1;
        rows = csbi.srWindow.Bottom - csbi.srWindow.Top  + 1;
    } else {
        rows = 24; cols = 80;
    }
}

#else  // POSIX (Linux x64/ARM, macOS)

#include <sys/select.h>

void Tui::enter_raw_mode() {
    tcgetattr(STDIN_FILENO, &saved_termios_);
    struct termios raw = saved_termios_;
    raw.c_lflag &= ~static_cast<tcflag_t>(ICANON | ECHO | ISIG);
    raw.c_iflag &= ~static_cast<tcflag_t>(IXON | ICRNL);
    raw.c_cc[VMIN]  = 0;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

void Tui::leave_raw_mode() {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &saved_termios_);
}

Key Tui::poll_key() {
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(STDIN_FILENO, &fds);
    struct timeval tv{0, 0};
    if (select(STDIN_FILENO + 1, &fds, nullptr, nullptr, &tv) <= 0)
        return Key::None;

    char buf[8] = {};
    int  n = static_cast<int>(read(STDIN_FILENO, buf, sizeof(buf) - 1));
    if (n <= 0) return Key::None;

    // エスケープシーケンス（矢印キー）
    if (n >= 3 && buf[0] == '\033' && buf[1] == '[') {
        switch (buf[2]) {
            case 'C': return Key::ArrowRight;
            case 'D': return Key::ArrowLeft;
            default:  return Key::Unknown;
        }
    }

    char c = buf[0];
    if (c == ' ')               return Key::Space;
    if (c == 'q' || c == 'Q')  return Key::Q;
    if (c == '+' || c == '=')  return Key::Plus;
    if (c == '-')               return Key::Minus;
    if (c >= '0' && c <= '9')
        return static_cast<Key>(static_cast<int>(Key::Zero) + (c - '0'));
    return Key::Unknown;
}

void Tui::get_term_size(int& rows, int& cols) {
    struct winsize ws{};
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0) {
        rows = ws.ws_row;
        cols = ws.ws_col;
    } else {
        rows = 24; cols = 80;
    }
}

#endif  // platform

// ══════════════════════════════════════════════════════════
//  共通 ANSI helpers（ヘッダで #ifdef 済みの後に定義）
// ══════════════════════════════════════════════════════════

static void ansi_write(const char* s) { std::fputs(s, stdout); }

void Tui::hide_cursor()  { ansi_write("\033[?25l\033[?7l"); fflush(stdout); }
void Tui::show_cursor()  { ansi_write("\033[?25h\033[?7h"); fflush(stdout); }
void Tui::clear_screen() { ansi_write("\033[2J\033[H"); fflush(stdout); }
void Tui::move_to(int row, int col) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "\033[%d;%dH", row, col);
    ansi_write(buf);
}

// ══════════════════════════════════════════════════════════
//  コンストラクタ / デストラクタ
// ══════════════════════════════════════════════════════════

Tui::Tui(TuiConfig cfg, Sequencer& seq, PlayerState& state)
    : cfg_(std::move(cfg)), seq_(seq), state_(state) {}

Tui::~Tui() {
    show_cursor();
    leave_raw_mode();
}

// ══════════════════════════════════════════════════════════
//  描画ヘルパー
// ══════════════════════════════════════════════════════════

static std::string fmt_time(uint64_t us) {
    uint64_t s = us / 1'000'000ULL;
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%u:%02u",
                  static_cast<unsigned>(s / 60),
                  static_cast<unsigned>(s % 60));
    return buf;
}

std::string Tui::render_progress_bar(int width) const {
    uint64_t pos = state_.pos_us.load();
    uint64_t dur = state_.duration_us.load();

    std::string left  = fmt_time(pos);
    std::string right = fmt_time(dur);
    int bar_w = width - static_cast<int>(left.size() + right.size()) - 4;
    if (bar_w < 4) bar_w = 4;

    int filled = (dur > 0)
        ? static_cast<int>(static_cast<uint64_t>(bar_w) * pos / dur)
        : 0;
    if (filled > bar_w) filled = bar_w;

    std::string bar;
    bar += "\033[38;5;246m" + left + " \033[0m";
    bar += "\033[38;5;33m";
    for (int i = 0; i < filled;      ++i) bar += "\xe2\x96\x88";  // █
    bar += "\033[38;5;239m";
    for (int i = filled; i < bar_w;  ++i) bar += "\xe2\x96\x91";  // ░
    bar += "\033[0m";
    bar += "\033[38;5;246m " + right + "\033[0m";
    return bar;
}

std::string Tui::render_status() const {
    bool  playing = state_.playing.load();
    bool  paused  = state_.paused.load();
    float scale   = state_.tempo_scale.load();
    int   vol     = state_.volume_pct.load();

    std::string s;
    if (!playing)         s += "\033[38;5;240m[STOPPED]\033[0m";
    else if (paused)      s += "\033[38;5;226m[PAUSED] \033[0m";
    else                  s += "\033[38;5;82m[PLAYING]\033[0m";

    char buf[64];
    std::snprintf(buf, sizeof(buf),
                  "  Tempo x%.2f   Vol %3d%%", static_cast<double>(scale), vol);
    s += "\033[38;5;252m";
    s += buf;
    s += "\033[0m";
    return s;
}

// ══════════════════════════════════════════════════════════
//  描画メイン
// ══════════════════════════════════════════════════════════

void Tui::render() {
    int old_r = term_rows_, old_c = term_cols_;
    get_term_size(term_rows_, term_cols_);

    // サイズが変化したら一度画面全体をクリア
    if (old_r != term_rows_ || old_c != term_cols_) {
        clear_screen();
    }

    // アクティブノートを state_ から収集
    active_notes_.clear();
    for (int n = 0; n < 128; ++n) {
        uint8_t ch1 = state_.note_channel[n].load(std::memory_order_relaxed);
        if (ch1 > 0)
            active_notes_.push_back({static_cast<uint8_t>(n),
                                     static_cast<uint8_t>(ch1 - 1)});
    }
    roll_.set_active(active_notes_);

    // ウォーターフォールに割り当てる行数を計算
    // 固定行: Title(1) + Sep(1) + Active(1) + Sep(1) + Prog(1) + Status(1) + Sep(1) + Help(1) = 8
    int fixed_lines = 8;
    if (state_.finished.load()) fixed_lines += 1;
    int waterfall_lines = term_rows_ - fixed_lines;
    if (waterfall_lines < 3) waterfall_lines = 3;

    // ─── バッファに全レンダリング ───
    std::string out;
    out.reserve(8192);

    // カーソルをホームへ
    out += "\033[H";

    // ── ヘッダ ──
    {
        std::string title = " CLI-MIDI | " + cfg_.filename;
        if (static_cast<int>(title.size()) > term_cols_ - 2)
            title = title.substr(0, static_cast<size_t>(term_cols_ - 2));
        out += "\033[1;38;5;75m";
        out += title;
        out += "\033[0m\033[K\n";
    }

    // ── セパレータ ──
    out += "\033[38;5;238m";
    for (int i = 0; i < term_cols_; ++i) out += "\xe2\x94\x80";  // ─
    out += "\033[0m\033[K\n";

    // ── アクティブノート一覧 ──
    out += " Active: ";
    out += roll_.render_active_list(16);
    out += "\033[K\n";

    // 未来のノート状態をシミュレーション（1行 = 約40ms * scale）
    int draw_lines = waterfall_lines - 1; // 1行は下の鍵盤ラベル用
    std::vector<std::vector<ActiveNote>> future_rows(draw_lines);
    
    // 滑らかなウォーターフォール進行のため、イベント発火時ではなくリアルタイム時刻を取得
    uint64_t start_us = state_.pos_us.load();
    if (state_.playing.load() && !state_.paused.load()) {
        int64_t tb = state_.time_base_us.load(std::memory_order_relaxed);
        if (tb > 0) {
            int64_t now_us = std::chrono::time_point_cast<std::chrono::microseconds>(
                std::chrono::high_resolution_clock::now()).time_since_epoch().count();
            int64_t diff = now_us - tb;
            if (diff > 0) {
                float s = state_.tempo_scale.load();
                uint64_t smooth_pos = static_cast<uint64_t>(diff * s);
                // 後戻り防止
                if (smooth_pos > start_us) start_us = smooth_pos;
            }
        }
    }
    
    float scale = state_.tempo_scale.load();
    uint64_t step_us = static_cast<uint64_t>(40000.0 * scale);

    // 現在の状態をコピー
    uint8_t cur_state[128];
    for (int n = 0; n < 128; ++n) {
        cur_state[n] = state_.note_channel[n].load(std::memory_order_relaxed);
    }

    const auto& events = seq_.get_events();
    auto it = std::lower_bound(events.begin(), events.end(), start_us, 
        [](const MidiEvent& ev, uint64_t us) { return ev.abs_us < us; });

    for (int row = 0; row < draw_lines; ++row) {
        uint64_t row_end = start_us + (row + 1) * step_us;
        
        while (it != events.end() && it->abs_us < row_end) {
            if (!it->is_meta && !it->is_sysex) {
                uint8_t type = it->status & 0xF0u;
                uint8_t note = it->data[0] & 0x7Fu;
                uint8_t vel = it->data[1];
                if (type == 0x90u && vel > 0) {
                    cur_state[note] = it->channel + 1;
                } else if (type == 0x80u || (type == 0x90u && vel == 0)) {
                    cur_state[note] = 0;
                }
            }
            ++it;
        }
        
        for (int n = 48; n < 96; ++n) {
            if (cur_state[n] > 0) {
                future_rows[row].push_back({static_cast<uint8_t>(n), static_cast<uint8_t>(cur_state[n] - 1)});
            }
        }
    }

    out += roll_.render_waterfall(waterfall_lines, term_cols_, future_rows);

    // ── セパレータ ──
    out += "\033[38;5;238m";
    for (int i = 0; i < term_cols_; ++i) out += "\xe2\x94\x80";
    out += "\033[0m\033[K\n";

    // ── プログレスバー ──
    out += " ";
    out += render_progress_bar(term_cols_ - 2);
    out += "\033[K\n";

    // ── ステータス ──
    out += " ";
    out += render_status();
    out += "\033[K\n";

    // ── セパレータ ──
    out += "\033[38;5;238m";
    for (int i = 0; i < term_cols_; ++i) out += "\xe2\x94\x80";
    out += "\033[0m\n";

    // ── キー操作ヘルプ ──
    out += "\033[38;5;242m";
    out += " [SPACE]Play/Pause  [<]Seek-5s  [>]Seek+5s"
           "  [+/-]Tempo  [0-9]Vol  [Q]Quit";
    out += "\033[0m\033[K"; // <--- 最後の行は \n を出力しない（スクロールジャンプ防止）

    // 終了メッセージ
    if (state_.finished.load()) {
        out += "\n\033[1;38;5;82m Playback finished. Auto-restarting in 3 seconds... (Press Q to quit)\033[0m\033[K"; 
    }

    std::fwrite(out.data(), 1, out.size(), stdout);
    fflush(stdout);
}

// ══════════════════════════════════════════════════════════
//  キー操作ハンドラ
// ══════════════════════════════════════════════════════════

void Tui::handle_key(Key k) {
    switch (k) {
        case Key::Space:
            if (!state_.playing.load()) {
                seq_.play();
            } else if (state_.paused.load()) {
                seq_.resume();
            } else {
                seq_.pause();
            }
            break;
        case Key::ArrowLeft:  seq_.seek_relative(-5.0); break;
        case Key::ArrowRight: seq_.seek_relative(+5.0); break;
        case Key::Plus: {
            float s = state_.tempo_scale.load();
            seq_.set_tempo_scale(s + 0.1f);
            break;
        }
        case Key::Minus: {
            float s = state_.tempo_scale.load();
            seq_.set_tempo_scale(s - 0.1f);
            break;
        }
        case Key::Zero: seq_.set_volume(0);   break;
        case Key::One:  seq_.set_volume(10);  break;
        case Key::Two:  seq_.set_volume(20);  break;
        case Key::Three:seq_.set_volume(30);  break;
        case Key::Four: seq_.set_volume(40);  break;
        case Key::Five: seq_.set_volume(50);  break;
        case Key::Six:  seq_.set_volume(60);  break;
        case Key::Seven:seq_.set_volume(70);  break;
        case Key::Eight:seq_.set_volume(80);  break;
        case Key::Nine: seq_.set_volume(90);  break;
        case Key::Q:
            seq_.stop();
            running_ = false;
            break;
        default: break;
    }
}

// ══════════════════════════════════════════════════════════
//  メインループ
// ══════════════════════════════════════════════════════════

void Tui::run() {
    enter_raw_mode();
    get_term_size(term_rows_, term_cols_);
    hide_cursor();
    clear_screen();

    // 再生開始
    seq_.play();

    auto loop_timer_start = std::chrono::steady_clock::time_point{};

    // 再生完了コールバック（finished フラグは Sequencer がセット済み）

    while (running_) {
        // 全入力キューを空にするまで処理
        Key k;
        while ((k = poll_key()) != Key::None) {
            if (k != Key::Unknown) handle_key(k);
            if (!running_) break;
        }

        render();

        // 終了待ち（3秒後に自動ループ）
        if (state_.finished.load() && !state_.playing.load()) {
            if (loop_timer_start == std::chrono::steady_clock::time_point{}) {
                loop_timer_start = std::chrono::steady_clock::now();
            } else {
                if (std::chrono::steady_clock::now() - loop_timer_start >= std::chrono::seconds(3)) {
                    seq_.stop();
                    seq_.play();
                    loop_timer_start = std::chrono::steady_clock::time_point{};
                }
            }
        } else {
            loop_timer_start = std::chrono::steady_clock::time_point{};
        }

        // 30fps wait
        std::this_thread::sleep_for(std::chrono::milliseconds(33));
    }

    show_cursor();
    leave_raw_mode();
    // 画面をクリアして終了
    std::fputs("\033[2J\033[H", stdout);
    fflush(stdout);
}
