cmake_minimum_required(VERSION 3.16)
project(uno CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

add_compile_options(-Wall -Wextra)

# ---------------------------------------------------------------------------
# 框架核心。
#
# 行式协议、事件循环、本地/TCP 传输以及直接持有命令表的 host/client
# Session 均由框架提供。卡牌、牌堆、玩家模型、大厅策略和 UNO 规则引擎
# 被有意留空，它们对应三个作业阶段。
# ---------------------------------------------------------------------------
set(UNO_CORE_SOURCES
    src/protocol/message.cpp
    src/protocol/net.cpp
    src/protocol/unix.cpp
    src/protocol/loop.cpp
    src/game/session.cpp
    src/game/host_session.cpp
    src/game/client_session.cpp
    src/game/card.cpp
    src/game/round.cpp
)

add_library(uno_core STATIC ${UNO_CORE_SOURCES})
target_include_directories(uno_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

add_executable(uno src/main.cpp)
target_link_libraries(uno PRIVATE uno_core)

include(CTest)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
