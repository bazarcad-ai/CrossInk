#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

#include "ReadingStatsUtils.h"

// Permanent per-day, per-book reading time, persisted to
// /.crosspoint/reading_history.bin. Each distinct title is stored once (48
// bytes) and each reading day adds 8 bytes per book, so a year of daily
// reading is roughly 8 KB. Callers stream the file; nothing loads it whole.
namespace DailyReadingLog {
constexpr size_t TITLE_BYTES = 47;

// Adds one finished session, split at local midnight, by rewriting the file
// through a .tmp copy and keeping the previous file as .bak.
bool recordSession(const char* title, const ReadingStatsDateTime& localStart, uint32_t seconds);

// Deletes the history, its backup, and any legacy 7-day log.
bool remove();
}  // namespace DailyReadingLog

// Bounded in-memory slice of the history for the stats pages. ~4.2 KB, so keep
// instances on the heap.
struct DailyReadingWindow {
  static constexpr size_t MAX_ENTRIES = 240;
  static constexpr size_t MAX_BOOKS = 48;

  struct Entry {
    uint16_t dayIndex = 0;  // readingStatsDayIndex() of the local date
    uint8_t book = 0;       // index into titles
    uint32_t seconds = 0;
  };

  uint32_t firstDay = 0;
  uint32_t lastDay = 0;
  std::array<Entry, MAX_ENTRIES> entries{};
  uint16_t count = 0;
  std::array<std::array<char, DailyReadingLog::TITLE_BYTES + 1>, MAX_BOOKS> titles{};
  uint8_t bookCount = 0;

  // Loads entries with firstDay <= day <= lastDay. An unreadable or missing
  // file leaves the window empty and returns false.
  bool load(uint32_t firstDayIndex, uint32_t lastDayIndex);

  // Adds an uncommitted session in memory only (for live previews).
  void addSpan(const char* title, const ReadingStatsDateTime& localStart, uint32_t seconds);
  void addSeconds(uint32_t dayIndex, const char* title, uint32_t seconds);
};
