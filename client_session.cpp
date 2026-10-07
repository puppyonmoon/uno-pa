#include "client_session.h"

#include <utility>

namespace uno {

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
    info("TODO(client): send JOIN name=" + session_name());
}

void ClientSession::on_peer_join(PeerId) {
    // 客户端只有一个对端，框架在 join_room()/connect_tcp() 里已经登记好，
    // 这里不会发生什么。
}

void ClientSession::on_peer_message(PeerId, const Message& message) {
    info("TODO(client): update the local view from " + message.type);
}

void ClientSession::on_peer_leave(PeerId) {
    info("TODO(client): the host went away -- report it and return to the lobby");
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
    info("TODO(client): print the hand cached from the last HAND message");
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

} // 命名空间 uno
