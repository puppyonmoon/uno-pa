#include "game/card.h"

#include <cctype>

namespace uno {

namespace {

int number_of(Face face) {
    const int value = static_cast<int>(face) - static_cast<int>(Face::Zero);
    return (value >= 0 && value <= 9) ? value : -1;
}

} // 匿名命名空间

bool operator==(const Card& a, const Card& b) {
    return a.color == b.color && a.face == b.face;
}

std::string color_name(Color color) {
    switch (color) {
    case Color::Red: return "R";
    case Color::Yellow: return "Y";
    case Color::Green: return "G";
    case Color::Blue: return "B";
    case Color::Wild: return "W";
    }
    return "W";
}

std::string card_name(const Card& card) {
    if (card.face == Face::Wild) return "W";
    if (card.face == Face::WildDrawFour) return "W4";

    const std::string color = color_name(card.color);
    const int number = number_of(card.face);
    if (number >= 0) return color + std::to_string(number);
    switch (card.face) {
    case Face::Skip: return color + "SK";
    case Face::Reverse: return color + "RV";
    case Face::DrawTwo: return color + "D2";
    default: break;
    }
    return color;
}

bool parse_card(const std::string& text, Card& out) {
    if (text == "W") {
        out = Card{Color::Wild, Face::Wild};
        return true;
    }
    if (text == "W4") {
        out = Card{Color::Wild, Face::WildDrawFour};
        return true;
    }
    if (text.size() < 2) return false;

    Color color;
    switch (text[0]) {
    case 'R': color = Color::Red; break;
    case 'Y': color = Color::Yellow; break;
    case 'G': color = Color::Green; break;
    case 'B': color = Color::Blue; break;
    default: return false;
    }

    const std::string face = text.substr(1);
    if (face == "SK") {
        out = Card{color, Face::Skip};
        return true;
    }
    if (face == "RV") {
        out = Card{color, Face::Reverse};
        return true;
    }
    if (face == "D2") {
        out = Card{color, Face::DrawTwo};
        return true;
    }
    if (face.size() == 1 && face[0] >= '0' && face[0] <= '9') {
        out = Card{color, static_cast<Face>(static_cast<int>(Face::Zero) +
                                           (face[0] - '0'))};
        return true;
    }
    return false;
}

bool parse_color(const std::string& text, Color& out) {
    std::string lower;
    lower.reserve(text.size());
    for (char c : text)
        lower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    if (lower == "r" || lower == "red") { out = Color::Red; return true; }
    if (lower == "y" || lower == "yellow") { out = Color::Yellow; return true; }
    if (lower == "g" || lower == "green") { out = Color::Green; return true; }
    if (lower == "b" || lower == "blue") { out = Color::Blue; return true; }
    return false;
}

bool is_wild(const Card& card) { return card.color == Color::Wild; }

bool is_number(const Card& card) {
    return !is_wild(card) && number_of(card.face) >= 0;
}

int card_value(const Card& card) {
    if (is_wild(card)) return 50;
    if (card.face == Face::Skip || card.face == Face::Reverse ||
        card.face == Face::DrawTwo)
        return 20;
    const int number = number_of(card.face);
    return number >= 0 ? number : 0;
}

std::vector<Card> make_deck() {
    std::vector<Card> deck;
    deck.reserve(108);

    for (Color color : {Color::Red, Color::Yellow, Color::Green, Color::Blue}) {
        deck.push_back(Card{color, Face::Zero});
        for (int number = 1; number <= 9; ++number) {
            const Card card{color, static_cast<Face>(static_cast<int>(Face::Zero) +
                                                     number)};
            deck.push_back(card);
            deck.push_back(card);
        }
        for (Face face : {Face::Skip, Face::Reverse, Face::DrawTwo}) {
            deck.push_back(Card{color, face});
            deck.push_back(Card{color, face});
        }
    }
    for (int i = 0; i < 4; ++i) deck.push_back(Card{Color::Wild, Face::Wild});
    for (int i = 0; i < 4; ++i)
        deck.push_back(Card{Color::Wild, Face::WildDrawFour});
    return deck;
}

std::vector<std::string> split_card_list(const std::string& text) {
    std::vector<std::string> cards;
    std::string current;
    for (char c : text) {
        if (c == ',') {
            if (!current.empty()) cards.push_back(current);
            current.clear();
        } else if (!std::isspace(static_cast<unsigned char>(c))) {
            current += c;
        }
    }
    if (!current.empty()) cards.push_back(current);
    return cards;
}

std::string join_card_list(const std::vector<Card>& cards) {
    std::string text;
    for (std::size_t i = 0; i < cards.size(); ++i) {
        if (i != 0) text += ",";
        text += card_name(cards[i]);
    }
    return text;
}

} // 命名空间 uno
