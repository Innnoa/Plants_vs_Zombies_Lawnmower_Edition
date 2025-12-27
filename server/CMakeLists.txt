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

# Abseil：优先使用 CMake Config 且要求 absl::log 目标存在，否则回退到 find_library
set(ABSL_LOG_TARGETS)
find_package(absl CONFIG)
if(absl_FOUND AND TARGET absl::log)
  list(APPEND ABSL_LOG_TARGETS
    absl::log
    absl::log_internal_check_op
    absl::log_internal_message
    absl::log_internal_nullguard
    absl::check
  )
else()
  find_library(ABSL_LOG absl_log REQUIRED)
  find_library(ABSL_LOG_INTERNAL_CHECK_OP absl_log_internal_check_op REQUIRED)
  find_library(ABSL_LOG_INTERNAL_MESSAGE absl_log_internal_message REQUIRED)
  find_library(ABSL_LOG_INTERNAL_NULLGUARD absl_log_internal_nullguard REQUIRED)
  find_library(ABSL_CHECK NAMES absl_check absl_die_if_null REQUIRED)
  list(APPEND ABSL_LOG_TARGETS
    ${ABSL_LOG}
    ${ABSL_LOG_INTERNAL_CHECK_OP}
    ${ABSL_LOG_INTERNAL_MESSAGE}
    ${ABSL_LOG_INTERNAL_NULLGUARD}
    ${ABSL_CHECK}
  )
endif()

target_link_libraries(server
  PRIVATE
      ${ABSL_LOG_TARGETS}
)
