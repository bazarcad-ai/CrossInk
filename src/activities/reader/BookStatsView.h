#pragma once

#include <string>

#include "BookReadingStats.h"
#include "DailyReadingLog.h"
#include "GlobalReadingStats.h"

class GfxRenderer;
class MappedInputManager;

namespace BookStatsTouchTarget {
constexpr int StartedDaysStat = 1;
constexpr int DateFieldBase = 10;
constexpr int DateFieldCount = 6;
constexpr int DateAdjustUp = 20;
constexpr int DateAdjustDown = 21;
constexpr int DateSave = 22;
constexpr int DateCancel = 23;

constexpr int dateField(const int index) { return DateFieldBase + index; }
}  // namespace BookStatsTouchTarget

void renderPerBookStatsPage(GfxRenderer& renderer, const MappedInputManager* mappedInput, const std::string& bookTitle,
                            const BookReadingStats& stats, float progressPercent, bool hasEstimatedTimeLeft,
                            uint32_t estimatedTimeLeftSeconds, bool showButtonHints, bool showEditButton,
                            bool showMoreButton);

void renderGlobalStatsPage(GfxRenderer& renderer, const MappedInputManager* mappedInput, const char* screenTitle,
                           const GlobalReadingStats& stats, bool showButtonHints, bool showMoreButton);

void renderNoRtcCombinedStatsPage(GfxRenderer& renderer, const MappedInputManager* mappedInput,
                                  const std::string& bookTitle, const BookReadingStats& bookStats,
                                  float progressPercent, bool hasEstimatedTimeLeft, uint32_t estimatedTimeLeftSeconds,
                                  const GlobalReadingStats& deviceStats, const GlobalReadingStats* allDevicesStats,
                                  bool showButtonHints);

void renderEditBookDatesPage(GfxRenderer& renderer, const MappedInputManager* mappedInput, const std::string& bookTitle,
                             const BookReadingStats& stats, int selectedField, bool showButtonHints);

// Reading log pages cover this many local days ending today.
constexpr uint32_t READING_LOG_WINDOW_DAYS = 30;

// Number of Monday-based calendar weeks the reading log window touches.
int readingLogWeekCount(const ReadingStatsDate& today);

// Totals, weekly bars, and top books for the reading log window. A null
// window renders an empty month.
void renderReadingSummaryPage(GfxRenderer& renderer, const MappedInputManager* mappedInput,
                              const DailyReadingWindow* window, const ReadingStatsDate& today, bool showButtonHints);

// Day-by-day, per-book reading time for one calendar week (0 = this week).
void renderReadingWeekPage(GfxRenderer& renderer, const MappedInputManager* mappedInput,
                           const DailyReadingWindow* window, const ReadingStatsDate& today, int weekIndex,
                           bool showButtonHints, bool showMoreButton);
