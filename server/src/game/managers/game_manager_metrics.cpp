#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <spdlog/spdlog.h>
#include <sstream>
#include <vector>

#include "game/managers/game_manager.hpp"

namespace {
constexpr const char* kPerfRootDir = "server_metrics";

std::tm ToLocalTm(std::time_t value) {
  std::tm result{};
#if defined(_WIN32)
  localtime_s(&result, &value);
#else
  localtime_r(&value, &result);
#endif
  return result;
}

std::string FormatDate(const std::chrono::system_clock::time_point& tp) {
  const std::time_t time_value = std::chrono::system_clock::to_time_t(tp);
  const std::tm tm_value = ToLocalTm(time_value);
  std::ostringstream oss;
  oss << std::put_time(&tm_value, "%Y-%m-%d");
  return oss.str();
}

std::string FormatDateTime(const std::chrono::system_clock::time_point& tp) {
  const std::time_t time_value = std::chrono::system_clock::to_time_t(tp);
  const std::tm tm_value = ToLocalTm(time_value);
  std::ostringstream oss;
  oss << std::put_time(&tm_value, "%Y-%m-%d %H:%M:%S");
  return oss.str();
}

std::filesystem::path GetServerRootDir() {
  std::filesystem::path root = std::filesystem::path(__FILE__).parent_path();
  for (int i = 0; i < 4; ++i) {
    if (!root.has_parent_path()) {
      break;
    }
    root = root.parent_path();
  }
  return root;
}

uint64_t ToEpochMs(const std::chrono::system_clock::time_point& tp) {
  return static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          tp.time_since_epoch())
          .count());
}

double ComputePercentile(std::vector<double> values, double percentile) {
  if (values.empty()) {
    return 0.0;
  }
  const double clamped = std::clamp(percentile, 0.0, 1.0);
  const std::size_t index = static_cast<std::size_t>(
      std::ceil(clamped * static_cast<double>(values.size() - 1)));
  std::nth_element(values.begin(), values.begin() + index, values.end());
  return values[index];
}
}  // namespace

void GameManager::ResetPerfStats(Scene& scene) {
  scene.perf.samples.clear();
  scene.perf.total_ms = 0.0;
  scene.perf.max_ms = 0.0;
  scene.perf.min_ms = 0.0;
  scene.perf.tick_count = 0;
  scene.perf.start_time = std::chrono::system_clock::now();
  scene.perf.end_time = scene.perf.start_time;
}

void GameManager::RecordPerfSampleLocked(Scene& scene, double elapsed_ms,
                                         double dt_seconds, bool is_paused,
                                         uint32_t dirty_player_count,
                                         uint32_t dirty_enemy_count,
                                         uint32_t dirty_item_count,
                                         uint32_t delta_items_size,
                                         uint32_t sync_items_size) {
  scene.perf.tick_count += 1;
  scene.perf.total_ms += elapsed_ms;
  if (scene.perf.tick_count == 1) {
    scene.perf.min_ms = elapsed_ms;
    scene.perf.max_ms = elapsed_ms;
  } else {
    scene.perf.min_ms = std::min(scene.perf.min_ms, elapsed_ms);
    scene.perf.max_ms = std::max(scene.perf.max_ms, elapsed_ms);
  }

  const uint32_t stride = std::max<uint32_t>(1, config_.perf_sample_stride);
  if (stride > 1 && (scene.tick % stride) != 0) {
    return;
  }

  PerfSample sample;
  sample.tick = scene.tick;
  sample.logic_ms = elapsed_ms;
  sample.dt_seconds = dt_seconds;
  sample.player_count = static_cast<uint32_t>(scene.players.size());
  sample.enemy_count = static_cast<uint32_t>(scene.enemies.size());
  sample.projectile_count = static_cast<uint32_t>(scene.projectiles.size());
  sample.item_count = static_cast<uint32_t>(scene.items.size());
  sample.dirty_player_count = dirty_player_count;
  sample.dirty_enemy_count = dirty_enemy_count;
  sample.dirty_item_count = dirty_item_count;
  sample.is_paused = is_paused;
  sample.delta_items_size = delta_items_size;
  sample.sync_items_size = sync_items_size;

  scene.perf.samples.push_back(sample);
}

