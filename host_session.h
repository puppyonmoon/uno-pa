#pragma once

#include <iostream>
#include <memory>
#include <string>

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

private:
    std::unique_ptr<Server> m_server;
    std::string m_room_path;
};

} // 命名空间 uno
