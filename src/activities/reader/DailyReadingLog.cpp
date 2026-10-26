#include "DailyReadingLog.h"

#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>

#include <cstring>
#include <limits>
#include <string>

namespace {
// Binary layout v1 (2 + count * 56 bytes):
//   [0]  version (= 1)
//   [1]  entry count (<= MAX_ENTRIES)
//   then per entry:
//     [0-3]   dayIndex  uint32_t LE
//     [4-7]   seconds   uint32_t LE
//     [8]     titleLen  uint8_t (<= TITLE_BYTES)
//     [9-55]  title     UTF-8, zero padded
constexpr uint8_t DAILY_LOG_VERSION = 1;
constexpr size_t HEADER_SIZE = 2;
constexpr size_t ENTRY_SIZE = 9 + DailyReadingLog::TITLE_BYTES;
constexpr char DAILY_LOG_PATH[] = "/.crosspoint/daily_reading.bin";
constexpr char DAILY_LOG_TMP_PATH[] = "/.crosspoint/daily_reading.bin.tmp";

uint32_t readLe32(const uint8_t* data) {
  return static_cast<uint32_t>(data[0]) | (static_cast<uint32_t>(data[1]) << 8) |
         (static_cast<uint32_t>(data[2]) << 16) | (static_cast<uint32_t>(data[3]) << 24);
}

void writeLe32(uint8_t* data, const uint32_t value) {
  data[0] = value & 0xFF;
  data[1] = (value >> 8) & 0xFF;
  data[2] = (value >> 16) & 0xFF;
  data[3] = (value >> 24) & 0xFF;
}

// Copies at most TITLE_BYTES without splitting a UTF-8 sequence.
void copyTitle(char* dest, const char* title) {
  const char* source = title ? title : "";
  size_t len = strnlen(source, DailyReadingLog::TITLE_BYTES + 1);
  if (len > DailyReadingLog::TITLE_BYTES) {
    len = DailyReadingLog::TITLE_BYTES;
    while (len > 0 && (static_cast<uint8_t>(source[len]) & 0xC0) == 0x80) {
      len--;
    }
  }
  memcpy(dest, source, len);
  dest[len] = '\0';
}

uint32_t addSaturated(const uint32_t a, const uint32_t b) {
  const uint32_t max = std::numeric_limits<uint32_t>::max();
  return max - a < b ? max : a + b;
}
}  // namespace

bool DailyReadingLog::load() {
  count = 0;
  FsFile f;
  if (!Storage.openFileForRead("DAILYLOG", DAILY_LOG_PATH, f)) return false;

  uint8_t header[HEADER_SIZE] = {};
  const size_t fileSize = f.fileSize();
  if (f.read(header, HEADER_SIZE) != static_cast<int>(HEADER_SIZE) || header[0] != DAILY_LOG_VERSION ||
      header[1] > MAX_ENTRIES || fileSize != HEADER_SIZE + header[1] * ENTRY_SIZE) {
    LOG_ERR("DAILYLOG", "Ignoring unreadable daily log (v%u, %u bytes)", header[0], static_cast<unsigned>(fileSize));
    f.close();
    return false;
  }

  uint8_t record[ENTRY_SIZE];
  for (uint8_t i = 0; i < header[1]; ++i) {
    if (f.read(record, ENTRY_SIZE) != static_cast<int>(ENTRY_SIZE)) {
      LOG_ERR("DAILYLOG", "Short read in daily log at entry %u", i);
      f.close();
      count = 0;
      return false;
    }
    Entry& entry = entries[count++];
    entry.dayIndex = readLe32(record);
    entry.seconds = readLe32(record + 4);
    const size_t titleLen = record[8] <= TITLE_BYTES ? record[8] : TITLE_BYTES;
    memcpy(entry.title, record + 9, titleLen);
    entry.title[titleLen] = '\0';
  }
  f.close();
  return true;
}

