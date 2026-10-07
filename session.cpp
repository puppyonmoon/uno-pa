#include "session.h"

#include <iostream>
#include <utility>

#include "basic/util.h"

namespace uno {

Session::Session(Role role, std::string name, std::istream& in, std::ostream& out)
    : m_name(std::move(name)), m_role(role), m_in(in), m_out(out),
      m_stdin_is_console(&in == &std::cin) {
    // -----------------------------------------------------------------
    // 大厅阶段：host 与 client 共有的命令。start / kick 由 HostSession
    // 在自己的构造函数里追加。
    // -----------------------------------------------------------------
    add_lobby_command("help", "help", "show this help",
                      [this](const Args&) {
                          print_help();
                          return Result::Continue;
                      });
    add_lobby_command("rename", "rename <name>", "change your own name",
                      [this](const Args& args) { return rename(args); }, 1, 1);
    add_lobby_command("exit", "exit", "leave the lobby and quit the program",
                      [](const Args&) { return Result::Quit; });
    add_lobby_command("quit", "quit", "same as exit",
                      [](const Args&) { return Result::Quit; });

    // -----------------------------------------------------------------
    // 游戏阶段：命令名对 host 和 client 相同，行为由各自的虚函数实现。
    // -----------------------------------------------------------------
    add_game_command("help", "help", "show this help",
                     [this](const Args&) {
                         print_help();
                         return Result::Continue;
                     });
    add_game_command("play", "play <card>[,<card>...] [color]",
                     "play one or more cards",
                     [this](const Args& args) { return play(args); }, 1, 2);
    add_game_command("cards", "cards", "show your hand",
                     [this](const Args& args) { return cards(args); }, 0, 0);
    add_game_command("draw", "draw", "draw one card",
                     [this](const Args& args) { return draw(args); }, 0, 0);
    add_game_command("pass", "pass",
                     "pass after drawing or when unable to play",
                     [this](const Args& args) { return pass(args); }, 0, 0);
    add_game_command("uno", "uno [player_id]",
                     "declare UNO, or report that player_id forgot to declare it",
                     [this](const Args& args) { return uno(args); }, 0, 1);
    add_game_command("challenge", "challenge yes|no",
                     "answer a Wild Draw Four challenge",
                     [this](const Args& args) { return challenge(args); }, 1, 1);
    add_game_command("swap", "swap <player_id>", "swap hands after playing a 7",
                     [this](const Args& args) { return swap(args); }, 1, 1);
    add_game_command("exit", "exit",
                     "abort this round and go back to the lobby",
                     [](const Args&) { return Result::LeaveGame; });
    add_game_command("quit", "quit", "same as exit",
                     [](const Args&) { return Result::LeaveGame; });

    // -----------------------------------------------------------------
    // 事件循环接线：框架负责「什么时候有事件」，子类负责「事件是什么」。
    // -----------------------------------------------------------------
    m_loop.on_join([this](PeerId who) {
        begin_output();
        on_peer_join(who);
    });
    m_loop.on_line([this](PeerId from, const std::string& line) {
        begin_output();
        deliver_peer_line(from, line);
    });
    m_loop.on_leave([this](PeerId who) {
        begin_output();
        on_peer_leave(who);
    });
    m_loop.on_stdin([this](const std::string& line) {
        begin_output();
        if (handle_line(line) == Result::Quit) m_loop.request_stop();
    });
    m_loop.on_idle([this] { show_prompt(); });
}

void Session::add_lobby_command(std::string name, std::string usage,
                                std::string help, Handler handler,
                                std::size_t min_args, std::size_t max_args) {
    m_lobby_commands[to_lower(name)] =
        Command{std::move(usage), std::move(help), std::move(handler),
                min_args, max_args};
}

void Session::add_game_command(std::string name, std::string usage,
                               std::string help, Handler handler,
                               std::size_t min_args, std::size_t max_args) {
    m_game_commands[to_lower(name)] =
        Command{std::move(usage), std::move(help), std::move(handler),
                min_args, max_args};
}

bool Session::has_lobby_command(const std::string& name) const {
    return m_lobby_commands.find(to_lower(name)) != m_lobby_commands.end();
}

bool Session::has_game_command(const std::string& name) const {
    return m_game_commands.find(to_lower(name)) != m_game_commands.end();
}

Session::Result Session::handle_line(const std::string& line) {
    const Args words = split_words(line);
    if (words.empty()) return Result::Continue;

    const auto& commands = current_commands();
    const auto it = commands.find(to_lower(words[0]));
    if (it == commands.end()) {
        error("Unknown command: " + words[0] +
              " (type 'help' for the command list)");
        return Result::Continue;
    }

    const Command& command = it->second;
    if (!command.handler) {
        error(words[0] + ": not available to this player");
        return Result::Continue;
    }

    const std::size_t argc = words.size() - 1;
    if (argc < command.min_args || argc > command.max_args) {
        error("usage: " + command.usage);
        return Result::Continue;
    }

    const Result result = command.handler(words);
    apply(result);
    return result;
}

void Session::deliver_peer_line(PeerId from, const std::string& line) {
    if (line.empty()) return;

    const Message message = Message::parse(line);
    if (message.type.empty()) return;

    // 回显：玩家能看到主机/对端说了什么，测试与日志也依赖它。
    m_out << message.serialize() << "\n";
    m_out.flush();

    // 客户端没有别的信息来源，所以 START/GAMEOVER/ABORTED 由框架搬运阶段；
    // host 自己决定什么时候开局（start 命令里调用 enter_game()）。
    if (m_role == Role::Client) {
        if (message.type == "START") {
            enter_game();
        } else if (message.type == "GAMEOVER" || message.type == "ABORTED") {
            return_to_lobby();
        }
    }

    on_peer_message(from, message);
}

int Session::run() {
    if (m_stdin_is_console) m_loop.watch_stdin(0);
    return m_loop.run();
}

bool Session::send_to(PeerId who, Message message) {
    return m_loop.send(who, message.serialize());
}

void Session::broadcast(Message message) {
    m_loop.send_all(message.serialize());
}

void Session::enter_game() {
    if (m_phase != Phase::Game) m_phase = Phase::Game;
}

void Session::return_to_lobby() {
    if (m_phase != Phase::Lobby) m_phase = Phase::Lobby;
}

const std::map<std::string, Session::Command>&
Session::current_commands() const {
    return m_phase == Phase::Lobby ? m_lobby_commands : m_game_commands;
}

const char* Session::current_table_name() const {
    return m_phase == Phase::Lobby ? "Lobby" : "Game";
}

const char* Session::current_prompt() const {
    return m_phase == Phase::Lobby ? "uno(lobby)> " : "uno(game)> ";
}

void Session::print_help() const {
    m_out << current_table_name() << " commands:\n";
    for (const auto& entry : current_commands())
        m_out << "  " << entry.second.usage << " - " << entry.second.help
              << "\n";
    m_out.flush();
}

void Session::info(const std::string& message) const {
    m_out << Message("INFO").set("msg", message).serialize() << "\n";
    m_out.flush();
}

void Session::error(const std::string& message) const {
    m_out << Message("ERROR").set("msg", message).serialize() << "\n";
    m_out.flush();
}

bool Session::valid_challenge_answer(const Args& args) const {
    if (args.size() < 2 || (args[1] != "yes" && args[1] != "no")) {
        error("usage: challenge yes|no");
        return false;
    }
    return true;
}

void Session::begin_output() {
    if (!m_prompt_pending) return;
    m_out << "\n";
    m_prompt_pending = false;
}

void Session::show_prompt() {
    if (!m_prompt || m_prompt_pending) return;
    m_out << current_prompt();
    m_out.flush();
    m_prompt_pending = true;
}

void Session::apply(Result result) {
    switch (result) {
    case Result::StartGame:
        if (m_phase == Phase::Lobby) m_phase = Phase::Game;
        break;
    case Result::LeaveGame:
    case Result::RoundOver:
        m_phase = Phase::Lobby;
        break;
    case Result::Quit:
    case Result::Continue:
        break;
    }
}

} // 命名空间 uno
