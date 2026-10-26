#include "DailyReadingLog.h"

#include <HalStorage.h>
#include <Logging.h>

#include <cstring>
#include <limits>

namespace {
// Binary layout v2:
//   [0]    version (= 2)
//   [1]    reserved (0)
//   [2-3]  titleCount  uint16_t LE
//   [4-7]  entryCount  uint32_t LE
//   then titleCount title records (48 bytes each, book id = record index):
//     [0]     titleLen uint8_t (<= TITLE_BYTES)
//     [1-47]  title    UTF-8, zero padded
//   then entryCount entry records (8 bytes each, one per local day and book):
//     [0-1]   dayIndex uint16_t LE (days since 2000-01-01)
//     [2-3]   bookId   uint16_t LE
//     [4-7]   seconds  uint32_t LE
constexpr uint8_t HISTORY_VERSION = 2;
constexpr size_t HEADER_SIZE = 8;
constexpr size_t TITLE_RECORD_SIZE = 1 + DailyReadingLog::TITLE_BYTES;
constexpr size_t ENTRY_RECORD_SIZE = 8;
constexpr size_t COPY_CHUNK_ENTRIES = 24;  // 192-byte stack buffer per chunk
constexpr size_t MAX_SESSION_SEGMENTS = 8;
constexpr char HISTORY_PATH[] = "/.crosspoint/reading_history.bin";
constexpr char HISTORY_TMP_PATH[] = "/.crosspoint/reading_history.bin.tmp";
constexpr char HISTORY_BAK_PATH[] = "/.crosspoint/reading_history.bin.bak";
// Format v1 kept only 7 days in a different file; it is not migrated.
constexpr char LEGACY_DAILY_LOG_PATH[] = "/.crosspoint/daily_reading.bin";
bool s_blockWrites = false;

enum class OpenResult : uint8_t { Missing, Ok, Invalid, Newer };

struct Header {
  uint16_t titleCount = 0;
  uint32_t entryCount = 0;
};

struct Segment {
  uint16_t dayIndex = 0;
  uint32_t seconds = 0;
  bool merged = false;
};

uint16_t readLe16(const uint8_t* data) {
  return static_cast<uint16_t>(data[0]) | static_cast<uint16_t>(static_cast<uint16_t>(data[1]) << 8);
}

uint32_t readLe32(const uint8_t* data) {
  return static_cast<uint32_t>(data[0]) | (static_cast<uint32_t>(data[1]) << 8) |
         (static_cast<uint32_t>(data[2]) << 16) | (static_cast<uint32_t>(data[3]) << 24);
}

void writeLe16(uint8_t* data, const uint16_t value) {
  data[0] = value & 0xFF;
  data[1] = (value >> 8) & 0xFF;
}

void writeLe32(uint8_t* data, const uint32_t value) {
  data[0] = value & 0xFF;
  data[1] = (value >> 8) & 0xFF;
  data[2] = (value >> 16) & 0xFF;
  data[3] = (value >> 24) & 0xFF;
}

uint32_t addSaturated(const uint32_t a, const uint32_t b) {
  const uint32_t max = std::numeric_limits<uint32_t>::max();
  return max - a < b ? max : a + b;
}

size_t expectedFileSize(const Header& header) {
  return HEADER_SIZE + static_cast<size_t>(header.titleCount) * TITLE_RECORD_SIZE +
         static_cast<size_t>(header.entryCount) * ENTRY_RECORD_SIZE;
}

// Copies at most TITLE_BYTES without splitting a UTF-8 sequence; returns the length.
size_t copyTitle(char* dest, const char* title) {
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
  return len;
}

// Opens path for reading and validates its header. On anything but Ok the
// file is closed.
OpenResult openHistory(const char* path, FsFile& f, Header& header) {
  if (!Storage.exists(path)) return OpenResult::Missing;
  if (!Storage.openFileForRead("RHIST", path, f)) return OpenResult::Invalid;
  uint8_t data[HEADER_SIZE] = {};
  if (f.read(data, HEADER_SIZE) != static_cast<int>(HEADER_SIZE)) {
    f.close();
    return OpenResult::Invalid;
  }
  if (data[0] > HISTORY_VERSION) {
    f.close();
    return OpenResult::Newer;
  }
  header.titleCount = readLe16(data + 2);
  header.entryCount = readLe32(data + 4);
  if (data[0] != HISTORY_VERSION || f.fileSize() != expectedFileSize(header)) {
    f.close();
    return OpenResult::Invalid;
  }
  return OpenResult::Ok;
}

// Opens the primary file, falling back to the backup when the primary is
// unreadable. Returns false only when a newer format forbids rewriting.
bool openSource(FsFile& f, Header& header, bool& hasSource) {
  hasSource = false;
  OpenResult result = openHistory(HISTORY_PATH, f, header);
  if (result == OpenResult::Invalid) {
    LOG_ERR("RHIST", "Reading history unreadable, trying backup");
    result = openHistory(HISTORY_BAK_PATH, f, header);
  }
  if (result == OpenResult::Newer) {
    LOG_ERR("RHIST", "Reading history is from a newer build; refusing to overwrite");
    s_blockWrites = true;
    return false;
  }
  if (result == OpenResult::Ok) {
    hasSource = true;
  } else {
    header = Header{};
  }
  return true;
}

bool findBookId(FsFile& source, const Header& header, const char* key, const size_t keyLen, uint16_t& bookId) {
  uint8_t record[TITLE_RECORD_SIZE];
  for (uint16_t id = 0; id < header.titleCount; ++id) {
    if (source.read(record, TITLE_RECORD_SIZE) != static_cast<int>(TITLE_RECORD_SIZE)) return false;
    if (record[0] == keyLen && memcmp(record + 1, key, keyLen) == 0) {
      bookId = id;
      return true;
    }
  }
  return false;
}

bool copyBytes(FsFile& source, FsFile& dest, size_t bytes) {
  uint8_t buffer[COPY_CHUNK_ENTRIES * ENTRY_RECORD_SIZE];
  while (bytes > 0) {
    const size_t chunk = bytes < sizeof(buffer) ? bytes : sizeof(buffer);
    if (source.read(buffer, chunk) != static_cast<int>(chunk) || dest.write(buffer, chunk) != chunk) return false;
    bytes -= chunk;
  }
  return true;
}

// Copies entries, folding segments into an existing (day, book) record.
bool copyEntriesMerging(FsFile& source, FsFile& dest, uint32_t entryCount, const uint16_t bookId, Segment* segments,
                        const size_t segmentCount) {
  uint8_t buffer[COPY_CHUNK_ENTRIES * ENTRY_RECORD_SIZE];
  while (entryCount > 0) {
    const size_t chunkEntries = entryCount < COPY_CHUNK_ENTRIES ? entryCount : COPY_CHUNK_ENTRIES;
    const size_t chunkBytes = chunkEntries * ENTRY_RECORD_SIZE;
    if (source.read(buffer, chunkBytes) != static_cast<int>(chunkBytes)) return false;
    for (size_t i = 0; i < chunkEntries; ++i) {
      uint8_t* record = buffer + i * ENTRY_RECORD_SIZE;
      if (readLe16(record + 2) != bookId) continue;
      const uint16_t day = readLe16(record);
      for (size_t s = 0; s < segmentCount; ++s) {
        if (!segments[s].merged && segments[s].dayIndex == day) {
          writeLe32(record + 4, addSaturated(readLe32(record + 4), segments[s].seconds));
          segments[s].merged = true;
        }
      }
    }
    if (dest.write(buffer, chunkBytes) != chunkBytes) return false;
    entryCount -= chunkEntries;
  }
  return true;
}

size_t splitAtMidnight(const ReadingStatsDateTime& localStart, uint32_t seconds, Segment* segments) {
  ReadingStatsDateTime cursor = localStart;
  size_t count = 0;
  while (seconds > 0) {
    const uint32_t secondsUntilMidnight =
        (24u * 3600u) - (static_cast<uint32_t>(cursor.hour) * 3600u + static_cast<uint32_t>(cursor.minute) * 60u +
                         static_cast<uint32_t>(cursor.second));
    const uint32_t segment = seconds < secondsUntilMidnight ? seconds : secondsUntilMidnight;
    const auto day = static_cast<uint16_t>(readingStatsDayIndex(cursor.date));
    if (count < MAX_SESSION_SEGMENTS) {
      segments[count++] = {day, segment, false};
    } else {
      segments[count - 1].seconds = addSaturated(segments[count - 1].seconds, segment);
    }
    seconds -= segment;
    addSecondsToReadingStatsDateTime(cursor, segment);
  }
  return count;
}

bool publishTmp() {
  if (Storage.exists(HISTORY_BAK_PATH) && !Storage.remove(HISTORY_BAK_PATH)) {
    LOG_ERR("RHIST", "Could not remove old reading history backup");
    Storage.remove(HISTORY_TMP_PATH);
    return false;
  }
  if (Storage.exists(HISTORY_PATH) && !Storage.rename(HISTORY_PATH, HISTORY_BAK_PATH)) {
    LOG_ERR("RHIST", "Could not rotate reading history backup");
    Storage.remove(HISTORY_TMP_PATH);
    return false;
  }
  if (!Storage.rename(HISTORY_TMP_PATH, HISTORY_PATH)) {
    LOG_ERR("RHIST", "Could not publish reading history");
    if (!Storage.exists(HISTORY_PATH) && Storage.exists(HISTORY_BAK_PATH)) {
      Storage.rename(HISTORY_BAK_PATH, HISTORY_PATH);
    }
    Storage.remove(HISTORY_TMP_PATH);
    return false;
  }
  return true;
}
}  // namespace