bool DailyReadingLog::save() const {
  if (Storage.exists(DAILY_LOG_TMP_PATH) && !Storage.remove(DAILY_LOG_TMP_PATH)) {
    LOG_ERR("DAILYLOG", "Could not remove stale daily log temp file");
    return false;
  }

  FsFile f;
  if (!Storage.openFileForWrite("DAILYLOG", DAILY_LOG_TMP_PATH, f)) {
    LOG_ERR("DAILYLOG", "Could not write daily log temp file");
    return false;
  }

  const uint8_t header[HEADER_SIZE] = {DAILY_LOG_VERSION, count};
  bool ok = f.write(header, HEADER_SIZE) == HEADER_SIZE;
  uint8_t record[ENTRY_SIZE];
  for (uint8_t i = 0; ok && i < count; ++i) {
    const Entry& entry = entries[i];
    memset(record, 0, sizeof(record));
    writeLe32(record, entry.dayIndex);
    writeLe32(record + 4, entry.seconds);
    const size_t titleLen = strnlen(entry.title, TITLE_BYTES);
    record[8] = static_cast<uint8_t>(titleLen);
    memcpy(record + 9, entry.title, titleLen);
    ok = f.write(record, ENTRY_SIZE) == ENTRY_SIZE;
  }
  f.flush();
  ok = ok && f.sync();
  if (!f.close() || !ok) {
    LOG_ERR("DAILYLOG", "Failed to write daily log temp file");
    Storage.remove(DAILY_LOG_TMP_PATH);
    return false;
  }

  if (Storage.exists(DAILY_LOG_PATH) && !Storage.remove(DAILY_LOG_PATH)) {
    LOG_ERR("DAILYLOG", "Could not replace daily log");
    Storage.remove(DAILY_LOG_TMP_PATH);
    return false;
  }
  if (!Storage.rename(DAILY_LOG_TMP_PATH, DAILY_LOG_PATH)) {
    LOG_ERR("DAILYLOG", "Could not publish daily log");
    return false;
  }
  return true;
}

bool DailyReadingLog::remove() {
  Storage.remove(DAILY_LOG_TMP_PATH);
  if (Storage.exists(DAILY_LOG_PATH) && !Storage.remove(DAILY_LOG_PATH)) {
    LOG_ERR("DAILYLOG", "Could not remove daily log");
    return false;
  }
  return true;
}

void DailyReadingLog::addSeconds(const uint32_t dayIndex, const char* title, const uint32_t seconds) {
  if (seconds == 0) return;
  char key[TITLE_BYTES + 1];
  copyTitle(key, title);

  for (uint8_t i = 0; i < count; ++i) {
    if (entries[i].dayIndex == dayIndex && strcmp(entries[i].title, key) == 0) {
      entries[i].seconds = addSaturated(entries[i].seconds, seconds);
      return;
    }
  }

  uint8_t slot = count;
  if (count == MAX_ENTRIES) {
    // Full: replace the oldest, smallest entry.
    slot = 0;
    for (uint8_t i = 1; i < count; ++i) {
      const Entry& candidate = entries[i];
      const Entry& current = entries[slot];
      if (candidate.dayIndex < current.dayIndex ||
          (candidate.dayIndex == current.dayIndex && candidate.seconds < current.seconds)) {
        slot = i;
      }
    }
    if (entries[slot].dayIndex > dayIndex) return;
  } else {
    count++;
  }
  entries[slot].dayIndex = dayIndex;
  entries[slot].seconds = seconds;
  memcpy(entries[slot].title, key, sizeof(key));
}

void DailyReadingLog::pruneBefore(const uint32_t firstDayIndex) {
  uint8_t kept = 0;
  for (uint8_t i = 0; i < count; ++i) {
    if (entries[i].dayIndex >= firstDayIndex) {
      if (kept != i) entries[kept] = entries[i];
      kept++;
    }
  }
  count = kept;
}

void DailyReadingLog::addSpan(const char* title, const ReadingStatsDateTime& localStart, const uint32_t seconds) {
  if (!localStart.isValid() || seconds == 0) return;

  ReadingStatsDateTime cursor = localStart;
  uint32_t remaining = seconds;
  uint32_t lastDay = readingStatsDayIndex(cursor.date);
  while (remaining > 0) {
    lastDay = readingStatsDayIndex(cursor.date);
    const uint32_t secondsUntilMidnight =
        (24u * 3600u) - (static_cast<uint32_t>(cursor.hour) * 3600u + static_cast<uint32_t>(cursor.minute) * 60u +
                         static_cast<uint32_t>(cursor.second));
    const uint32_t segment = remaining < secondsUntilMidnight ? remaining : secondsUntilMidnight;
    addSeconds(lastDay, title, segment);
    remaining -= segment;
    addSecondsToReadingStatsDateTime(cursor, segment);
  }
  if (lastDay + 1 >= WINDOW_DAYS) pruneBefore(lastDay + 1 - WINDOW_DAYS);
}

void DailyReadingLog::recordSession(const char* title, const ReadingStatsDateTime& localStart,
                                    const uint32_t seconds) {
  // ~3.6 KB: too large for the caller's stack, and only needed for this write.
  auto log = makeUniqueNoThrow<DailyReadingLog>();
  if (!log) {
    LOG_ERR("DAILYLOG", "Out of memory recording daily reading time");
    return;
  }
  log->load();
  log->addSpan(title, localStart, seconds);
  log->save();
}
