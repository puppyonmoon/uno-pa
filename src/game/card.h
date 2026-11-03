#pragma once

#include <string>
#include <vector>

namespace uno {

// 牌的颜色与图案。顺序有意义：Color 的 Red/Yellow/Green/Blue 下标就是
// 「选颜色」时的优先级顺序。
enum class Color { Red, Yellow, Green, Blue, Wild };

enum class Face {
    Zero, One, Two, Three, Four, Five, Six, Seven, Eight, Nine,
    Skip, Reverse, DrawTwo, Wild, WildDrawFour,
};

struct Card {
    Color color = Color::Wild;
    Face face = Face::Wild;
};

bool operator==(const Card& a, const Card& b);
inline bool operator!=(const Card& a, const Card& b) { return !(a == b); }

// 牌名："R5" / "RSK" / "RRV" / "RD2" / "W" / "W4"
std::string card_name(const Card& card);
bool parse_card(const std::string& text, Card& out);

// 颜色：同时接受 "R"/"B" 与 "red"/"blue"（大小写不敏感）。
std::string color_name(Color color);
bool parse_color(const std::string& text, Color& out);

bool is_wild(const Card& card);
bool is_number(const Card& card);
int card_value(const Card& card); // 0-9 / 20 / 50

// 108 张牌，顺序固定：颜色 R,Y,G,B；每色 0,1,1,...,9,9,SK,SK,RV,RV,D2,D2；
// 最后 W x4、W4 x4。
std::vector<Card> make_deck();

// "R5,Y3" <-> 牌列表
std::vector<std::string> split_card_list(const std::string& text);
std::string join_card_list(const std::vector<Card>& cards);

} // 命名空间 uno
