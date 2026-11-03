#include "host_session.h"
#include <algorithm>
#include <cctype>
#include <utility>
#include "basic/util.h"
namespace uno {

HostSession::HostSession(std::string name, std::istream& in, std::ostream& out)
    : Session(Role::Host, std::move(name), in, out) {
    add_lobby_command("start", "start",
                      "start the game and deal the cards (host only)",
                      [this](const Args& args) { return start(args); });
    add_lobby_command("kick", "kick <player_id|player_name>",
                      "remove a player from the room (host only)",
                      [this](const Args& args) { return kick(args); }, 1, 1);

    // 房主自己就是 0 号玩家：开局时它和客户端一样拿 7 张牌，只是没有连接。
    m_players.push_back(Player{session_name(), PeerId{}});
}

HostSession::~HostSession() = default;

// ---------------------------------------------------------------------------
// 传输初始化。这段是框架的一部分：房间路径推导、残留 socket 清理、权限、
// 非阻塞设置都已经处理好，你不需要改动。
// ---------------------------------------------------------------------------
bool HostSession::listen_room(const std::string& room, std::string& err) {
    const std::string path = room_socket_path(room, err);
    if (path.empty()) return false;

    auto server = std::make_unique<UnixServer>();
    if (!server->listen_path(path, err)) return false;

    m_room_path = path;
    m_server = std::move(server);
    loop().set_server(m_server.get());
    return true;
}

bool HostSession::listen_tcp(int port, std::string& err) {
    auto server = std::make_unique<TcpServer>();
    if (!server->listen(port, err)) return false;

    m_server = std::move(server);
    loop().set_server(m_server.get());
    return true;
}

// ---------------------------------------------------------------------------
// 事件钩子：这些是你要实现的部分。
// ---------------------------------------------------------------------------
void HostSession::on_peer_join(PeerId) {
    // 连接刚建立时还不知道名字：客户端的第一条消息才是 JOIN，
    // 座位在 handle_join() 里分配，所以这里什么都不用做。
}

void HostSession::on_peer_message(PeerId from, const Message& message) {
    //info("TODO(host): handle " + message.type + " from peer " + from.str());
    if (message.type == "JOIN") {
	handle_join(from, message);
	return;
    }
    if (message.type == "RENAME") {
	handle_rename(from, message);
	return;
    }
    if (message.type == "LEAVE") {
	handle_leave(from);
	return;
    }
    // 阶段二：出牌、摸牌、跳过、喊 UNO 都交给同一个规则引擎。
    if (message.type == "PLAY") {
	handle_play(from, message);
	return;
    }
    if (message.type == "DRAW") {
	handle_draw(from);
	return;
    }
    if (message.type == "PASS") {
	handle_pass(from);
	return;
    }
    if (message.type == "UNO") {
	handle_uno(from, message);
	return;
    }
    // 阶段三才有：+4 质疑与 7 换牌。
    if (message.type == "CHALLENGE" || message.type == "SWAP") {
	send_error(from, message.type + ": not implemented yet");
	return;
    }
    send_error(from, "unknown message: " + message.type);
}

void HostSession::on_peer_leave(PeerId who) {
   /* info("TODO(host): peer " + who.str() +
         " disconnected -- drop it from the player list");*/
  const int seat = seat_of(who);
  if (seat < 0) return; // 连上但没 JOIN：名单没有变化，不广播
  info(m_players[seat].name + " left the room");

  // 一局进行中出现离开（断线 / 被踢）：他的手牌和回合位置都不在了，
  // 这一局不可能正确继续，直接作废。
  //   * 先广播 ABORTED，客户端由框架切回大厅；
  //   * 主机自己 return_to_lobby()，命令表恢复成 start / kick。
  if (phase() == Phase::Game) {
	broadcast_to_players(Message("ABORTED"));
	m_round.reset();
	return_to_lobby();
	info("round aborted: a player left");
  }

  m_players.erase(m_players.begin() + seat);
  m_seat_of.erase(who);
  reseat_from(seat);
  broadcast_players();

}
// ---------------------------------------------------------------------------
// 大厅：名单维护
// ---------------------------------------------------------------------------
int HostSession::seat_of(PeerId who) const {
	const auto it = m_seat_of.find(who);
	return it == m_seat_of.end() ? -1 : it->second;
}

HostSession::Player* HostSession::player_by_arg(const std::string& arg) {
	// 纯数字按座位号解释（id），否则按名字精确匹配；重名时取座位号最小的。
	const bool numeric =
	!arg.empty() && std::all_of(arg.begin(), arg.end(),
	[](unsigned char c) {
	return std::isdigit(c) != 0;
	});
	if (numeric) {
	try {
	const int seat = std::stoi(arg);
	if (seat >= 0 && seat < static_cast<int>(m_players.size()))
	return &m_players[static_cast<std::size_t>(seat)];
	} catch (...) {
	}
	return nullptr;
	}
	for (Player& player : m_players)
	if (player.name == arg) return &player;
	return nullptr;
}

Message HostSession::welcome_message(int seat) const {
	Message message("WELCOME");
	message.set("id", seat);
	message.set("name", m_players[static_cast<std::size_t>(seat)].name);
	return message;
}

Message HostSession::players_message() const {
	Message message("PLAYERS");
	for (int seat = 0; seat < static_cast<int>(m_players.size()); ++seat)
	message.set(std::to_string(seat),
	m_players[static_cast<std::size_t>(seat)].name);
	message.set("count", static_cast<int>(m_players.size()));
	return message;
}
void HostSession::broadcast_players() { broadcast(players_message()); }

void HostSession::broadcast_to_players(Message message) {
	for (const Player& player : m_players)
	if (player.peer.valid()) send_to(player.peer, message);
}
void HostSession::reseat_from(int first_seat) {
	for (int seat = first_seat; seat < static_cast<int>(m_players.size());
	++seat) {
	const PeerId peer = m_players[static_cast<std::size_t>(seat)].peer;
	if (!peer.valid()) continue;
	m_seat_of[peer] = seat;
	// 座位号变了必须让本人知道：WELCOME 的语义就是「你坐在哪」。
	send_to(peer, welcome_message(seat));
}
}

bool HostSession::send_error(PeerId who, const std::string& text) {
	Message message("ERROR");
	message.set("msg", text);
	return send_to(who, std::move(message));
}
void HostSession::handle_join(PeerId from, const Message& message) {
	if (phase() != Phase::Lobby) {
	send_error(from, "JOIN: a game is already running");
	return;
	}
	if (seat_of(from) >= 0) {
	send_error(from, "JOIN: you have already joined");
	return;
	}
	const std::string name = trim(message.get("name"));
	if (name.empty()) {
	send_error(from, "JOIN: missing field \"name\"");
	return;
}
	const int seat = static_cast<int>(m_players.size());
	m_players.push_back(Player{name, from});
	m_seat_of[from] = seat;
	send_to(from, welcome_message(seat)); // 先告诉本人座位
	broadcast_players(); // 再向所有人同步名单
	info(name + " joined as seat " + std::to_string(seat));
}

void HostSession::handle_rename(PeerId from, const Message& message) {
	const int seat = seat_of(from);
	if (seat < 0) {
	send_error(from, "RENAME: join the room first");
	return;
	}
	if (phase() != Phase::Lobby) {
	send_error(from, "RENAME: only allowed in the lobby");
	return;
	}
	const std::string name = trim(message.get("name"));
	if (name.empty()) {
	send_error(from, "RENAME: missing field \"name\"");
	return;
}
	info(m_players[static_cast<std::size_t>(seat)].name + " renamed to " + name);
	m_players[static_cast<std::size_t>(seat)].name = name;
	broadcast_players();
}

// 客户端在游戏里执行 exit / quit：它想退出本局，但连接还在、人还在房间里。
// 一局少了一个玩家就没法正确继续，所以这里和「玩家掉线」一样中止本局。
void HostSession::handle_leave(PeerId from) {
	const int seat = seat_of(from);
	if (seat < 0) {
	send_error(from, "LEAVE: join the room first");
	return;
	}
	if (phase() != Phase::Game) {
	send_error(from, "LEAVE: no game is running");
	return;
	}
	info(m_players[static_cast<std::size_t>(seat)].name + " left the round");
	broadcast_to_players(Message("ABORTED"));
	m_round.reset();
	return_to_lobby();
}

// ---------------------------------------------------------------------------
// 阶段二：把 Round 的结果翻译成协议消息
// ---------------------------------------------------------------------------
std::string HostSession::hand_text(int seat) const {
    if (!m_round) return {};
    return join_card_list(m_round->hand(seat));
}

// HAND 是私有信息：只发给本人。房主没有连接，手牌通过 cards 命令打印。
void HostSession::send_hand(int seat) {
    if (!m_round) return;
    if (seat < 0 || seat >= static_cast<int>(m_players.size())) return;
    const PeerId peer = m_players[static_cast<std::size_t>(seat)].peer;
    if (!peer.valid()) return;

    Message message("HAND");
    message.set("cards", hand_text(seat));
    send_to(peer, std::move(message));
}

void HostSession::broadcast_turn() {
    if (!m_round) return;
    const Round::View view = m_round->view();
    Message message("TURN");
    message.set("player", view.current);
    message.set("top", view.top);
    message.set("direction", view.direction);
    message.set("pending", view.pending);
    message.set("color", view.color);
    broadcast_to_players(message);
}

// 房主自己敲的命令没有连接，错误打印到终端；客户端的错误只回给他本人。
void HostSession::report(PeerId to, const std::string& text) {
    if (to.valid()) send_error(to, text);
    else error(text);
}

void HostSession::play_at(PeerId from, int seat, const std::string& cards,
                          const std::string& color) {
    if (!m_round) {
        report(from, "PLAY: no game is running");
        return;
    }

    const Round::PlayResult result =
        m_round->play(seat, split_card_list(cards), color);
    if (!result.ok) {
        report(from, "PLAY: " + result.error);
        return;
    }

    Message played("PLAYED");
    played.set("player", seat);
    played.set("cards", join_card_list(result.cards));
    if (!result.color.empty()) played.set("color", result.color);
    broadcast_to_players(played);

    if (result.over) {
        finish_round(result.winner, result.points);
        return;
    }
    send_hand(seat);
    broadcast_turn();
}

void HostSession::draw_at(PeerId from, int seat) {
    if (!m_round) {
        report(from, "DRAW: no game is running");
        return;
    }

    const Round::DrawResult result = m_round->draw(seat);
    if (!result.ok) {
        report(from, "DRAW: " + result.error);
        return;
    }

    Message drawn("DRAWN");
    drawn.set("player", seat);
    drawn.set("count", result.count);
    broadcast_to_players(drawn);
    send_hand(seat);
    broadcast_turn();
}

void HostSession::pass_at(PeerId from, int seat) {
    if (!m_round) {
        report(from, "PASS: no game is running");
        return;
    }

    const Round::TurnResult result = m_round->pass(seat);
    if (!result.ok) {
        report(from, "PASS: " + result.error);
        return;
    }
    broadcast_turn();
}

void HostSession::uno_at(PeerId from, int seat) {
    if (!m_round) {
        report(from, "UNO: no game is running");
        return;
    }

    const Round::UnoResult result = m_round->uno(seat);
    if (!result.ok) {
        report(from, "UNO: " + result.error);
        return;
    }

    if (result.legal) {
        Message message("UNO");
        message.set("player", seat);
        broadcast_to_players(message);
    } else {
        Message message("UNOPENALTY");
        message.set("player", seat);
        message.set("count", result.penalty);
        broadcast_to_players(message);
        send_hand(seat);
    }
}

void HostSession::finish_round(int winner, int points) {
    Message message("GAMEOVER");
    message.set("winner", winner);
    message.set("points", points);
    broadcast_to_players(message);
    m_round.reset();
    return_to_lobby();
}

void HostSession::handle_play(PeerId from, const Message& message) {
    const int seat = seat_of(from);
    if (seat < 0) {
        send_error(from, "PLAY: join the room first");
        return;
    }
    play_at(from, seat, message.get("cards"), message.get("color"));
}

void HostSession::handle_draw(PeerId from) {
    const int seat = seat_of(from);
    if (seat < 0) {
        send_error(from, "DRAW: join the room first");
        return;
    }
    draw_at(from, seat);
}

void HostSession::handle_pass(PeerId from) {
    const int seat = seat_of(from);
    if (seat < 0) {
        send_error(from, "PASS: join the room first");
        return;
    }
    pass_at(from, seat);
}

void HostSession::handle_uno(PeerId from, const Message& message) {
    const int seat = seat_of(from);
    if (seat < 0) {
        send_error(from, "UNO: join the room first");
        return;
    }
    if (message.has("player")) {
        send_error(from, "UNO: reporting is not implemented yet");
        return;
    }
    uno_at(from, seat);
}

// ---------------------------------------------------------------------------
// 本地命令行：host 玩家自己敲的命令。
// ---------------------------------------------------------------------------
Session::Result HostSession::start(const Args&) {
    if (m_players.size() < 2) {
	error("start needs at least 2 players (currently " +
	std::to_string(m_players.size()) + ")");
	return Result::Continue; // 留在大厅，什么都不改
	}

    // 建一局：洗牌/发牌/引牌，长度不够或牌名非法就留在 Lobby。
    auto round = std::make_unique<Round>();
    std::string round_error;
    const std::vector<std::string>* override_cards =
        has_deck_override() ? &deck_override() : nullptr;
    if (!round->setup(static_cast<int>(m_players.size()), has_seed(), seed(),
                      override_cards, round_error)) {
        error("start: " + round_error);
        return Result::Continue;
    }
    m_round = std::move(round);

    // 手册要求的顺序：先给每人发私有 HAND，再广播 START，最后广播 TURN。
    for (int seat = 0; seat < static_cast<int>(m_players.size()); ++seat)
        send_hand(seat);
    broadcast_to_players(Message("START"));
    broadcast_turn();

    enter_game();
    return Result::StartGame;
}

Session::Result HostSession::kick(const Args& args) {
   // info("TODO(host): remove player " + args[1] + " and broadcast PLAYERS");
   Player* target = player_by_arg(args[1]);
	if (target == nullptr) {
	error("kick: no such player: \"" + args[1] + "\"");
	return Result::Continue;
	}
	if (!target->peer.valid()) {
	error("kick: the host cannot kick itself");
	return Result::Continue;
	}
	// 断开之后 on_peer_leave() 会删名单并向所有人广播 PLAYERS，所以这里
	// 不要自己再删一遍（否则会广播两条）。
	const PeerId peer = target->peer;
	loop().close_peer(peer);
	return Result::Continue; 
        
}

Session::Result HostSession::rename(const Args& args) {
    //info("TODO(host): rename player to " + args[1] + " and broadcast PLAYERS");
    	const std::string name = trim(args[1]);
	if (name.empty()) {
	error("usage: rename <name>");
	return Result::Continue;
	}
	m_players.front().name = name; // 房主永远坐在 0 号
	broadcast_players();
	info("you are now \"" + name + "\"");
	return Result::Continue;
}

Session::Result HostSession::leave_game() {
    // 房主在游戏里执行 exit / quit：中止本局，先通知所有客户端回大厅，
    // 再由 apply(Result::LeaveGame) 把本端切回大厅。
    if (phase() == Phase::Game) broadcast_to_players(Message("ABORTED"));
    m_round.reset();
    return Result::LeaveGame;
}

// 房主自己敲的 play / draw / pass / uno：座位固定是 0，错误打印到本端。
Session::Result HostSession::play(const Args& args) {
    play_at(PeerId{}, 0, args[1], args.size() > 2 ? args[2] : std::string());
    return Result::Continue;
}

Session::Result HostSession::cards(const Args&) {
    if (!m_round) {
        error("cards: no game is running");
        return Result::Continue;
    }
    // cards 是本地命令：直接打印一行 HAND，而不是发协议消息。
    Message message("HAND");
    message.set("cards", hand_text(0));
    out() << message.serialize() << "\n";
    out().flush();
    return Result::Continue;
}

Session::Result HostSession::draw(const Args&) {
    draw_at(PeerId{}, 0);
    return Result::Continue;
}

Session::Result HostSession::pass(const Args&) {
    pass_at(PeerId{}, 0);
    return Result::Continue;
}

Session::Result HostSession::uno(const Args& args) {
    if (args.size() > 1) {
        info("UNO player=<id>: reporting is not implemented yet");
        return Result::Continue;
    }
    uno_at(PeerId{}, 0);
    return Result::Continue;
}

Session::Result HostSession::challenge(const Args& args) {
    if (!valid_challenge_answer(args)) return Result::Continue;
    info("TODO(host): resolve challenge answer=" + args[1]);
    return Result::Continue;
}

Session::Result HostSession::swap(const Args& args) {
    info("TODO(host): swap hands with player " + args[1]);
    return Result::Continue;
}

} // 命名空间 uno
