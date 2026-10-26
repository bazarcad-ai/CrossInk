#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

#include "ReadingStatsUtils.h"

// Rolling per-day, per-book reading time for the last DailyReadingLog::WINDOW_DAYS
// local days, persisted to /.crosspoint/daily_reading.bin. Books are keyed by
// title. The struct is ~3.6 KB, so keep instances on the heap.
struct DailyReadingLog {
  static constexpr size_t MAX_ENTRIES = 64;
  static constexpr size_t TITLE_BYTES = 47;
  static constexpr uint32_t WINDOW_DAYS = 7;

  struct Entry {
    uint32_t dayIndex = 0;  // readingStatsDayIndex() of the local date
    uint32_t seconds = 0;
    char title[TITLE_BYTES + 1] = {};
  };

  std::array<Entry, MAX_ENTRIES> entries{};
  uint8_t count = 0;

  // Replaces this log with the file contents. Leaves it empty and returns false
  // when the file is missing, corrupt, or from a newer build.
  bool load();
  bool save() const;
  static bool remove();

  // Splits the span at local midnight and adds each part to its day. Drops days
  // that fall outside the window ending on the span's last day.
  void addSpan(const char* title, const ReadingStatsDateTime& localStart, uint32_t seconds);
  void addSeconds(uint32_t dayIndex, const char* title, uint32_t seconds);
  void pruneBefore(uint32_t firstDayIndex);

  // Load, add one finished session, prune, and save. Used by reader session commits.
  static void recordSession(const char* title, const ReadingStatsDateTime& localStart, uint32_t seconds);
};