bool DailyReadingLog::recordSession(const char* title, const ReadingStatsDateTime& localStart,
                                    const uint32_t seconds) {
  if (!localStart.isValid() || seconds == 0) return false;
  if (s_blockWrites) {
    LOG_ERR("RHIST", "Skipping reading history write after newer-format file was detected");
    return false;
  }

  Segment segments[MAX_SESSION_SEGMENTS];
  const size_t segmentCount = splitAtMidnight(localStart, seconds, segments);
  char key[TITLE_BYTES + 1];
  const size_t keyLen = copyTitle(key, title);

  FsFile source;
  Header header;
  bool hasSource = false;
  if (!openSource(source, header, hasSource)) return false;

  uint16_t bookId = header.titleCount;
  bool isNewBook = true;
  if (hasSource) {
    isNewBook = !findBookId(source, header, key, keyLen, bookId);
    if (isNewBook) bookId = header.titleCount;
    if (isNewBook && header.titleCount == std::numeric_limits<uint16_t>::max()) {
      LOG_ERR("RHIST", "Reading history title table is full");
      source.close();
      return false;
    }
    if (!source.seek(HEADER_SIZE)) {
      LOG_ERR("RHIST", "Could not rewind reading history");
      source.close();
      return false;
    }
  }

  if (Storage.exists(HISTORY_TMP_PATH)) Storage.remove(HISTORY_TMP_PATH);
  FsFile dest;
  if (!Storage.openFileForWrite("RHIST", HISTORY_TMP_PATH, dest)) {
    LOG_ERR("RHIST", "Could not write reading history temp file");
    if (hasSource) source.close();
    return false;
  }

  // The header is rewritten with final counts once the entries are known.
  uint8_t headerData[HEADER_SIZE] = {HISTORY_VERSION, 0};
  bool ok = dest.write(headerData, HEADER_SIZE) == HEADER_SIZE;
  if (ok && hasSource) ok = copyBytes(source, dest, static_cast<size_t>(header.titleCount) * TITLE_RECORD_SIZE);
  Header outHeader = header;
  if (ok && isNewBook) {
    uint8_t record[TITLE_RECORD_SIZE] = {};
    record[0] = static_cast<uint8_t>(keyLen);
    memcpy(record + 1, key, keyLen);
    ok = dest.write(record, TITLE_RECORD_SIZE) == TITLE_RECORD_SIZE;
    outHeader.titleCount++;
  }
  if (ok && hasSource) ok = copyEntriesMerging(source, dest, header.entryCount, bookId, segments, segmentCount);
  if (hasSource) source.close();
  for (size_t s = 0; ok && s < segmentCount; ++s) {
    if (segments[s].merged) continue;
    uint8_t record[ENTRY_RECORD_SIZE];
    writeLe16(record, segments[s].dayIndex);
    writeLe16(record + 2, bookId);
    writeLe32(record + 4, segments[s].seconds);
    ok = dest.write(record, ENTRY_RECORD_SIZE) == ENTRY_RECORD_SIZE;
    outHeader.entryCount++;
  }
  if (ok) {
    writeLe16(headerData + 2, outHeader.titleCount);
    writeLe32(headerData + 4, outHeader.entryCount);
    ok = dest.seek(0) && dest.write(headerData, HEADER_SIZE) == HEADER_SIZE;
  }
  dest.flush();
  ok = ok && dest.sync() && dest.fileSize() == expectedFileSize(outHeader);
  if (!dest.close() || !ok) {
    LOG_ERR("RHIST", "Failed to write reading history temp file");
    Storage.remove(HISTORY_TMP_PATH);
    return false;
  }
  if (!publishTmp()) return false;

  LOG_DBG("RHIST", "Recorded %lus for \"%s\" (%u books, %lu day entries)", static_cast<unsigned long>(seconds), key,
          static_cast<unsigned>(outHeader.titleCount), static_cast<unsigned long>(outHeader.entryCount));
  return true;
}

