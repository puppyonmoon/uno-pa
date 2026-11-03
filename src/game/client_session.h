#pragma once

#include <iostream>
#include <memory>
#include <string>
#include <map>
#include <vector>
#include "game/session.h"
#include "protocol/net.h"
#include "protocol/unix.h"

namespace uno {

// ---------------------------------------------------------------------------
// ClientSession -- 只连主机、不持有权威状态的一端的会话。
//
// 客户端只有一个对端（主机），所以 on_peer_join 不会被调用。你要做的是：
//
//   * on_connected()：连上之后发出第一条消息（JOIN）；
//   * on_peer_message()：把主机发来的消息维护成一份本地视图（自己是谁、
//     手牌是什么、现在轮到谁），这就是所谓「镜像」；
//   * cards()：从镜像里把手牌打印出来，而不是去问主机；
//   * on_peer_leave()：主机断开时的收尾。
//
// 命令 -> 协议消息的翻译已经给出（协议由框架固定），你不需要重新发明它。
// ---------------------------------------------------------------------------
class ClientSession : public Session {
public:
    explicit ClientSession(std::string name, std::istream& in = std::cin,
                           std::ostream& out = std::cout);
    ~ClientSession() override;

    // 加入本机上的一个房间（AF_UNIX）。
    bool join_room(const std::string& room, std::string& err);
    // 网络彩蛋：连接远程主机（TCP）。
    bool connect_tcp(const std::string& host, int port, std::string& err);

protected:
    void on_peer_join(PeerId who) override;
    void on_peer_message(PeerId from, const Message& message) override;
    void on_peer_leave(PeerId who) override;
    void on_connected() override;

    // 发给主机。连接已经断开时返回 false。
    bool send_to_host(Message message);

    Result rename(const Args& args) override;
    Result play(const Args& args) override;
    Result cards(const Args& args) override;
    Result draw(const Args& args) override;
    Result pass(const Args& args) override;
    Result uno(const Args& args) override;
    Result challenge(const Args& args) override;
    Result swap(const Args& args) override;
    // 游戏阶段 exit / quit：回到大厅，并告诉主机「本局我退出」。
    Result leave_game() override;

private:
    PeerId m_host;
    int m_seat = -1; // 最近一条 WELCOME 的 id
    std::string m_display_name; // 最近一条 WELCOME/PLAYERS 里我的名字
    std::map<int, std::string> m_roster; // 最近一条 PLAYERS 的名单

    // 牌桌镜像（只用于显示，不参与判定）
    std::vector<std::string> m_hand; // 最近一条 HAND
    std::string m_top;               // 当前牌面
    std::string m_color;             // 万能牌生效的颜色
    int m_current = -1;              // 轮到谁
    int m_direction = 1;             // +1 / -1
    int m_pending = 0;               // 当前玩家面前的罚抽张数
};

} // 命名空间 uno
