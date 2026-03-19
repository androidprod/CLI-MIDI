#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <array>
#include <deque>

// チャンネルごとの ANSI 256色パレット
static constexpr std::array<int, 12> CH_COLORS = {
    196,  // ch0  Red
     46,  // ch1  Green
     39,  // ch2  Blue
     51,  // ch3  Cyan
    201,  // ch4  Magenta
    226,  // ch5  Yellow
    208,  // ch6  Orange
    129,  // ch7  Purple
     82,  // ch8  Lime
    213,  // ch9  Pink (Drums)
     45,  // ch10 Sky
    190,  // ch11 Chartreuse
};

struct ActiveNote {
    uint8_t note;
    uint8_t channel;
};

// ─────────────────────────────────────────────
//  ピアノロール描画（ヘッダオンリー）
// ─────────────────────────────────────────────
class PianoRoll {
public:
    static constexpr int NOTE_LOW  = 36;
    static constexpr int NOTE_HIGH = 96;

    void set_active(const std::vector<ActiveNote>& notes) { 
        active_ = notes; 
    }

    static std::string note_name(int note) {
        static const char* names[] = {
            "C","C#","D","D#","E","F","F#","G","G#","A","A#","B"
        };
        int oct = (note / 12) - 1;
        return std::string(names[note % 12]) + std::to_string(oct);
    }

    static bool is_black_key(int note) {
        int n = note % 12;
        return n == 1 || n == 3 || n == 6 || n == 8 || n == 10;
    }

    // 未来の行（上）から現在の行（下）へ向かって描画
    // future_rows[0] が現在（一番下のライン）、future_rows[N] が未来（一番上）
    std::string render_waterfall(int lines, int term_cols, const std::vector<std::vector<ActiveNote>>& future_rows) const {
        if (lines <= 1) return "";
        std::string out;
        
        int draw_lines = static_cast<int>(future_rows.size());
        int padding_len = (term_cols - 48) / 2;
        if (padding_len < 0) padding_len = 0;
        std::string pad(padding_len, ' ');
        
        for (int i = draw_lines - 1; i >= 0; --i) {
            out += pad;
            const auto& row_notes = future_rows[i];
            bool is_current = (i == 0); // ヒット判定ライン
            
            for (int n = 48; n < 96; ++n) {
                int color = -1;
                for (const auto& an : row_notes) {
                    if (an.note == n) {
                        color = CH_COLORS[an.channel % 12];
                        break;
                    }
                }
                
                if (color >= 0) {
                    // ノートブロック（現在はもちろん、未来から降ってくるブロックも塗る）
                    out += "\033[38;5;" + std::to_string(color) + "m\xe2\x96\x88\033[0m";
                } else {
                    if (is_current) {
                        // 判定ライン（一番下）の白鍵/黒鍵デザイン
                        bool blk = is_black_key(n);
                        out += blk ? "\033[38;5;236m\xc2\xb7\033[0m"   // · 黒鍵
                                   : "\033[38;5;244m\xc2\xb7\033[0m";  // · 白鍵
                    } else {
                        // 未来の背景（何もない空間）
                        out += " ";
                    }
                }
            }
            out += "\033[K\n";
        }
        
        // 最後に一番下に鍵盤の C3... ガイドを描画
        out += pad;
        out += "\033[38;5;240mC3            C4            C5"
               "            C6              \033[0m\033[K\n";
               
        return out;
    }

    // アクティブノートの文字列リスト
    std::string render_active_list(int max_n = 12) const {
        std::string out = " ";
        int shown = 0;
        for (const auto& an : active_) {
            if (shown >= max_n) break;
            int color = CH_COLORS[an.channel % 12];
            out += "\033[38;5;" + std::to_string(color) + "m";
            out += "[" + note_name(an.note) + "]";
            out += "\033[0m ";
            ++shown;
        }
        if (shown == 0) out += "\033[38;5;239m(none)\033[0m";
        return out;
    }

private:
    std::vector<ActiveNote> active_;
};