bool DailyReadingLog::remove() {
  bool ok = true;
  for (const char* path : {HISTORY_PATH, HISTORY_BAK_PATH, HISTORY_TMP_PATH, LEGACY_DAILY_LOG_PATH}) {
    if (Storage.exists(path) && !Storage.remove(path)) {
      LOG_ERR("RHIST", "Could not remove %s", path);
      ok = false;
    }
  }
  s_blockWrites = false;
  return ok;
}

bool DailyReadingWindow::load(const uint32_t firstDayIndex, const uint32_t lastDayIndex) {
  firstDay = firstDayIndex;
  lastDay = lastDayIndex;
  count = 0;
  bookCount = 0;

  FsFile f;
  Header header;
  OpenResult result = openHistory(HISTORY_PATH, f, header);
  if (result == OpenResult::Invalid) result = openHistory(HISTORY_BAK_PATH, f, header);
  if (result != OpenResult::Ok) return false;

  // Map file book ids to window slots; titles are read after the entry scan.
  std::array<uint16_t, MAX_BOOKS> fileIds{};
  uint32_t dropped = 0;
  bool ok = f.seek(HEADER_SIZE + static_cast<size_t>(header.titleCount) * TITLE_RECORD_SIZE);
  uint8_t buffer[COPY_CHUNK_ENTRIES * ENTRY_RECORD_SIZE];
  uint32_t remaining = header.entryCount;
  while (ok && remaining > 0) {
    const size_t chunkEntries = remaining < COPY_CHUNK_ENTRIES ? remaining : COPY_CHUNK_ENTRIES;
    const size_t chunkBytes = chunkEntries * ENTRY_RECORD_SIZE;
    ok = f.read(buffer, chunkBytes) == static_cast<int>(chunkBytes);
    for (size_t i = 0; ok && i < chunkEntries; ++i) {
      const uint8_t* record = buffer + i * ENTRY_RECORD_SIZE;
      const uint16_t day = readLe16(record);
      if (day < firstDay || day > lastDay) continue;
      const uint16_t fileId = readLe16(record + 2);
      uint8_t slot = 0;
      while (slot < bookCount && fileIds[slot] != fileId) slot++;
      if (slot == bookCount) {
        if (bookCount == MAX_BOOKS || fileId >= header.titleCount) {
          dropped++;
          continue;
        }
        fileIds[bookCount++] = fileId;
      }
      if (count == MAX_ENTRIES) {
        dropped++;
        continue;
      }
      entries[count++] = {day, slot, readLe32(record + 4)};
    }
    remaining -= chunkEntries;
  }

  uint8_t titleRecord[TITLE_RECORD_SIZE];
  for (uint8_t slot = 0; ok && slot < bookCount; ++slot) {
    ok = f.seek(HEADER_SIZE + static_cast<size_t>(fileIds[slot]) * TITLE_RECORD_SIZE) &&
         f.read(titleRecord, TITLE_RECORD_SIZE) == static_cast<int>(TITLE_RECORD_SIZE);
    if (!ok) break;
    const size_t len = titleRecord[0] <= DailyReadingLog::TITLE_BYTES ? titleRecord[0] : DailyReadingLog::TITLE_BYTES;
    memcpy(titles[slot].data(), titleRecord + 1, len);
    titles[slot][len] = '\0';
  }
  f.close();

  if (!ok) {
    LOG_ERR("RHIST", "Short read in reading history");
    count = 0;
    bookCount = 0;
    return false;
  }
  if (dropped > 0) {
    LOG_ERR("RHIST", "Reading history window full; skipped %lu entries", static_cast<unsigned long>(dropped));
  }
  return true;
}