void GameManager::SavePerfStatsToFile(uint32_t room_id, const PerfStats& stats,
                                      uint32_t tick_rate, uint32_t sync_rate,
                                      double elapsed_seconds) {
  const std::string date_dir = FormatDate(stats.end_time);
  const std::filesystem::path root_dir = GetServerRootDir();
  std::filesystem::path output_dir = root_dir / kPerfRootDir / date_dir;
  std::error_code ec;
  std::filesystem::create_directories(output_dir, ec);
  if (ec) {
    spdlog::warn("房间 {} 性能数据目录创建失败: {}", room_id, ec.message());
    return;
  }

  const uint64_t epoch_ms = ToEpochMs(stats.end_time);
  std::ostringstream file_name;
  file_name << "room_" << room_id << "_run_" << epoch_ms << ".json";
  const std::filesystem::path output_file = output_dir / file_name.str();

  std::ofstream out(output_file, std::ios::out | std::ios::trunc);
  if (!out.is_open()) {
    spdlog::warn("房间 {} 性能数据文件打开失败: {}", room_id,
                 output_file.string());
    return;
  }

  const double avg_ms =
      stats.tick_count > 0 ? stats.total_ms / stats.tick_count : 0.0;
  std::vector<double> ms_values;
  ms_values.reserve(stats.samples.size());
  uint64_t sum_players = 0;
  uint64_t sum_enemies = 0;
  uint64_t sum_items = 0;
  uint64_t sum_dirty_players = 0;
  uint64_t sum_dirty_enemies = 0;
  uint64_t sum_dirty_items = 0;
  for (const auto& sample : stats.samples) {
    ms_values.push_back(sample.logic_ms);
    sum_players += sample.player_count;
    sum_enemies += sample.enemy_count;
    sum_items += sample.item_count;
    sum_dirty_players += sample.dirty_player_count;
    sum_dirty_enemies += sample.dirty_enemy_count;
    sum_dirty_items += sample.dirty_item_count;
  }
  const double p95_ms = ComputePercentile(ms_values, 0.95);
  const double dirty_player_ratio =
      sum_players > 0 ? static_cast<double>(sum_dirty_players) /
                            static_cast<double>(sum_players)
                      : 0.0;
  const double dirty_enemy_ratio =
      sum_enemies > 0 ? static_cast<double>(sum_dirty_enemies) /
                            static_cast<double>(sum_enemies)
                      : 0.0;
  const double dirty_item_ratio = sum_items > 0
                                      ? static_cast<double>(sum_dirty_items) /
                                            static_cast<double>(sum_items)
                                      : 0.0;

  out << "{\n";
  out << "  \"room_id\": " << room_id << ",\n";
  out << "  \"start_time\": \"" << FormatDateTime(stats.start_time) << "\",\n";
  out << "  \"end_time\": \"" << FormatDateTime(stats.end_time) << "\",\n";
  out << "  \"elapsed_seconds\": " << std::fixed << std::setprecision(3)
      << elapsed_seconds << ",\n";
  out << "  \"tick_rate\": " << tick_rate << ",\n";
  out << "  \"sync_rate\": " << sync_rate << ",\n";
  out << "  \"tick_count\": " << stats.tick_count << ",\n";
  out << "  \"avg_ms\": " << std::fixed << std::setprecision(3) << avg_ms
      << ",\n";
  out << "  \"min_ms\": " << std::fixed << std::setprecision(3) << stats.min_ms
      << ",\n";
  out << "  \"max_ms\": " << std::fixed << std::setprecision(3) << stats.max_ms
      << ",\n";
  out << "  \"p95_ms\": " << std::fixed << std::setprecision(3) << p95_ms
      << ",\n";
  out << "  \"dirty_player_ratio\": " << std::fixed << std::setprecision(6)
      << dirty_player_ratio << ",\n";
  out << "  \"dirty_enemy_ratio\": " << std::fixed << std::setprecision(6)
      << dirty_enemy_ratio << ",\n";
  out << "  \"dirty_item_ratio\": " << std::fixed << std::setprecision(6)
      << dirty_item_ratio << ",\n";
  out << "  \"samples\": [\n";
  for (std::size_t i = 0; i < stats.samples.size(); ++i) {
    const auto& sample = stats.samples[i];
    const double sample_dirty_player_ratio =
        sample.player_count > 0
            ? static_cast<double>(sample.dirty_player_count) /
                  static_cast<double>(sample.player_count)
            : 0.0;
    const double sample_dirty_enemy_ratio =
        sample.enemy_count > 0 ? static_cast<double>(sample.dirty_enemy_count) /
                                     static_cast<double>(sample.enemy_count)
                               : 0.0;
    const double sample_dirty_item_ratio =
        sample.item_count > 0 ? static_cast<double>(sample.dirty_item_count) /
                                    static_cast<double>(sample.item_count)
                              : 0.0;
    out << "    {\"tick\": " << sample.tick << ", \"logic_ms\": " << std::fixed
        << std::setprecision(3) << sample.logic_ms
        << ", \"dt_seconds\": " << std::fixed << std::setprecision(6)
        << sample.dt_seconds << ", \"players\": " << sample.player_count
        << ", \"enemies\": " << sample.enemy_count
        << ", \"projectiles\": " << sample.projectile_count
        << ", \"items\": " << sample.item_count
        << ", \"dirty_players\": " << sample.dirty_player_count
        << ", \"dirty_enemies\": " << sample.dirty_enemy_count
        << ", \"dirty_items\": " << sample.dirty_item_count
        << ", \"dirty_player_ratio\": " << std::fixed << std::setprecision(6)
        << sample_dirty_player_ratio
        << ", \"dirty_enemy_ratio\": " << std::fixed << std::setprecision(6)
        << sample_dirty_enemy_ratio << ", \"dirty_item_ratio\": " << std::fixed
        << std::setprecision(6) << sample_dirty_item_ratio
        << ", \"delta_items\": " << sample.delta_items_size
        << ", \"sync_items\": " << sample.sync_items_size
        << ", \"paused\": " << (sample.is_paused ? "true" : "false") << "}";
    if (i + 1 < stats.samples.size()) {
      out << ",";
    }
    out << "\n";
  }
  out << "  ]\n";
  out << "}\n";
  out.close();

  spdlog::info("房间 {} 性能数据已保存: {}", room_id, output_file.string());
}
