#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <iostream>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "protocol/loop.h"
#include "protocol/message.h"
#include "protocol/peer.h"

namespace uno {

// ---------------------------------------------------------------------------
// Session -- host/client 两个具体会话的抽象基类，同时直接承担命令层。
//
// 每个会话持有两张命令表：
//     lobby 阶段 -> m_lobby_commands
//     game  阶段 -> m_game_commands
//
// 命令主体是 Session 的虚函数，由 HostSession / ClientSession 实现；表里的
// Handler 捕获 this，调用时虚分发到具体子类。host 与 client 共用的 game
// 命令在基类构造函数里注册，各自独有的大厅命令由子类追加（例如 host 的
// start / kick）。
//
// 框架已经替你完成的事情（不要在这里重复实现一遍网络或并发逻辑）：
//
//   * 事件循环：同时盯着标准输入、监听端点和所有连接，谁就绪处理谁；
//   * 行分帧与断线检测，并在回调触发前完成连接容器的增删；
//   * 出站消息排队：send_to() / broadcast() 只入队，循环在安全时机统一
//     写入，因此你可以在处理一条消息的过程中放心广播；
//   * START / GAMEOVER / ABORTED 在客户端一侧的阶段搬运。
//
// 你要实现的事情：玩家与座位模型、PeerId 到座位的映射、大厅策略、规则
// 引擎与回合状态机，以及「每个玩家应该看到什么」。
// ---------------------------------------------------------------------------
class Session {
public:
    using Args = std::vector<std::string>;

    enum class Phase { Lobby, Game };

    // 这个会话是权威端还是镜像端。它决定 START/GAMEOVER 由谁搬运。
    enum class Role { Host, Client };

    enum class Result {
        Continue,   // 留在当前阶段
        StartGame,  // 大厅 -> 游戏
        LeaveGame,  // 游戏 -> 大厅
        RoundOver,  // 收到 GAMEOVER / ABORTED 后，游戏 -> 大厅
        Quit,       // 退出程序
    };

    virtual ~Session() = default;

    // 命令表里的 Handler 捕获了 this，因此 Session 不可拷贝。
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    // 本地命令行：把一行文本交给当前阶段的命令表；测试也用它驱动。
    Result handle_line(const std::string& line);

    // 推进一次事件循环（timeout_ms = 0 表示不等待）。真实运行由 run() 驱动，
    // 测试可以自己调用它来制造确定的多方交互顺序。
    bool pump(int timeout_ms = 0) { return m_loop.pump(timeout_ms); }

    // 阻塞式主循环：跑事件循环，直到退出或标准输入结束。
    int run();

    Phase phase() const { return m_phase; }
    Role role() const { return m_role; }

    bool has_lobby_command(const std::string& name) const;
    bool has_game_command(const std::string& name) const;

    // 当前还活着的连接。host 侧就是房间里所有客户端；client 侧只有一个。
    std::vector<PeerId> peers() const { return m_loop.peers(); }

    // 确定性洗牌契约。框架只保存并暴露 seed；你实现的牌堆必须在使用 seed
    // 时遵循它，具体算法见实验指导手册。
    void set_seed(std::uint32_t seed) { m_seed = seed; }
    bool has_seed() const { return m_seed.has_value(); }
    std::uint32_t seed() const { return m_seed.value_or(0); }

    // 确定性牌堆覆盖（测试与教学演示用）。
    //
    // 提供之后，start 必须**不洗牌**，严格按给定顺序发牌：座位 0 拿前 7 张，
    // 座位 1 拿接下来 7 张，以此类推，下一张作为参照牌（引牌），其余按顺序
    // 作为摸牌堆。给出覆盖时 seed 被忽略。
    void set_deck_override(std::vector<std::string> cards) {
        m_deck_override = std::move(cards);
    }
    bool has_deck_override() const { return m_deck_override.has_value(); }
    const std::vector<std::string>& deck_override() const {
        return m_deck_override.value();
    }

    std::ostream& out() const { return m_out; }
    bool prompt_enabled() const { return m_prompt; }
    void set_prompt_enabled(bool enabled) { m_prompt = enabled; }

    void print_help() const;
    void info(const std::string& message) const;
    void error(const std::string& message) const;

protected:
    using Handler = std::function<Result(const Args& args)>;

    struct Command {
        std::string usage;
        std::string help;
        Handler handler;
        std::size_t min_args = 0;
        std::size_t max_args = 0;
    };

    Session(Role role, std::string name, std::istream& in, std::ostream& out);

    // ---- 你要实现的事件钩子 -------------------------------------------
    //
    // host 侧：有连接建立 / 某个连接发来一条协议消息 / 某个连接断开。
    // client 侧：对端只有一个（主机），所以 on_peer_join 不会被调用。
    virtual void on_peer_join(PeerId who) = 0;
    virtual void on_peer_message(PeerId from, const Message& message) = 0;
    virtual void on_peer_leave(PeerId who) = 0;

    // client 侧：与主机的连接建立完成。在这里发送你的第一条消息（例如 JOIN）。
    virtual void on_connected() {}

    // ---- 框架提供的投递接口（排队投递，不会写坏正在遍历的容器）--------
    bool send_to(PeerId who, Message message);
    void broadcast(Message message);
    EventLoop& loop() { return m_loop; }

    // 阶段切换。host 的 start 命令应该调用 enter_game()。
    void enter_game();
    void return_to_lobby();

    // host 与 client 共有的命令主体。只由命令表调用，因此不对外公开；
    // HostSession 另外实现自己的 start / kick。
    virtual Result rename(const Args& args) = 0;
    virtual Result play(const Args& args) = 0;
    virtual Result cards(const Args& args) = 0;
    virtual Result draw(const Args& args) = 0;
    virtual Result pass(const Args& args) = 0;
    virtual Result uno(const Args& args) = 0;
    virtual Result challenge(const Args& args) = 0;
    virtual Result swap(const Args& args) = 0;

    // 注册命令。handler 一般写成 [this](const Args& args) { return f(args); }。
    void add_lobby_command(std::string name, std::string usage,
                           std::string help, Handler handler,
                           std::size_t min_args = 0,
                           std::size_t max_args = 0);
    void add_game_command(std::string name, std::string usage,
                          std::string help, Handler handler,
                          std::size_t min_args = 0,
                          std::size_t max_args = 0);

    // challenge 的公共参数校验：不合法时打印 usage 并返回 false。
    bool valid_challenge_answer(const Args& args) const;

    const std::string& session_name() const { return m_name; }

private:
    void apply(Result result);
    void deliver_peer_line(PeerId from, const std::string& line);

    // 提示符显示：出站前先换行收掉旧提示符，空闲时再显示新提示符。
    void begin_output();
    void show_prompt();

    const std::map<std::string, Command>& current_commands() const;
    const char* current_table_name() const;
    const char* current_prompt() const;

    std::map<std::string, Command> m_lobby_commands;
    std::map<std::string, Command> m_game_commands;
    std::string m_name;
    Role m_role;
    Phase m_phase = Phase::Lobby;
    std::istream& m_in;
    std::ostream& m_out;
    EventLoop m_loop;
    bool m_stdin_is_console = false;
    bool m_prompt = true;
    bool m_prompt_pending = false;
    std::optional<std::uint32_t> m_seed;
    std::optional<std::vector<std::string>> m_deck_override;
};

} // 命名空间 uno