void DailyReadingWindow::addSeconds(const uint32_t dayIndex, const char* title, const uint32_t seconds) {
  if (seconds == 0 || dayIndex < firstDay || dayIndex > lastDay) return;
  char key[DailyReadingLog::TITLE_BYTES + 1];
  copyTitle(key, title);

  uint8_t slot = 0;
  while (slot < bookCount && strcmp(titles[slot].data(), key) != 0) slot++;
  if (slot == bookCount) {
    if (bookCount == MAX_BOOKS) return;
    memcpy(titles[slot].data(), key, sizeof(key));
    bookCount++;
  }
  for (uint16_t i = 0; i < count; ++i) {
    if (entries[i].dayIndex == dayIndex && entries[i].book == slot) {
      entries[i].seconds = addSaturated(entries[i].seconds, seconds);
      return;
    }
  }
  if (count == MAX_ENTRIES) return;
  entries[count++] = {static_cast<uint16_t>(dayIndex), slot, seconds};
}

void DailyReadingWindow::addSpan(const char* title, const ReadingStatsDateTime& localStart, const uint32_t seconds) {
  if (!localStart.isValid() || seconds == 0) return;
  Segment segments[MAX_SESSION_SEGMENTS];
  const size_t segmentCount = splitAtMidnight(localStart, seconds, segments);
  for (size_t s = 0; s < segmentCount; ++s) {
    addSeconds(segments[s].dayIndex, title, segments[s].seconds);
  }
}
