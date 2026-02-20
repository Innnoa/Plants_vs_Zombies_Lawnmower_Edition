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
  src/network/tcp/session_auth.cpp
  src/network/tcp/session_room.cpp
  src/network/tcp/session_gameplay.cpp
  src/network/tcp/tcp_server.cpp
  src/network/udp/udp_server.cpp
  src/config/server_config.cpp
  src/config/player_roles_config.cpp
  src/config/enemy_types_config.cpp
  src/config/item_types_config.cpp
  src/config/upgrade_config.cpp
  src/game/managers/room_manager.cpp
  src/game/managers/game_manager.cpp
  src/game/managers/game_manager_loop.cpp
  src/game/managers/game_manager_metrics.cpp
  src/game/managers/game_manager_scene.cpp
  src/game/managers/game_manager_sync.cpp
  src/game/managers/game_manager_sync_dispatch.cpp
  src/game/managers/game_manager_event_dispatch.cpp
  src/game/managers/game_manager_misc_utils.cpp
  src/game/managers/game_manager_runtime.cpp
  src/game/managers/game_manager_tick_flow.cpp
  src/game/managers/game_manager_tick_dispatch.cpp
  src/game/managers/game_manager_internal_utils.cpp
  src/game/managers/game_manager_upgrade.cpp
  src/game/managers/game_manager_session.cpp
  src/game/managers/game_manager_enemy.cpp
  src/game/managers/game_manager_combat.cpp
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
set(ABSL_STATUS_TARGETS)
find_package(absl CONFIG QUIET)
if(absl_FOUND AND TARGET absl::log)
  list(APPEND ABSL_LOG_TARGETS
    absl::log
    absl::log_internal_check_op
    absl::log_internal_message
    absl::log_internal_nullguard
    absl::check
  )
  if(TARGET absl::status)
    list(APPEND ABSL_STATUS_TARGETS absl::status)
  endif()
  if(TARGET absl::statusor)
    list(APPEND ABSL_STATUS_TARGETS absl::statusor)
  endif()
else()
  find_library(ABSL_LOG absl_log)
  find_library(ABSL_LOG_INTERNAL_CHECK_OP absl_log_internal_check_op)
  find_library(ABSL_LOG_INTERNAL_MESSAGE absl_log_internal_message)
  find_library(ABSL_LOG_INTERNAL_NULLGUARD absl_log_internal_nullguard)
  find_library(ABSL_CHECK NAMES absl_check absl_die_if_null)
  find_library(ABSL_STATUS absl_status)
  find_library(ABSL_STATUSOR absl_statusor)
  if(ABSL_LOG AND ABSL_LOG_INTERNAL_CHECK_OP AND ABSL_LOG_INTERNAL_MESSAGE AND ABSL_LOG_INTERNAL_NULLGUARD AND ABSL_CHECK)
    list(APPEND ABSL_LOG_TARGETS
      ${ABSL_LOG}
      ${ABSL_LOG_INTERNAL_CHECK_OP}
      ${ABSL_LOG_INTERNAL_MESSAGE}
      ${ABSL_LOG_INTERNAL_NULLGUARD}
      ${ABSL_CHECK}
    )
    if(ABSL_STATUS)
      list(APPEND ABSL_STATUS_TARGETS ${ABSL_STATUS})
    endif()
    if(ABSL_STATUSOR)
      list(APPEND ABSL_STATUS_TARGETS ${ABSL_STATUSOR})
    endif()
  else()
    message(STATUS "Abseil log libs not found; skipping absl logging")
  endif()
endif()

target_link_libraries(server
  PRIVATE
      ${ABSL_LOG_TARGETS}
      ${ABSL_STATUS_TARGETS}
)

enable_testing()

add_executable(server_smoke_test
  tests/server_smoke_test.cpp
)
target_link_libraries(server_smoke_test
  PRIVATE
      proto_lib
      ${ABSL_LOG_TARGETS}
)

add_test(
  NAME server_smoke
  COMMAND server_smoke_test $<TARGET_FILE:server>
)
set_tests_properties(server_smoke PROPERTIES TIMEOUT 45)

add_executable(udp_sync_smoke_test
  tests/udp_sync_smoke_test.cpp
)
target_link_libraries(udp_sync_smoke_test
  PRIVATE
      proto_lib
      ${ABSL_LOG_TARGETS}
)

add_test(
  NAME udp_sync_smoke
  COMMAND udp_sync_smoke_test $<TARGET_FILE:server>
)
set_tests_properties(udp_sync_smoke PROPERTIES TIMEOUT 60)

add_executable(config_loader_smoke_test
  tests/config_loader_smoke_test.cpp
  src/config/server_config.cpp
  src/config/player_roles_config.cpp
  src/config/enemy_types_config.cpp
  src/config/item_types_config.cpp
  src/config/upgrade_config.cpp
)
target_include_directories(config_loader_smoke_test PRIVATE include)
target_link_libraries(config_loader_smoke_test
  PRIVATE
      proto_lib
      spdlog::spdlog
      ${ABSL_LOG_TARGETS}
      ${ABSL_STATUS_TARGETS}
)

add_test(
  NAME config_loader_smoke
  COMMAND config_loader_smoke_test
)
set_tests_properties(config_loader_smoke PROPERTIES TIMEOUT 45)
