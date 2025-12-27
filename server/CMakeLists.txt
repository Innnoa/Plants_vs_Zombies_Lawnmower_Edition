cmake_minimum_required(VERSION 3.20)
project(LawnMowerServer CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 查找依赖
find_package(Protobuf REQUIRED)
find_package(spdlog REQUIRED)

# Protobuf 代码生成
set(PROTO_DIR ${CMAKE_SOURCE_DIR}/proto)
set(GEN_DIR ${CMAKE_SOURCE_DIR}/generated)
file(MAKE_DIRECTORY ${GEN_DIR})

file(GLOB PROTO_FILES "${PROTO_DIR}/*.proto")
set(GENERATED_SRCS)

foreach(PROTO ${PROTO_FILES})
    get_filename_component(NAME ${PROTO} NAME_WE)
    add_custom_command(
        OUTPUT ${GEN_DIR}/${NAME}.pb.cc ${GEN_DIR}/${NAME}.pb.h
        COMMAND protobuf::protoc
        ARGS --cpp_out=${GEN_DIR} --proto_path=${PROTO_DIR} ${PROTO}
        DEPENDS ${PROTO}
    )
    list(APPEND GENERATED_SRCS ${GEN_DIR}/${NAME}.pb.cc)
endforeach()

# Proto 库
add_library(proto_lib STATIC ${GENERATED_SRCS})
target_include_directories(proto_lib PUBLIC ${GEN_DIR})
target_link_libraries(proto_lib
  PUBLIC
      protobuf::libprotobuf
)

# 主程序
add_executable(server
  src/main.cpp
  src/network/tcp/tcp_server.cpp
  src/game/managers/room_manager.cpp
  src/game/managers/game_manager.cpp
)
target_include_directories(server PRIVATE include)
target_link_libraries(server 
  PRIVATE 
      proto_lib 
      spdlog::spdlog
)

# Abseil: protobuf 里内联的 CHECK 宏会依赖 absl 的日志实现，使用动态库名查找
find_library(ABSL_LOG_INTERNAL_CHECK_OP absl_log_internal_check_op REQUIRED)
find_library(ABSL_LOG_INTERNAL_MESSAGE absl_log_internal_message REQUIRED)
find_library(ABSL_LOG_INTERNAL_NULLGUARD absl_log_internal_nullguard REQUIRED)

target_link_libraries(server
  PRIVATE
      ${ABSL_LOG_INTERNAL_CHECK_OP}
      ${ABSL_LOG_INTERNAL_MESSAGE}
      ${ABSL_LOG_INTERNAL_NULLGUARD}
)
