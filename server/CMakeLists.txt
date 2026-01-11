cmake_minimum_required(VERSION 3.20)
project(LawnMowerServer CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 查找依赖
find_package(Protobuf REQUIRED)
find_package(spdlog REQUIRED)

# Protobuf 代码生成
get_filename_component(REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(PROTO_DIR "${REPO_ROOT}/proto")
if(NOT EXISTS "${PROTO_DIR}")
    message(FATAL_ERROR "Proto directory not found: ${PROTO_DIR}")
endif()
set(GEN_DIR ${CMAKE_BINARY_DIR}/generated)
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
  src/network/tcp/tcp_session.cpp
  src/network/tcp/tcp_server.cpp
  src/network/udp/udp_server.cpp
  src/config/server_config.cpp
  src/game/managers/room_manager.cpp
  src/game/managers/game_manager.cpp
)
target_include_directories(server PRIVATE include)
target_compile_definitions(server PRIVATE SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_DEBUG)
target_link_libraries(server 
  PRIVATE 
      proto_lib 
      spdlog::spdlog
)

# Abseil（可选）：如果找不到日志目标则跳过，不阻塞构建
set(ABSL_LOG_TARGETS)
find_package(absl CONFIG QUIET)
if(absl_FOUND AND TARGET absl::log)
  list(APPEND ABSL_LOG_TARGETS
    absl::log
    absl::log_internal_check_op
    absl::log_internal_message
    absl::log_internal_nullguard
    absl::check
  )
else()
  find_library(ABSL_LOG absl_log)
  find_library(ABSL_LOG_INTERNAL_CHECK_OP absl_log_internal_check_op)
  find_library(ABSL_LOG_INTERNAL_MESSAGE absl_log_internal_message)
  find_library(ABSL_LOG_INTERNAL_NULLGUARD absl_log_internal_nullguard)
  find_library(ABSL_CHECK NAMES absl_check absl_die_if_null)
  if(ABSL_LOG AND ABSL_LOG_INTERNAL_CHECK_OP AND ABSL_LOG_INTERNAL_MESSAGE AND ABSL_LOG_INTERNAL_NULLGUARD AND ABSL_CHECK)
    list(APPEND ABSL_LOG_TARGETS
      ${ABSL_LOG}
      ${ABSL_LOG_INTERNAL_CHECK_OP}
      ${ABSL_LOG_INTERNAL_MESSAGE}
      ${ABSL_LOG_INTERNAL_NULLGUARD}
      ${ABSL_CHECK}
    )
  else()
    message(STATUS "Abseil log libs not found; skipping absl logging")
  endif()
endif()

target_link_libraries(server
  PRIVATE
      ${ABSL_LOG_TARGETS}
)
