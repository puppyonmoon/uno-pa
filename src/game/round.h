#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "game/card.h"

namespace uno {

// ---------------------------------------------------------------------------
// Round -- 一局的权威状态与规则。
//
// 这里只有牌桌上的事：手牌、摸牌堆、弃牌堆、轮次、方向、罚抽、有效颜色。
// 它不知道 Message、PeerId、Socket 的存在；发消息是 HostSession 的事。
//
// 每个动作都返回一个结果结构（ok / error / 发生了什么 / 下一位是谁），
// HostSession 负责把结果翻译成 HAND / TURN / PLAYED / DRAWN / GAMEOVER。
// ---------------------------------------------------------------------------
class Round {
public:
    // 给客户端看的轮次视图
    struct View {
        int current = 0;
        std::string top;
        std::string color;
        int direction = 1;
        int pending = 0;
    };

    struct PlayResult {
        bool ok = false;
        std::string error;
        std::vector<Card> cards;
        std::string color; // 客户端传来的颜色原文，用于 PLAYED 回显
        View next;
        bool over = false;
        int winner = -1;
        int points = 0;
    };

    struct DrawResult {
        bool ok = false;
        std::string error;
        int player = -1;
        int count = 0;
        std::vector<Card> cards;
        View next;
    };

    struct TurnResult {
        bool ok = false;
        std::string error;
        View next;
    };

    struct UnoResult {
        bool ok = false;
        std::string error;
        int player = -1;
        bool legal = false; // 手里正好 1 张
        int penalty = 0;    // 错喊罚抽 2 张
    };

    // 发牌 + 翻引牌 + 引牌效果。
    // deck_override 非空时严格按它发牌（不洗牌、忽略 seed）。
    bool setup(int seats, bool has_seed, std::uint32_t seed,
               const std::vector<std::string>* deck_override, std::string& error);

    View view() const;
    const std::vector<Card>& hand(int seat) const;

    PlayResult play(int seat, const std::vector<std::string>& cards,
                    const std::string& color);
    DrawResult draw(int seat);
    TurnResult pass(int seat);
    UnoResult uno(int seat);

private:
    int step(int from, int count) const;
    void apply_opening();
    Color dominant_color(int seat) const;
    bool draw_cards(int seat, int count, std::vector<Card>& out,
                    std::string& error);
    void settle(int winner, PlayResult& result);

    int m_seats = 0;
    std::vector<std::vector<Card>> m_hands; // 下标 = 座位号
    std::vector<Card> m_draw;               // 摸牌堆，m_draw_pos 指向下一张
    std::size_t m_draw_pos = 0;
    std::vector<Card> m_discard;            // back() 是牌面

    int m_current = 0;
    int m_direction = 1; // +1 顺时针 / -1 逆时针
    int m_pending = 0;   // 面前累计的罚抽张数
    Color m_color = Color::Wild; // 万能牌生效的颜色
    bool m_drew = false;         // 本回合是否已摸牌
    Card m_drawn{};              // 刚摸到的那张
};

} // 命名空间 uno
