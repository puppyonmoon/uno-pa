#pragma once

#include <iostream>
#include <memory>
#include <string>
#include <map>
#include <vector>
#include "game/round.h"
#include "game/session.h"
#include "protocol/net.h"
#include "protocol/unix.h"

namespace uno {

// ---------------------------------------------------------------------------
// HostSession -- 持有权威状态的一端的会话。
//
// 框架已经替你完成：本地/网络监听、连接的 accept 与编号、断线检测、消息
// 分帧、出站排队。你在这里要做的只有「策略」：
//
//   * on_peer_join / on_peer_message / on_peer_leave：一个连接来、说话、走；
//   * start / kick 以及共用命令的 host 侧实现：改状态、决定发给谁；
//   * 玩家与座位模型、规则引擎、回合状态机。
//
// 与游戏规则相关的工作先用 info("TODO(host): ...") 标记，你在对应函数里
// 替换成真正的实现。
// ---------------------------------------------------------------------------
class HostSession : public Session {
public:
    explicit HostSession(std::string name, std::istream& in = std::cin,
                         std::ostream& out = std::cout);
    ~HostSession() override;

    // 本地房间：在 <运行时目录>/uno/<room>.sock 上监听，客户端用 --join=<room>
    // 加入。这是本作业默认的多人方式，同一台机器上的多个进程即可开局。
    bool listen_room(const std::string& room, std::string& err);

    // 网络彩蛋：TCP 监听，客户端用 --host=<ip> 加入。
    bool listen_tcp(int port, std::string& err);

    const std::string& room_path() const { return m_room_path; }

protected:
    // ---- 事件钩子（你要实现）------------------------------------------
    void on_peer_join(PeerId who) override;
    void on_peer_message(PeerId from, const Message& message) override;
    void on_peer_leave(PeerId who) override;

    // ---- host 独有的大厅命令 -------------------------------------------
    Result start(const Args& args);
    Result kick(const Args& args);

    // ---- 与 client 共有的命令，host 侧实现 -----------------------------
    Result rename(const Args& args) override;
    Result play(const Args& args) override;
    Result cards(const Args& args) override;
    Result draw(const Args& args) override;
    Result pass(const Args& args) override;
    Result uno(const Args& args) override;
    Result challenge(const Args& args) override;
    Result swap(const Args& args) override;
    // 游戏阶段 exit / quit：中止本局并通知所有客户端。
    Result leave_game() override;

private:
    struct Player {
	std::string name;
	PeerId peer;
    };
    int seat_of(PeerId who) const; // 没 JOIN 过返回 -1
    Player* player_by_arg(const std::string& arg); // kick: 先按座位号，再按名字
    Message welcome_message(int seat) const;
    Message players_message() const;
    void broadcast_players();
    void broadcast_to_players(Message message);
    void reseat_from(int first_seat); // 前移座位并通知本人
    bool send_error(PeerId who, const std::string& text);
    void handle_join(PeerId from, const Message& message);
    void handle_rename(PeerId from, const Message& message);
    void handle_leave(PeerId from);

    // ---- 阶段二：一局的状态与消息 ---------------------------------------
    std::string hand_text(int seat) const;
    Message hand_message(int seat) const;
    void send_hand(int seat);      // 只发给本人（房主没有连接，跳过）
    void show_own_hand();          // 房主自己的手牌直接打印在终端
    void print_local(const Message& message); // 把广播也显示给房主
    void broadcast_turn();         // 广播当前轮次视图
    void report(PeerId to, const std::string& text); // 有连接就回 ERROR，否则打印
    void play_at(PeerId from, int seat, const std::string& cards,
                 const std::string& color);
    void draw_at(PeerId from, int seat);
    void pass_at(PeerId from, int seat);
    void uno_at(PeerId from, int seat);
    void finish_round(int winner, int points);
    void handle_play(PeerId from, const Message& message);
    void handle_draw(PeerId from);
    void handle_pass(PeerId from);
    void handle_uno(PeerId from, const Message& message);

    std::vector<Player> m_players; // 下标 == 座位号，始终紧密
    std::map<PeerId, int> m_seat_of; // 连接 -> 座位号
    std::unique_ptr<Round> m_round; // 游戏阶段才有；回大厅时清空
    std::unique_ptr<Server> m_server;
    std::string m_room_path;

};

} // 命名空间 uno
