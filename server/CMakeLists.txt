cmake_minimum_required(VERSION 4.2.1)
project(LawnMowerServer CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 查找依赖
find_package(Protobuf REQUIRED)
find_package(spdlog REQUIRED)
find_package(PkgConfig REQUIRED)

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
)
target_include_directories(server PRIVATE include)
target_link_libraries(server 
  PRIVATE 
      proto_lib 
      spdlog::spdlog
)

# absl全家桶
pkg_check_modules(ABSEIL REQUIRED IMPORTED_TARGET
    absl_log
    absl_flags
    absl_flags_parse
)
target_link_libraries(server 
  PRIVATE 
  PkgConfig::ABSEIL
  absl_log_internal_check_op
  absl_die_if_null
  absl_log_internal_message
)
