#include "game/round.h"

#include <algorithm>
#include <random>

namespace uno {

bool Round::setup(int seats, bool has_seed, std::uint32_t seed,
                  const std::vector<std::string>* deck_override,
                  std::string& error) {
    if (seats < 2) {
        error = "need at least 2 players";
        return false;
    }
    m_seats = seats;

    std::vector<Card> deck;
    if (deck_override != nullptr) {
        // 给了固定牌堆：不洗牌，严格按给定顺序发。
        for (const std::string& name : *deck_override) {
            Card card;
            if (!parse_card(name, card)) {
                error = "invalid card: " + name;
                return false;
            }
            deck.push_back(card);
        }
    } else {
        deck = make_deck();
        std::mt19937 generator(has_seed ? seed : std::random_device{}());
        std::shuffle(deck.begin(), deck.end(), generator);
    }

    const std::size_t needed = static_cast<std::size_t>(seats) * 7 + 1;
    if (deck.size() < needed) {
        error = "deck has " + std::to_string(deck.size()) + " cards, need " +
                std::to_string(needed);
        return false;
    }

    m_hands.assign(static_cast<std::size_t>(seats), {});
    for (int seat = 0; seat < seats; ++seat) {
        const auto first = deck.begin() + seat * 7;
        m_hands[seat].assign(first, first + 7);
    }
    m_discard.assign(1, deck[static_cast<std::size_t>(seats) * 7]);
    m_draw.assign(deck.begin() + seats * 7 + 1, deck.end());
    m_draw_pos = 0;

    m_current = 0;
    m_direction = 1;
    m_pending = 0;
    m_drew = false;
    apply_opening();
    return true;
}

// 引牌是功能牌时按功能生效：
//   SK      -> 跳过庄家（0 号），首出 1
//   RV      -> 三人以上反转方向，仍由 0 号出；两人局等同 SK
//   D2 / W4 -> 0 号面前挂罚抽，必须 draw 吃下后被跳过
//   W       -> 由庄家选色（取他手里最多的颜色）
void Round::apply_opening() {
    const Card top = m_discard.back();

    if (top.face == Face::Wild) {
        m_color = dominant_color(0);
        m_current = 0;
        return;
    }
    if (top.face == Face::WildDrawFour) {
        m_color = dominant_color(0);
        m_pending = 4;
        m_current = 0;
        return;
    }

    m_color = top.color;
    switch (top.face) {
    case Face::Skip:
        m_current = 1 % m_seats;
        break;
    case Face::Reverse:
        if (m_seats == 2) {
            m_current = 1;
        } else {
            m_direction = -1;
            m_current = 0;
        }
        break;
    case Face::DrawTwo:
        m_pending = 2;
        m_current = 0;
        break;
    default:
        m_current = 0;
        break;
    }
}

Color Round::dominant_color(int seat) const {
    int counts[4] = {0, 0, 0, 0};
    for (const Card& card : m_hands[static_cast<std::size_t>(seat)]) {
        if (is_wild(card)) continue;
        counts[static_cast<int>(card.color)] += 1;
    }
    int best = 0;
    for (int i = 1; i < 4; ++i)
        if (counts[i] > counts[best]) best = i;
    return static_cast<Color>(best);
}

Round::View Round::view() const {
    View view;
    view.current = m_current;
    view.top = m_discard.empty() ? std::string() : card_name(m_discard.back());
    view.color = color_name(m_color);
    view.direction = m_direction;
    view.pending = m_pending;
    return view;
}

const std::vector<Card>& Round::hand(int seat) const {
    return m_hands.at(static_cast<std::size_t>(seat));
}

int Round::step(int from, int count) const {
    return ((from + m_direction * count) % m_seats + m_seats) % m_seats;
}

Round::PlayResult Round::play(int seat, const std::vector<std::string>& cards,
                              const std::string& color) {
    PlayResult result;
    if (seat < 0 || seat >= m_seats) {
        result.error = "invalid seat";
        return result;
    }
    if (seat != m_current) {
        result.error = "not your turn";
        return result;
    }
    if (m_pending > 0) {
        result.error = "draw the penalty first";
        return result;
    }
    if (cards.size() != 1) {
        result.error = cards.empty() ? "missing card" : "play one card per turn";
        return result;
    }

    Card card;
    if (!parse_card(cards.front(), card)) {
        result.error = "unknown card: " + cards.front();
        return result;
    }

    auto& hand = m_hands[static_cast<std::size_t>(seat)];
    const auto found = std::find(hand.begin(), hand.end(), card);
    if (found == hand.end()) {
        result.error = "not in your hand: " + cards.front();
        return result;
    }
    if (m_drew && !(card == m_drawn)) {
        result.error = "you may only play the card you drew";
        return result;
    }

    Color chosen = m_color;
    if (is_wild(card)) {
        if (color.empty()) {
            result.error = "a wild card needs a color";
            return result;
        }
        if (!parse_color(color, chosen)) {
            result.error = "unknown color: " + color;
            return result;
        }
    } else if (!color.empty()) {
        result.error = "only wild cards take a color";
        return result;
    }

    const Card top = m_discard.back();
    if (!is_wild(card) && card.color != m_color && card.face != top.face) {
        result.error = "does not match " + card_name(top);
        return result;
    }

    hand.erase(found);
    m_discard.push_back(card);
    // 有效颜色：万能牌用指定颜色，普通牌用这张牌自己的颜色。
    // 规则是「万能牌指定的颜色一直生效到下一次打出非万能牌」，所以普通牌
    // 必须把颜色刷新成自己，否则后面同色的牌会被误判为不匹配。
    m_color = is_wild(card) ? chosen : card.color;
    m_drew = false;

    result.ok = true;
    result.cards.push_back(card);
    result.color = color; // 原样回显，测试会核对 "blue" 这种写法

    switch (card.face) {
    case Face::Skip:
        m_current = step(seat, 2);
        break;
    case Face::Reverse:
        if (m_seats == 2) {
            m_current = seat; // 两人局 RV 等同 SK：还是自己出
        } else {
            m_direction = -m_direction;
            m_current = step(seat, 1);
        }
        break;
    case Face::DrawTwo:
        m_pending += 2;
        m_current = step(seat, 1);
        break;
    case Face::WildDrawFour:
        m_pending += 4;
        m_current = step(seat, 1);
        break;
    default:
        m_current = step(seat, 1);
        break;
    }

    if (hand.empty()) {
        settle(seat, result);
        return result;
    }
    result.next = view();
    return result;
}

void Round::settle(int winner, PlayResult& result) {
    result.over = true;
    result.winner = winner;
    int points = 0;
    for (int seat = 0; seat < m_seats; ++seat) {
        if (seat == winner) continue;
        for (const Card& card : m_hands[static_cast<std::size_t>(seat)])
            points += card_value(card);
    }
    result.points = points;
    result.next = view();
}

bool Round::draw_cards(int seat, int count, std::vector<Card>& out,
                       std::string& error) {
    const std::size_t remaining = m_draw.size() - m_draw_pos;
    if (remaining < static_cast<std::size_t>(count)) {
        error = "the draw pile is empty";
        return false;
    }
    for (int i = 0; i < count; ++i) {
        const Card card = m_draw[m_draw_pos++];
        m_hands[static_cast<std::size_t>(seat)].push_back(card);
        out.push_back(card);
    }
    return true;
}

Round::DrawResult Round::draw(int seat) {
    DrawResult result;
    result.player = seat;
    if (seat < 0 || seat >= m_seats) {
        result.error = "invalid seat";
        return result;
    }
    if (seat != m_current) {
        result.error = "not your turn";
        return result;
    }
    // 一回合只能摸一次：摸过之后只能出刚摸到的那张牌，或者 pass。
    if (m_drew) {
        result.error = "you have already drawn this turn";
        return result;
    }

    const int count = m_pending > 0 ? m_pending : 1;
    std::string error;
    if (!draw_cards(seat, count, result.cards, error)) {
        result.error = error;
        return result;
    }
    result.ok = true;
    result.count = count;

    if (m_pending > 0) {
        // 吃下罚抽：清零、跳过本人、轮到下家。
        m_pending = 0;
        m_current = step(seat, 1);
    } else {
        // 普通摸牌：仍由本人决定是出刚摸的这张，还是 pass。
        m_drew = true;
        m_drawn = result.cards.back();
    }
    result.next = view();
    return result;
}

Round::TurnResult Round::pass(int seat) {
    TurnResult result;
    if (seat < 0 || seat >= m_seats) {
        result.error = "invalid seat";
        return result;
    }
    if (seat != m_current) {
        result.error = "not your turn";
        return result;
    }
    if (!m_drew) {
        result.error = "you must draw before passing";
        return result;
    }
    m_drew = false;
    m_current = step(seat, 1);
    result.ok = true;
    result.next = view();
    return result;
}

Round::UnoResult Round::uno(int seat) {
    UnoResult result;
    result.player = seat;
    if (seat < 0 || seat >= m_seats) {
        result.error = "invalid seat";
        return result;
    }
    if (m_hands[static_cast<std::size_t>(seat)].size() == 1) {
        result.ok = true;
        result.legal = true;
        return result;
    }

    std::string error;
    std::vector<Card> drawn;
    if (!draw_cards(seat, 2, drawn, error)) {
        result.error = error;
        return result;
    }
    result.ok = true;
    result.legal = false;
    result.penalty = 2;
    return result;
}

} // 命名空间 uno
