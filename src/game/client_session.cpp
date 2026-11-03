#include "client_session.h"

#include <utility>

#include "game/card.h"

namespace uno {

namespace {

std::string join_names(const std::vector<std::string>& names) {
    std::string text;
    for (std::size_t i = 0; i < names.size(); ++i) {
        if (i != 0) text += ",";
        text += names[i];
    }
    return text;
}

} // 匿名命名空间

ClientSession::ClientSession(std::string name, std::istream& in,
                             std::ostream& out)
    : Session(Role::Client, std::move(name), in, out) {
    // 大厅命令表沿用基类的 rename / help / exit / quit；client 没有
    // start / kick。
}

ClientSession::~ClientSession() = default;

// ---------------------------------------------------------------------------
// 传输初始化。这一段是框架的一部分：房间路径推导与连接、非阻塞设置都由
// listen_room()/connect_tcp() 完成，你不需要改动。
// ---------------------------------------------------------------------------
bool ClientSession::join_room(const std::string& room, std::string& err) {
    const std::string path = room_socket_path(room, err);
    if (path.empty()) return false;

    auto connection = std::make_unique<UnixConnection>();
    if (!connection->connect(path)) {
        err = "could not join room '" + room + "' (" + path + ")";
        return false;
    }

    m_host = loop().adopt(std::move(connection));
    on_connected();
    return true;
}

bool ClientSession::connect_tcp(const std::string& host, int port,
                                std::string& err) {
    auto connection = std::make_unique<TcpConnection>();
    if (!connection->connect(host, port)) {
        err = "could not connect to " + host + ":" + std::to_string(port);
        return false;
    }

    m_host = loop().adopt(std::move(connection));
    on_connected();
    return true;
}

bool ClientSession::send_to_host(Message message) {
    if (!m_host.valid()) {
        error("not connected to a host");
        return false;
    }
    if (!send_to(m_host, std::move(message))) {
        error("the connection to the host is gone");
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// 事件钩子：这些是你要实现的部分。
// ---------------------------------------------------------------------------
void ClientSession::on_connected() {
    //info("TODO(client): send JOIN name=" + session_name());
    Message join("JOIN");
    join.set("name", session_name());
    send_to_host(std::move(join));
}

void ClientSession::on_peer_join(PeerId) {
    // 客户端只有一个对端，框架在 join_room()/connect_tcp() 里已经登记好，
    // 这里不会发生什么。
}

void ClientSession::on_peer_message(PeerId, const Message& message) {
    //info("TODO(client): update the local view from " + message.type);
    if (message.type == "WELCOME") {
	m_seat = message.get_int("id", m_seat);
	m_display_name = message.get("name", session_name());
	return;
	}
	if (message.type == "PLAYERS") {
	m_roster.clear();
	const int count = message.get_int("count", 0);
	for (int seat = 0; seat < count; ++seat) {
	const std::string key = std::to_string(seat);
	if (message.has(key)) m_roster[seat] = message.get(key);
	}
	const auto it = m_roster.find(m_seat);
	if (it != m_roster.end()) m_display_name = it->second;
	return;
}

    // 牌桌镜像：只用于显示，合法性与胜负都由主机判定。
    if (message.type == "HAND") {
	m_hand = split_card_list(message.get("cards"));
	return;
    }
    if (message.type == "TURN") {
	m_current = message.get_int("player", m_current);
	m_top = message.get("top", m_top);
	m_direction = message.get_int("direction", m_direction);
	m_pending = message.get_int("pending", m_pending);
	m_color = message.get("color", m_color);
	return;
    }
    if (message.type == "PLAYED") {
	m_top = message.get("cards", m_top);
	if (message.has("color")) m_color = message.get("color");
	return;
    }
    if (message.type == "GAMEOVER" || message.type == "ABORTED") {
	m_hand.clear();
	m_top.clear();
	m_color.clear();
	m_current = -1;
	m_pending = 0;
	return;
    }
}

void ClientSession::on_peer_leave(PeerId) {
    //info("TODO(client): the host went away -- report it and return to the lobby");
    info("the host closed the connection");
	m_seat = -1;
	m_display_name.clear();
	m_roster.clear();
	m_hand.clear();
	m_top.clear();
	m_color.clear();
	m_current = -1;
	m_pending = 0;
	return_to_lobby();
	// 主机不在了，客户端独自留在本地大厅没有任何意义：直接干净退出。
	// 这里是事件回调而不是命令，所以用 request_stop() 结束事件循环。
	loop().request_stop();
}

// ---------------------------------------------------------------------------
// 命令 -> 协议消息：协议由框架固定，这里已经写好，你不需要改动。
// ---------------------------------------------------------------------------
Session::Result ClientSession::rename(const Args& args) {
    Message message("RENAME");
    message.set("name", args[1]);
    send_to_host(std::move(message));
    return Result::Continue;
}

Session::Result ClientSession::play(const Args& args) {
    Message message("PLAY");
    message.set("cards", args[1]);
    if (args.size() > 2) message.set("color", args[2]);
    send_to_host(std::move(message));
    return Result::Continue;
}

Session::Result ClientSession::cards(const Args&) {
    // cards 是本地命令：把镜像里的手牌打印成一行 HAND（不是协议消息）。
    Message message("HAND");
    message.set("cards", join_names(m_hand));
    out() << message.serialize() << "\n";
    out().flush();
    return Result::Continue;
}

Session::Result ClientSession::draw(const Args&) {
    send_to_host(Message("DRAW"));
    return Result::Continue;
}

Session::Result ClientSession::pass(const Args&) {
    send_to_host(Message("PASS"));
    return Result::Continue;
}

Session::Result ClientSession::uno(const Args& args) {
    Message message("UNO");
    if (args.size() > 1) message.set("player", args[1]);
    send_to_host(std::move(message));
    return Result::Continue;
}

Session::Result ClientSession::challenge(const Args& args) {
    if (!valid_challenge_answer(args)) return Result::Continue;
    Message message("CHALLENGE");
    message.set("accept", args[1]);
    send_to_host(std::move(message));
    return Result::Continue;
}

Session::Result ClientSession::swap(const Args& args) {
    Message message("SWAP");
    message.set("player", args[1]);
    send_to_host(std::move(message));
    return Result::Continue;
}

Session::Result ClientSession::leave_game() {
    // 告诉主机「本局我退出」，主机会中止本局并广播 ABORTED。
    // 连接不断开：回到大厅后仍在这个房间里，可以被下一局重新拉进去。
    send_to_host(Message("LEAVE"));
    return Result::LeaveGame;
}

} // 命名空间 uno
