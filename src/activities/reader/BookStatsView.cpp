#include "BookStatsView.h"

#include <FreeInkUICore.h>
#include <GfxRenderer.h>
#include <HalClock.h>
#include <I18n.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <string>

#include "MappedInputManager.h"
#include "components/CompactHeader.h"
#include "components/TouchActionButtons.h"
#include "components/TouchHeaderBackButton.h"
#include "components/TouchRegistry.h"
#include "components/UITheme.h"
#include "components/icons/listIcons.h"
#include "fontIds.h"

namespace {
constexpr int kStatsButtonHintTopGap = 10;
constexpr int kStandaloneNoRtcMaxTopCardHeightDivisor = 2;
constexpr int kStandaloneNoRtcMaxVerticalOffset = 32;
constexpr int kPerBookRtcTopCardMaxExtra = 84;

struct StatsLayout {
  int headerHeight;
  int headerDrawHeight;
  int topGap;
  int cardGap;
  int topCardTitleH;
  int topCardH;
  int globalCardH;
  int sectionTitleH;
  int sectionTitleFontId;
  int chartLabelFontId;
  int chartLabelW;
  int barH;
  int barGap;
  int chartTopPadding;
  int chartBottomPadding;
};

constexpr StatsLayout kDefaultLayout = {
    .headerHeight = 78,
    .headerDrawHeight = 67,
    .topGap = 8,
    .cardGap = 26,
    .topCardTitleH = 36,
    .topCardH = 214,
    .globalCardH = 154,
    .sectionTitleH = 34,
    .sectionTitleFontId = UI_10_FONT_ID,
    .chartLabelFontId = UI_10_FONT_ID,
    .chartLabelW = 88,
    .barH = 22,
    .barGap = 12,
    .chartTopPadding = 14,
    .chartBottomPadding = 14,
};

constexpr StatsLayout kCompactLayout = {
    .headerHeight = 67,
    .headerDrawHeight = 67,
    .topGap = 6,
    .cardGap = 8,
    .topCardTitleH = 30,
    .topCardH = 156,
    .globalCardH = 110,
    .sectionTitleH = 30,
    .sectionTitleFontId = UI_10_FONT_ID,
    .chartLabelFontId = SMALL_FONT_ID,
    .chartLabelW = 78,
    .barH = 16,
    .barGap = 8,
    .chartTopPadding = 8,
    .chartBottomPadding = 8,
};

constexpr std::array<StrId, READING_TIME_BUCKET_COUNT> TIME_BUCKET_LABELS = {
    StrId::STR_STATS_MORNING, StrId::STR_STATS_AFTERNOON, StrId::STR_STATS_EVENING, StrId::STR_STATS_NIGHT};
constexpr std::array<StrId, READING_DAY_OF_WEEK_COUNT> DAY_LABELS = {
    StrId::STR_STATS_MON, StrId::STR_STATS_TUE, StrId::STR_STATS_WED, StrId::STR_STATS_THU,
    StrId::STR_STATS_FRI, StrId::STR_STATS_SAT, StrId::STR_STATS_SUN};

const char* dayCountText(const uint16_t days) { return days == 1 ? tr(STR_STATS_DAY) : tr(STR_STATS_DAYS); }

int sectionCardHeight(const StatsLayout& layout, const int rowCount) {
  if (rowCount <= 0) {
    return layout.sectionTitleH + layout.chartTopPadding + layout.chartBottomPadding;
  }
  const int rowStride = layout.barH + layout.barGap;
  return layout.sectionTitleH + layout.chartTopPadding + layout.chartBottomPadding + layout.barH +
         (rowCount - 1) * rowStride;
}

bool shouldShowRtcBasedStats() { return halClock.isAvailable(); }

int noRtcCardBaseHeight(const StatsLayout& layout) { return layout.globalCardH; }

int statsHeaderHeight(const ThemeMetrics& metrics, const StatsLayout& layout, const MappedInputManager* mappedInput) {
  if (mappedInput && mappedInput->hasTouchHardware()) {
    return CompactHeader::height(metrics);
  }
  return std::min(metrics.headerHeight, layout.headerHeight);
}

int statsContentHeight(const StatsLayout& layout, const int headerHeight, const bool globalPage,
                       const bool showRtcStats) {
  const int topCardH = globalPage ? layout.globalCardH : layout.topCardH;
  if (!showRtcStats) {
    return headerHeight + layout.topGap + topCardH;
  }
  const int timeOfDayH = sectionCardHeight(layout, static_cast<int>(TIME_BUCKET_LABELS.size()));
  const int dayOfWeekH = sectionCardHeight(layout, static_cast<int>(DAY_LABELS.size()));
  return headerHeight + layout.topGap + topCardH + layout.cardGap + timeOfDayH + layout.cardGap + dayOfWeekH;
}

int noRtcCombinedContentHeight(const StatsLayout& layout, const int headerHeight, const bool showAllDevicesStats) {
  const int cardBaseH = noRtcCardBaseHeight(layout);
  return headerHeight + layout.topGap + cardBaseH + layout.cardGap + layout.globalCardH +
         (showAllDevicesStats ? layout.cardGap + layout.globalCardH : 0);
}

int statsBottomInset(const ThemeMetrics& metrics, const bool showButtonHints) {
  return metrics.verticalSpacing + (showButtonHints ? metrics.buttonHintsHeight + kStatsButtonHintTopGap : 0);
}

int perBookRtcTopCardHeight(const StatsLayout& layout, const int extraHeight) {
  return layout.topCardH + std::min(extraHeight, kPerBookRtcTopCardMaxExtra);
}

int globalRtcCardHeightForPerBookRowSpacing(const StatsLayout& layout, const int perBookExtraHeight) {
  constexpr int perBookDataRowCount = 3;
  constexpr int globalDataRowCount = 2;
  const int perBookDataRowH =
      (perBookRtcTopCardHeight(layout, perBookExtraHeight) - layout.topCardTitleH) / perBookDataRowCount;
  return std::max(layout.globalCardH, layout.topCardTitleH + perBookDataRowH * globalDataRowCount);
}

const StatsLayout& getStatsLayout(const GfxRenderer& renderer, const MappedInputManager* mappedInput,
                                  const bool globalPage, const bool showButtonHints, const bool showRtcStats) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int availableHeight =
      renderer.getScreenHeight() - metrics.topPadding - statsBottomInset(metrics, showButtonHints);
  const int defaultHeaderHeight = statsHeaderHeight(metrics, kDefaultLayout, mappedInput);
  const bool defaultFitsCurrentPage =
      statsContentHeight(kDefaultLayout, defaultHeaderHeight, globalPage, showRtcStats) <= availableHeight;
  const bool defaultMatchesPerBookCharts =
      !globalPage || !showRtcStats ||
      statsContentHeight(kDefaultLayout, defaultHeaderHeight, false, showRtcStats) <= availableHeight;
  if (defaultFitsCurrentPage && defaultMatchesPerBookCharts) {
    return kDefaultLayout;
  }
  return kCompactLayout;
}

const StatsLayout& getNoRtcCombinedLayout(const GfxRenderer& renderer, const MappedInputManager* mappedInput,
                                          const bool showButtonHints, const bool showAllDevicesStats) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int availableHeight =
      renderer.getScreenHeight() - metrics.topPadding - statsBottomInset(metrics, showButtonHints);
  if (noRtcCombinedContentHeight(kDefaultLayout, statsHeaderHeight(metrics, kDefaultLayout, mappedInput),
                                 showAllDevicesStats) <= availableHeight) {
    return kDefaultLayout;
  }
  return kCompactLayout;
}

bool fallbackEstimatedTimeLeft(const BookReadingStats& stats, const float progressPercent, uint32_t& seconds) {
  seconds = 0;
  if (progressPercent <= 0.0f || progressPercent >= 100.0f || stats.totalReadingSeconds < 120) {
    return false;
  }

  const float progress = progressPercent / 100.0f;
  const float estimate = (static_cast<float>(stats.totalReadingSeconds) * (1.0f - progress)) / progress;
  if (estimate <= 0.0f) {
    return false;
  }
  seconds = static_cast<uint32_t>(estimate + 0.5f);
  return seconds > 0;
}

bool cachedEstimatedTimeLeft(const BookReadingStats& stats, uint32_t& seconds) {
  seconds = stats.estimatedTimeLeftSeconds;
  return seconds > 0;
}

bool estimateFinishDateFromDailyPace(const BookReadingStats& stats, const ReadingStatsDateTime& today,
                                     const uint32_t estimatedReadingSeconds, ReadingStatsDate& outDate) {
  outDate = {};
  if (!today.isValid() || !stats.startDate.isValid() || estimatedReadingSeconds == 0 ||
      stats.totalReadingSeconds == 0) {
    return false;
  }

  const uint16_t elapsedDays = readingSpanDaysElapsed(stats.startDate, today.date);
  const uint16_t readingDays = std::max<uint16_t>(1, elapsedDays);

  // Convert remaining reading time into calendar time using the book's average reading seconds per calendar day.
  const uint64_t estimatedCalendarSeconds =
      (static_cast<uint64_t>(estimatedReadingSeconds) * static_cast<uint64_t>(readingDays) * 86400ULL +
       static_cast<uint64_t>(stats.totalReadingSeconds) / 2ULL) /
      static_cast<uint64_t>(stats.totalReadingSeconds);
  if (estimatedCalendarSeconds == 0) {
    return false;
  }

  ReadingStatsDateTime estimatedFinish = today;
  addSecondsToReadingStatsDateTime(estimatedFinish,
                                   static_cast<uint32_t>(std::min<uint64_t>(estimatedCalendarSeconds, UINT32_MAX)));
  outDate = estimatedFinish.date;
  return outDate.isValid();
}

float pagesPerMinute(const uint32_t totalPagesTurned, const uint32_t totalReadingSeconds) {
  if (totalReadingSeconds <= 60) {
    return 0.0f;
  }
  return static_cast<float>(totalPagesTurned) * 60.0f / static_cast<float>(totalReadingSeconds);
}

void drawCenteredLabel(const GfxRenderer& renderer, const int fontId, const int x, const int w, const int y,
                       const char* text, const bool bold = false) {
  const int textWidth = renderer.getTextWidth(fontId, text, bold ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
  renderer.drawText(fontId, x + (w - textWidth) / 2, y, text, true,
                    bold ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
}

void drawStatCell(const GfxRenderer& renderer, const int x, const int w, const int y, const int h, const char* value,
                  const char* label) {
  const int valueLineH = renderer.getLineHeight(UI_12_FONT_ID);
  const int labelLineH = renderer.getLineHeight(SMALL_FONT_ID);
  const int totalTextH = valueLineH + 4 + labelLineH;
  const int textY = y + (h - totalTextH) / 2;
  drawCenteredLabel(renderer, UI_12_FONT_ID, x, w, textY, value, true);
  drawCenteredLabel(renderer, SMALL_FONT_ID, x, w, textY + valueLineH + 4, label);
}

void drawSectionCard(const GfxRenderer& renderer, const int x, const int y, const int w, const int h, const char* title,
                     const StatsLayout& layout) {
  renderer.drawRect(x, y, w, h);
  renderer.drawLine(x, y + layout.sectionTitleH, x + w, y + layout.sectionTitleH);
  drawCenteredLabel(renderer, layout.sectionTitleFontId, x, w,
                    y + (layout.sectionTitleH - renderer.getLineHeight(layout.sectionTitleFontId)) / 2, title, true);
}

template <size_t N>
void drawHorizontalBars(GfxRenderer& renderer, const int x, const int y, const int w, const int h,
                        const std::array<uint32_t, N>& values, const std::array<StrId, N>& labels,
                        const StatsLayout& layout) {
  constexpr int labelLeftPadding = 10;
  constexpr int labelRightPadding = 18;
  constexpr int barLeftGap = 8;
  constexpr int rightPadding = 18;
  const uint32_t maxValue = *std::max_element(values.begin(), values.end());
  const int labelLineH = renderer.getLineHeight(layout.chartLabelFontId);
  const int rowContentH = std::max(labelLineH, layout.barH);
  const int baseContentH = layout.sectionTitleH + layout.chartTopPadding + layout.chartBottomPadding + rowContentH +
                           (static_cast<int>(N) - 1) * (rowContentH + layout.barGap);
  const int extraHeight = std::max(0, h - baseContentH);
  const int spacingSlotCount = static_cast<int>(N) + 1;
  const int extraPerSlot = spacingSlotCount > 0 ? extraHeight / spacingSlotCount : 0;
  const int extraRemainder = spacingSlotCount > 0 ? extraHeight % spacingSlotCount : 0;
  const int topPadding = layout.chartTopPadding + extraPerSlot + (extraRemainder > 0 ? 1 : 0);
  const int rowGap = layout.barGap + extraPerSlot;
  const int contentTop = y + layout.sectionTitleH + topPadding;
  const int rowStride = rowContentH + rowGap;
  int maxLabelW = 0;
  for (size_t i = 0; i < N; ++i) {
    maxLabelW = std::max(maxLabelW, renderer.getTextWidth(layout.chartLabelFontId, I18N.get(labels[i])));
  }
  const int labelColumnW = std::max(layout.chartLabelW, labelLeftPadding + maxLabelW + labelRightPadding);
  const int barX = x + labelColumnW + barLeftGap;
  const int barW = std::max(0, w - labelColumnW - barLeftGap - rightPadding);
  for (size_t i = 0; i < N; ++i) {
    const int rowTop = contentTop + static_cast<int>(i) * rowStride;
    const int labelY = rowTop + (rowContentH - labelLineH) / 2;
    const int barY = rowTop + (rowContentH - layout.barH) / 2;
    renderer.drawText(layout.chartLabelFontId, x + labelLeftPadding, labelY, I18N.get(labels[i]));
    if (maxValue > 0 && values[i] > 0) {
      const int fillW = std::max(2, static_cast<int>((static_cast<uint64_t>(barW) * values[i]) / maxValue));
      renderer.fillRect(barX, barY, fillW, layout.barH, true);
    }
  }
}

void drawPerBookStatsCard(GfxRenderer& renderer, const int x, const int y, const int w, const int h,
                          const std::string& bookTitle, const BookReadingStats& stats, const float progressPercent,
                          const bool hasEstimatedTimeLeft, const uint32_t estimatedTimeLeftSeconds,
                          const StatsLayout& layout) {
  renderer.drawRect(x, y, w, h);
  renderer.drawLine(x, y + layout.topCardTitleH, x + w, y + layout.topCardTitleH);
  const std::string visibleTitle =
      renderer.truncatedText(UI_10_FONT_ID, bookTitle.c_str(), w - 20, EpdFontFamily::BOLD);
  drawCenteredLabel(renderer, UI_10_FONT_ID, x, w,
                    y + (layout.topCardTitleH - renderer.getLineHeight(UI_10_FONT_ID)) / 2, visibleTitle.c_str(), true);

  const bool showRtcStats = shouldShowRtcBasedStats();
  const int thirdW = w / 3;
  const int halfW = w / 2;
  const int rowCount = showRtcStats ? 3 : 2;
  const int rowH = (h - layout.topCardTitleH) / rowCount;
  char buf[40];

  snprintf(buf, sizeof(buf), "%u", static_cast<unsigned>(stats.sessionCount));
  drawStatCell(renderer, x, thirdW, y + layout.topCardTitleH, rowH, buf, tr(STR_STATS_SESSIONS_LBL));

  BookReadingStats::formatDuration(stats.totalReadingSeconds, buf, sizeof(buf));
  drawStatCell(renderer, x + thirdW, thirdW, y + layout.topCardTitleH, rowH, buf, tr(STR_STATS_TIME_LBL));

  if (progressPercent >= 0.0f) {
    snprintf(buf, sizeof(buf), "%d%%", static_cast<int>(progressPercent + 0.5f));
  } else {
    snprintf(buf, sizeof(buf), "-");
  }
  drawStatCell(renderer, x + thirdW * 2, thirdW, y + layout.topCardTitleH, rowH, buf, tr(STR_STATS_PROGRESS_LBL));

  const uint32_t avgSecs = stats.sessionCount > 0 ? stats.totalReadingSeconds / stats.sessionCount : 0;
  BookReadingStats::formatDuration(avgSecs, buf, sizeof(buf));
  drawStatCell(renderer, x, thirdW, y + layout.topCardTitleH + rowH, rowH, buf, tr(STR_STATS_AVG_SESSION_LBL));

  uint32_t fallbackEstimateSeconds = 0;
  uint32_t cachedEstimateSeconds = 0;
  const bool hasCachedEstimate = cachedEstimatedTimeLeft(stats, cachedEstimateSeconds);
  const bool hasFallbackEstimate = fallbackEstimatedTimeLeft(stats, progressPercent, fallbackEstimateSeconds);
  if (!stats.isCompleted && (hasEstimatedTimeLeft || hasCachedEstimate || hasFallbackEstimate)) {
    formatCompactReadingDuration(hasEstimatedTimeLeft ? estimatedTimeLeftSeconds
                                 : hasCachedEstimate  ? cachedEstimateSeconds
                                                      : fallbackEstimateSeconds,
                                 buf, sizeof(buf));
  } else {
    snprintf(buf, sizeof(buf), "-");
  }
  drawStatCell(renderer, x + thirdW, thirdW, y + layout.topCardTitleH + rowH, rowH, buf, tr(STR_TIME_LEFT));

  snprintf(buf, sizeof(buf), "%.1f", pagesPerMinute(stats.totalPagesTurned, stats.totalReadingSeconds));
  drawStatCell(renderer, x + thirdW * 2, thirdW, y + layout.topCardTitleH + rowH, rowH, buf,
               tr(STR_STATS_PAGES_PER_MIN));

  if (!showRtcStats) {
    return;
  }

  ReadingStatsDateTime today;
  const bool hasToday = getCurrentLocalReadingStatsDateTime(today);
  const ReadingStatsDate endDate = stats.isCompleted && stats.finishedDate.isValid()
                                       ? stats.finishedDate
                                       : (hasToday ? today.date : ReadingStatsDate{});
  const bool hasDaySpan = stats.startDate.isValid() && endDate.isValid();
  const uint16_t daysReading = hasDaySpan ? readingSpanDaysElapsed(stats.startDate, endDate) : 0;
  if (hasDaySpan) {
    snprintf(buf, sizeof(buf), "%u %s", static_cast<unsigned>(daysReading), dayCountText(daysReading));
  } else {
    snprintf(buf, sizeof(buf), "-");
  }
  char startedLabel[32];
  char dateBuf[24];
  formatReadingStatsShortDate(stats.startDate, dateBuf, sizeof(dateBuf));
  snprintf(startedLabel, sizeof(startedLabel), "%s %s", tr(STR_STATS_STARTED), dateBuf);
  const int startedY = y + layout.topCardTitleH + rowH * 2;
  TouchRegistry::getInstance().add(Rect(x, startedY, halfW, rowH), BookStatsTouchTarget::StartedDaysStat,
                                   TouchRegistry::Item);
  drawStatCell(renderer, x, halfW, startedY, rowH, buf, startedLabel);

  ReadingStatsDate finishDisplayDate;
  bool finished = stats.isCompleted;
  if (finished) {
    finishDisplayDate = stats.finishedDate;
  } else if (hasToday && (hasEstimatedTimeLeft || hasCachedEstimate || hasFallbackEstimate)) {
    const uint32_t remainingReadingSeconds = hasEstimatedTimeLeft ? estimatedTimeLeftSeconds
                                             : hasCachedEstimate  ? cachedEstimateSeconds
                                                                  : fallbackEstimateSeconds;
    if (!estimateFinishDateFromDailyPace(stats, today, remainingReadingSeconds, finishDisplayDate)) {
      ReadingStatsDateTime estimatedFinish = today;
      addSecondsToReadingStatsDateTime(estimatedFinish, remainingReadingSeconds);
      finishDisplayDate = estimatedFinish.date;
    }
  }
  formatReadingStatsShortDate(finishDisplayDate, buf, sizeof(buf));
  drawStatCell(renderer, x + halfW, halfW, y + layout.topCardTitleH + rowH * 2, rowH, buf,
               finished ? tr(STR_STATS_FINISHED_DATE) : tr(STR_STATS_EST_FINISH_DATE));
}

void drawGlobalStatsCard(GfxRenderer& renderer, const int x, const int y, const int w, const int h, const char* title,
                         const GlobalReadingStats& stats, const StatsLayout& layout) {
  renderer.drawRect(x, y, w, h);
  renderer.drawLine(x, y + layout.topCardTitleH, x + w, y + layout.topCardTitleH);
  const bool showRtcStats = shouldShowRtcBasedStats();
  drawCenteredLabel(renderer, UI_10_FONT_ID, x, w,
                    y + (layout.topCardTitleH - renderer.getLineHeight(UI_10_FONT_ID)) / 2, title, true);

  const int thirdW = w / 3;
  const int halfW = w / 2;
  const int rowH = (h - layout.topCardTitleH) / 2;
  char buf[40];

  snprintf(buf, sizeof(buf), "%lu", static_cast<unsigned long>(stats.totalSessions));
  drawStatCell(renderer, x, thirdW, y + layout.topCardTitleH, rowH, buf, tr(STR_STATS_SESSIONS_LBL));

  BookReadingStats::formatDuration(stats.totalReadingSeconds, buf, sizeof(buf));
  drawStatCell(renderer, x + thirdW, thirdW, y + layout.topCardTitleH, rowH, buf, tr(STR_STATS_TIME_LBL));

  snprintf(buf, sizeof(buf), "%.1f", pagesPerMinute(stats.totalPagesTurned, stats.totalReadingSeconds));
  drawStatCell(renderer, x + thirdW * 2, thirdW, y + layout.topCardTitleH, rowH, buf, tr(STR_STATS_PAGES_PER_MIN));

  const uint32_t avgSecs = stats.totalSessions > 0 ? stats.totalReadingSeconds / stats.totalSessions : 0;
  BookReadingStats::formatDuration(avgSecs, buf, sizeof(buf));
  if (showRtcStats) {
    drawStatCell(renderer, x, thirdW, y + layout.topCardTitleH + rowH, rowH, buf, tr(STR_STATS_AVG_SESSION_LBL));
  } else {
    drawStatCell(renderer, x, halfW, y + layout.topCardTitleH + rowH, rowH, buf, tr(STR_STATS_AVG_SESSION_LBL));
  }

  if (showRtcStats) {
    ReadingStatsDateTime today;
    const bool hasToday = getCurrentLocalReadingStatsDateTime(today);
    const uint16_t currentStreak = hasToday ? stats.currentReadingStreak(&today.date) : 0;
    if (currentStreak > 0) {
      snprintf(buf, sizeof(buf), "%u %s", static_cast<unsigned>(currentStreak), dayCountText(currentStreak));
    } else {
      snprintf(buf, sizeof(buf), "-");
    }
    drawStatCell(renderer, x + thirdW, thirdW, y + layout.topCardTitleH + rowH, rowH, buf,
                 tr(STR_STATS_READING_STREAK_LBL));
  }

  if (stats.completedBooks > 0) {
    snprintf(buf, sizeof(buf), "%lu", static_cast<unsigned long>(stats.completedBooks));
  } else {
    snprintf(buf, sizeof(buf), "-");
  }
  drawStatCell(renderer, showRtcStats ? x + thirdW * 2 : x + halfW, showRtcStats ? thirdW : halfW,
               y + layout.topCardTitleH + rowH, rowH, buf, tr(STR_STATS_COMPLETED_LBL));
}

void drawDateField(const GfxRenderer& renderer, const int x, const int y, const int w, const char* text,
                   const bool selected, const int touchTarget = -1) {
  const int h = renderer.getLineHeight(UI_12_FONT_ID) + 10;
  if (touchTarget >= 0) {
    TouchRegistry::getInstance().add(Rect(x, y, w, h), touchTarget, TouchRegistry::Item);
  }
  renderer.fillRectDither(x, y, w, h, selected ? Color::LightGray : Color::White);
  renderer.drawRect(x, y, w, h, true);
  if (selected) {
    renderer.drawRect(x + 1, y + 1, w - 2, h - 2, true);
  }
  drawCenteredLabel(renderer, UI_12_FONT_ID, x, w, y + 5, text);
}

void drawDateAdjustButton(const GfxRenderer& renderer, const int x, const int y, const int size,
                          const freeink::Icon& icon, const int touchTarget) {
  TouchRegistry::getInstance().add(Rect(x, y, size, size), touchTarget, TouchRegistry::Item);
  renderer.drawRect(x, y, size, size, true);
  const freeink::ui::BitmapRef bitmap{icon.bits, icon.w, icon.h, freeink::ui::BitmapFormat::Mask1, true};
  freeink::ui::forEachBitmapPixel(
      freeink::ui::Rect{static_cast<int16_t>(x), static_cast<int16_t>(y), static_cast<int16_t>(size),
                        static_cast<int16_t>(size)},
      bitmap, freeink::ui::BitmapMode::Center,
      [&renderer](const int16_t px, const int16_t py) { renderer.drawPixel(px, py, true); });
}
// Draws left text (truncated to fit) and right-aligned value on one row.
void drawLabelValueRow(const GfxRenderer& renderer, const int fontId, const int x, const int w, const int y,
                       const char* label, const char* value, const EpdFontFamily::Style style) {
  constexpr int kLabelValueGap = 12;
  const int valueW = renderer.getTextWidth(fontId, value, style);
  renderer.drawText(fontId, x + w - valueW, y, value, true, style);
  const int labelMaxW = std::max(0, w - valueW - kLabelValueGap);
  const std::string fitted = renderer.truncatedText(fontId, label, labelMaxW, style);
  renderer.drawText(fontId, x, y, fitted.c_str(), true, style);
}

void formatLogDuration(const uint32_t seconds, char* buf, const size_t len) {
  if (seconds == 0) {
    snprintf(buf, len, "-");
    return;
  }
  formatCompactReadingDuration(seconds, buf, len);
}

}  // namespace

void renderPerBookStatsPage(GfxRenderer& renderer, const MappedInputManager* mappedInput, const std::string& bookTitle,
                            const BookReadingStats& stats, const float progressPercent, const bool hasEstimatedTimeLeft,
                            const uint32_t estimatedTimeLeftSeconds, const bool showButtonHints,
                            const bool showEditButton, const bool showMoreButton) {
  renderer.clearScreen();
  const bool showRtcStats = shouldShowRtcBasedStats();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto& layout = getStatsLayout(renderer, mappedInput, false, showButtonHints, showRtcStats);
  if (mappedInput && mappedInput->hasTouchHardware()) {
    TouchHeaderBackButton::drawCompact(renderer, tr(STR_READING_STATS), false, true);
  } else {
    CompactHeader::drawTitle(renderer, tr(STR_READING_STATS), true);
  }
  const int screenW = renderer.getScreenWidth();
  const int cardX = metrics.contentSidePadding;
  const int cardW = screenW - metrics.contentSidePadding * 2;
  const int availableHeight =
      renderer.getScreenHeight() - metrics.topPadding - statsBottomInset(metrics, showButtonHints);
  int topCardH = layout.topCardH;
  const int headerHeight = statsHeaderHeight(metrics, layout, mappedInput);
  int y = metrics.topPadding + headerHeight + layout.topGap;

  if (showRtcStats) {
    const int timeOfDayH = sectionCardHeight(layout, static_cast<int>(TIME_BUCKET_LABELS.size()));
    const int dayOfWeekH = sectionCardHeight(layout, static_cast<int>(DAY_LABELS.size()));
    const int compactContentHeight =
        headerHeight + layout.topGap + layout.topCardH + layout.cardGap + timeOfDayH + layout.cardGap + dayOfWeekH;
    const int extraHeight = std::max(0, availableHeight - compactContentHeight);
    const int extraTopCardHeight = std::min(extraHeight, kPerBookRtcTopCardMaxExtra);
    const int remainingExtraHeight = extraHeight - extraTopCardHeight;
    const int timeOfDayExtraHeight = (remainingExtraHeight * 4) / 11;
    const int dayOfWeekExtraHeight = remainingExtraHeight - timeOfDayExtraHeight;
    const int timeOfDayCardH = timeOfDayH + timeOfDayExtraHeight;
    const int dayOfWeekCardH = dayOfWeekH + dayOfWeekExtraHeight;
    topCardH += extraTopCardHeight;

    drawPerBookStatsCard(renderer, cardX, y, cardW, topCardH, bookTitle, stats, progressPercent, hasEstimatedTimeLeft,
                         estimatedTimeLeftSeconds, layout);
    y += topCardH + layout.cardGap;

    drawSectionCard(renderer, cardX, y, cardW, timeOfDayCardH, tr(STR_STATS_TIME_OF_DAY), layout);
    drawHorizontalBars(renderer, cardX, y, cardW, timeOfDayCardH, stats.timeOfDaySeconds, TIME_BUCKET_LABELS, layout);
    y += timeOfDayCardH + layout.cardGap;

    drawSectionCard(renderer, cardX, y, cardW, dayOfWeekCardH, tr(STR_STATS_DAY_OF_WEEK), layout);
    drawHorizontalBars(renderer, cardX, y, cardW, dayOfWeekCardH, stats.dayOfWeekSeconds, DAY_LABELS, layout);
  } else {
    const int compactContentHeight = headerHeight + layout.topGap + layout.topCardH;
    const int extraHeight = std::max(0, availableHeight - compactContentHeight);
    if (showButtonHints) {
      topCardH += extraHeight;
    } else {
      // The sleep-screen variant has no footer controls, so on tall portrait displays the
      // single card can balloon and create huge internal gaps between the two stat rows.
      // Cap the card growth and spend the rest as outer margin instead.
      const int maxStandaloneCardHeight =
          std::max(layout.topCardH, renderer.getScreenHeight() / kStandaloneNoRtcMaxTopCardHeightDivisor);
      topCardH = std::min(layout.topCardH + extraHeight, maxStandaloneCardHeight);
      const int unusedExtraHeight = extraHeight - (topCardH - layout.topCardH);
      y += std::min(unusedExtraHeight / 3, kStandaloneNoRtcMaxVerticalOffset);
    }
    drawPerBookStatsCard(renderer, cardX, y, cardW, topCardH, bookTitle, stats, progressPercent, hasEstimatedTimeLeft,
                         estimatedTimeLeftSeconds, layout);
  }

  if (showButtonHints && mappedInput) {
    const auto labels =
        mappedInput->mapLabels(mappedInput->withBackArrow(tr(STR_BACK)), showEditButton ? tr(STR_EDIT) : "", "",
                               showMoreButton ? tr(STR_MORE) : "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4, true);
  }
}

void renderGlobalStatsPage(GfxRenderer& renderer, const MappedInputManager* mappedInput, const char* screenTitle,
                           const GlobalReadingStats& stats, const bool showButtonHints, const bool showMoreButton) {
  renderer.clearScreen();
  const bool showRtcStats = shouldShowRtcBasedStats();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto& layout = getStatsLayout(renderer, mappedInput, true, showButtonHints, showRtcStats);
  if (mappedInput && mappedInput->hasTouchHardware()) {
    TouchHeaderBackButton::drawCompact(renderer, screenTitle, false);
  } else {
    CompactHeader::drawTitle(renderer, screenTitle);
  }
  const int screenW = renderer.getScreenWidth();
  const int cardX = metrics.contentSidePadding;
  const int cardW = screenW - metrics.contentSidePadding * 2;
  const int availableHeight =
      renderer.getScreenHeight() - metrics.topPadding - statsBottomInset(metrics, showButtonHints);
  int globalCardH = layout.globalCardH;
  const int headerHeight = statsHeaderHeight(metrics, layout, mappedInput);
  int y = metrics.topPadding + headerHeight + layout.topGap;

  if (showRtcStats) {
    const int timeOfDayH = sectionCardHeight(layout, static_cast<int>(TIME_BUCKET_LABELS.size()));
    const int dayOfWeekH = sectionCardHeight(layout, static_cast<int>(DAY_LABELS.size()));
    const int compactContentHeight =
        headerHeight + layout.topGap + layout.globalCardH + layout.cardGap + timeOfDayH + layout.cardGap + dayOfWeekH;
    const int extraHeight = std::max(0, availableHeight - compactContentHeight);
    const int perBookCompactContentHeight =
        headerHeight + layout.topGap + layout.topCardH + layout.cardGap + timeOfDayH + layout.cardGap + dayOfWeekH;
    const int perBookExtraHeight = std::max(0, availableHeight - perBookCompactContentHeight);
    const int targetGlobalCardH = globalRtcCardHeightForPerBookRowSpacing(layout, perBookExtraHeight);
    const int extraTopCardHeight = std::min(extraHeight, std::max(0, targetGlobalCardH - layout.globalCardH));
    const int remainingExtraHeight = extraHeight - extraTopCardHeight;
    const int timeOfDayExtraHeight = (remainingExtraHeight * 4) / 11;
    const int dayOfWeekExtraHeight = remainingExtraHeight - timeOfDayExtraHeight;
    const int timeOfDayCardH = timeOfDayH + timeOfDayExtraHeight;
    const int dayOfWeekCardH = dayOfWeekH + dayOfWeekExtraHeight;
    globalCardH += extraTopCardHeight;

    drawGlobalStatsCard(renderer, cardX, y, cardW, globalCardH, tr(STR_STATS_ALL_TIME), stats, layout);
    y += globalCardH + layout.cardGap;

    drawSectionCard(renderer, cardX, y, cardW, timeOfDayCardH, tr(STR_STATS_TIME_OF_DAY), layout);
    drawHorizontalBars(renderer, cardX, y, cardW, timeOfDayCardH, stats.timeOfDaySeconds, TIME_BUCKET_LABELS, layout);
    y += timeOfDayCardH + layout.cardGap;

    drawSectionCard(renderer, cardX, y, cardW, dayOfWeekCardH, tr(STR_STATS_DAY_OF_WEEK), layout);
    drawHorizontalBars(renderer, cardX, y, cardW, dayOfWeekCardH, stats.dayOfWeekSeconds, DAY_LABELS, layout);
  } else {
    const int compactContentHeight = headerHeight + layout.topGap + layout.globalCardH;
    globalCardH += std::max(0, availableHeight - compactContentHeight);
    drawGlobalStatsCard(renderer, cardX, y, cardW, globalCardH, tr(STR_STATS_ALL_TIME), stats, layout);
  }

  if (showButtonHints && mappedInput) {
    const auto labels =
        mappedInput->mapLabels(mappedInput->withBackArrow(tr(STR_EXIT)), "", mappedInput->withBackArrow(tr(STR_BACK)),
                               showMoreButton ? tr(STR_MORE) : "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4, true);
  }
}

void renderNoRtcCombinedStatsPage(GfxRenderer& renderer, const MappedInputManager* mappedInput,
                                  const std::string& bookTitle, const BookReadingStats& bookStats,
                                  const float progressPercent, const bool hasEstimatedTimeLeft,
                                  const uint32_t estimatedTimeLeftSeconds, const GlobalReadingStats& deviceStats,
                                  const GlobalReadingStats* allDevicesStats, const bool showButtonHints) {
  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto& layout = getNoRtcCombinedLayout(renderer, mappedInput, showButtonHints, allDevicesStats != nullptr);
  if (mappedInput && mappedInput->hasTouchHardware()) {
    TouchHeaderBackButton::drawCompact(renderer, tr(STR_READING_STATS), false);
  } else {
    CompactHeader::drawTitle(renderer, tr(STR_READING_STATS));
  }
  const int screenW = renderer.getScreenWidth();
  const int cardX = metrics.contentSidePadding;
  const int cardW = screenW - metrics.contentSidePadding * 2;
  const int availableHeight =
      renderer.getScreenHeight() - metrics.topPadding - statsBottomInset(metrics, showButtonHints);
  const int headerHeight = statsHeaderHeight(metrics, layout, mappedInput);
  const int compactContentHeight = noRtcCombinedContentHeight(layout, headerHeight, allDevicesStats != nullptr);
  const int extraHeight = std::max(0, availableHeight - compactContentHeight);
  const int visibleCardCount = allDevicesStats ? 3 : 2;
  const int extraPerCard = visibleCardCount > 0 ? extraHeight / visibleCardCount : 0;
  const int extraRemainder = visibleCardCount > 0 ? extraHeight % visibleCardCount : 0;
  const int perBookExtraHeight = extraPerCard + (extraRemainder > 0 ? 1 : 0);
  const int deviceExtraHeight = extraPerCard + (extraRemainder > 1 ? 1 : 0);
  const int allDevicesExtraHeight = allDevicesStats ? extraPerCard : 0;
  const int perBookCardH = noRtcCardBaseHeight(layout) + perBookExtraHeight;
  const int deviceCardH = layout.globalCardH + deviceExtraHeight;
  const int allDevicesCardH = layout.globalCardH + allDevicesExtraHeight;

  int y = metrics.topPadding + headerHeight + layout.topGap;
  drawPerBookStatsCard(renderer, cardX, y, cardW, perBookCardH, bookTitle, bookStats, progressPercent,
                       hasEstimatedTimeLeft, estimatedTimeLeftSeconds, layout);
  y += perBookCardH + layout.cardGap;

  drawGlobalStatsCard(renderer, cardX, y, cardW, deviceCardH, tr(STR_STATS_THIS_DEVICE_SCREEN), deviceStats, layout);
  y += deviceCardH;

  if (allDevicesStats) {
    y += layout.cardGap;
    drawGlobalStatsCard(renderer, cardX, y, cardW, allDevicesCardH, tr(STR_STATS_ALL_DEVICES_SCREEN), *allDevicesStats,
                        layout);
  }

  if (showButtonHints && mappedInput) {
    const auto labels = mappedInput->mapLabels(mappedInput->withBackArrow(tr(STR_BACK)), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4, true);
  }
}

void renderEditBookDatesPage(GfxRenderer& renderer, const MappedInputManager* mappedInput, const std::string& bookTitle,
                             const BookReadingStats& stats, const int selectedField, const bool showButtonHints) {
  renderer.clearScreen();
  if (mappedInput && mappedInput->hasTouchHardware()) {
    TouchHeaderBackButton::drawCompact(renderer, tr(STR_READING_STATS), false);
  } else {
    CompactHeader::drawTitle(renderer, tr(STR_READING_STATS));
  }

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int cardW = pageWidth - 120;
  const int cardH = 250;
  const int cardX = (pageWidth - cardW) / 2;
  const int cardY = 138;

  const std::string visibleTitle =
      renderer.truncatedText(UI_12_FONT_ID, bookTitle.c_str(), pageWidth - 80, EpdFontFamily::BOLD);
  renderer.drawCenteredText(UI_12_FONT_ID, 96, visibleTitle.c_str(), true, EpdFontFamily::BOLD);
  renderer.drawRect(cardX, cardY, cardW, cardH);

  const int sectionGap = 104;
  const int row1Y = cardY + 66;
  const int row2Y = row1Y + sectionGap;
  const int monthW = 52;
  const int dayW = 46;
  const int yearW = 68;
  const int gap = 14;
  const int totalFieldW = monthW + gap + dayW + gap + yearW;
#if CROSSINK_APP_CAP_TOUCH
  const bool showTouchControls = mappedInput && mappedInput->hasTouch();
  constexpr int adjustButtonSize = 60;
  constexpr int adjustButtonRightPadding = 34;
  constexpr int adjustButtonGap = 24;
  const int adjustButtonX = cardX + cardW - adjustButtonRightPadding - adjustButtonSize;
  const int fieldAreaW = showTouchControls ? adjustButtonX - cardX - adjustButtonGap : cardW;
#else
  const int fieldAreaW = cardW;
#endif
  const int fieldStartX = cardX + (std::max(totalFieldW, fieldAreaW) - totalFieldW) / 2;

  char monthBuf[8];
  char dayBuf[8];
  char yearBuf[8];

  drawCenteredLabel(renderer, UI_10_FONT_ID, cardX, cardW, cardY + 24, tr(STR_STATS_START_DATE), true);
  formatReadingStatsMonthToken(stats.startDate, monthBuf, sizeof(monthBuf));
  snprintf(dayBuf, sizeof(dayBuf), "%s", stats.startDate.isValid() ? "" : "-");
  if (stats.startDate.isValid()) {
    snprintf(dayBuf, sizeof(dayBuf), "%02u", static_cast<unsigned>(stats.startDate.day));
    snprintf(yearBuf, sizeof(yearBuf), "%u", static_cast<unsigned>(stats.startDate.year));
  } else {
    snprintf(dayBuf, sizeof(dayBuf), "-");
    snprintf(yearBuf, sizeof(yearBuf), "-");
  }
  drawDateField(renderer, fieldStartX, row1Y, monthW, monthBuf, selectedField == 0, BookStatsTouchTarget::dateField(0));
  drawDateField(renderer, fieldStartX + monthW + gap, row1Y, dayW, dayBuf, selectedField == 1,
                BookStatsTouchTarget::dateField(1));
  drawDateField(renderer, fieldStartX + monthW + gap + dayW + gap, row1Y, yearW, yearBuf, selectedField == 2,
                BookStatsTouchTarget::dateField(2));

  drawCenteredLabel(renderer, UI_10_FONT_ID, cardX, cardW, cardY + 24 + sectionGap, tr(STR_STATS_FINISHED_DATE), true);
  const bool showFinishedFields = stats.isCompleted && stats.finishedDate.isValid();
  formatReadingStatsMonthToken(showFinishedFields ? stats.finishedDate : ReadingStatsDate{}, monthBuf,
                               sizeof(monthBuf));
  if (showFinishedFields) {
    snprintf(dayBuf, sizeof(dayBuf), "%02u", static_cast<unsigned>(stats.finishedDate.day));
    snprintf(yearBuf, sizeof(yearBuf), "%u", static_cast<unsigned>(stats.finishedDate.year));
  } else {
    snprintf(dayBuf, sizeof(dayBuf), "-");
    snprintf(yearBuf, sizeof(yearBuf), "-");
  }
  drawDateField(renderer, fieldStartX, row2Y, monthW, monthBuf, selectedField == 3, BookStatsTouchTarget::dateField(3));
  drawDateField(renderer, fieldStartX + monthW + gap, row2Y, dayW, dayBuf, selectedField == 4,
                BookStatsTouchTarget::dateField(4));
  drawDateField(renderer, fieldStartX + monthW + gap + dayW + gap, row2Y, yearW, yearBuf, selectedField == 5,
                BookStatsTouchTarget::dateField(5));

#if CROSSINK_APP_CAP_TOUCH
  if (showTouchControls) {
    const int fieldH = renderer.getLineHeight(UI_12_FONT_ID) + 10;
    drawDateAdjustButton(renderer, adjustButtonX, row1Y + (fieldH - adjustButtonSize) / 2, adjustButtonSize,
                         icon_chevron_up_32, BookStatsTouchTarget::DateAdjustUp);
    drawDateAdjustButton(renderer, adjustButtonX, row2Y + (fieldH - adjustButtonSize) / 2, adjustButtonSize,
                         icon_chevron_down_32, BookStatsTouchTarget::DateAdjustDown);

    constexpr int actionHeight = TouchActionButtons::kDefaultHeight;
    constexpr int actionGap = TouchActionButtons::kDefaultGap;
    constexpr int actionTotalHeight = actionHeight * 2 + actionGap;
    const Rect safeArea = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
    const Rect actionArea{safeArea.x + metrics.contentSidePadding,
                          safeArea.y + safeArea.height - metrics.verticalSpacing - actionTotalHeight,
                          safeArea.width - metrics.contentSidePadding * 2, actionTotalHeight};
    const auto actions = TouchActionButtons::vertical(actionArea, 2);
    const char* labels[] = {tr(STR_SAVE), tr(STR_CANCEL)};
    TouchActionButtons::draw(renderer, actions, labels, 0, -1, UI_10_FONT_ID);
    TouchRegistry::getInstance().add(actions.buttons[0], BookStatsTouchTarget::DateSave, TouchRegistry::Item);
    TouchRegistry::getInstance().add(actions.buttons[1], BookStatsTouchTarget::DateCancel, TouchRegistry::Item);
  }
#endif

  if (showButtonHints && mappedInput) {
    const auto labels = mappedInput->mapLabels(mappedInput->withBackArrow(tr(STR_BACK)), tr(STR_NEXT_FIELD),
                                               tr(STR_DIR_UP), tr(STR_DIR_DOWN));
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4, true);
  }
}

namespace {

int dayOfWeekForIndex(const ReadingStatsDate& today, const uint32_t todayIndex, const uint32_t dayIndex) {
  const int offset = static_cast<int>(todayIndex - dayIndex) % 7;
  return (static_cast<int>(readingStatsDayOfWeekIndex(today)) - offset + 7) % 7;
}

uint32_t readingLogFirstDay(const uint32_t todayIndex) {
  return todayIndex + 1 >= READING_LOG_WINDOW_DAYS ? todayIndex + 1 - READING_LOG_WINDOW_DAYS : 0;
}

// Monday-based calendar week weekIndex weeks back from today's week,
// clipped to the reading log window.
void weekDayRange(const ReadingStatsDate& today, const int weekIndex, uint32_t& firstDay, uint32_t& lastDay,
                  uint32_t& weekStart) {
  const uint32_t todayIndex = readingStatsDayIndex(today);
  weekStart = todayIndex - static_cast<uint32_t>(readingStatsDayOfWeekIndex(today)) - 7u * weekIndex;
  firstDay = std::max(weekStart, readingLogFirstDay(todayIndex));
  lastDay = std::min(weekStart + 6, todayIndex);
}

void formatDayIndexDate(const uint32_t dayIndex, char* buf, const size_t len) {
  ReadingStatsDate date;
  readingStatsDateFromDayIndex(dayIndex, date);
  formatReadingStatsShortDate(date, buf, len);
}

void formatWeekLabel(const int weekIndex, const uint32_t weekStart, char* buf, const size_t len) {
  if (weekIndex == 0) {
    snprintf(buf, len, "%s", tr(STR_STATS_THIS_WEEK));
    return;
  }
  if (weekIndex == 1) {
    snprintf(buf, len, "%s", tr(STR_STATS_LAST_WEEK));
    return;
  }
  char start[16];
  char end[16];
  formatDayIndexDate(weekStart, start, sizeof(start));
  formatDayIndexDate(weekStart + 6, end, sizeof(end));
  snprintf(buf, len, "%s - %s", start, end);
}

uint32_t secondsInRange(const DailyReadingWindow* window, const uint32_t firstDay, const uint32_t lastDay) {
  uint32_t total = 0;
  for (uint16_t i = 0; window && i < window->count; ++i) {
    const auto& entry = window->entries[i];
    if (entry.dayIndex >= firstDay && entry.dayIndex <= lastDay) total += entry.seconds;
  }
  return total;
}

const char* bookTitleOrUntitled(const DailyReadingWindow& window, const uint8_t book) {
  return window.titles[book][0] ? window.titles[book].data() : tr(STR_STATS_UNTITLED);
}

void drawReadingLogHeader(GfxRenderer& renderer, const MappedInputManager* mappedInput, const char* title) {
  renderer.clearScreen();
  if (mappedInput && mappedInput->hasTouchHardware()) {
    TouchHeaderBackButton::drawCompact(renderer, title, false);
  } else {
    CompactHeader::drawTitle(renderer, title);
  }
}

void drawReadingLogButtonHints(GfxRenderer& renderer, const MappedInputManager* mappedInput, const bool showButtonHints,
                               const bool showMoreButton) {
  if (!showButtonHints || !mappedInput) return;
  const auto labels =
      mappedInput->mapLabels(mappedInput->withBackArrow(tr(STR_EXIT)), "", mappedInput->withBackArrow(tr(STR_BACK)),
                             showMoreButton ? tr(STR_MORE) : "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4, true);
}

void drawLabelBarValueRow(GfxRenderer& renderer, const int x, const int w, const int y, const int rowH,
                          const char* label, const uint32_t seconds, const uint32_t maxSeconds, const int labelW) {
  constexpr int kGap = 10;
  constexpr int kBarH = 12;
  char value[24];
  formatLogDuration(seconds, value, sizeof(value));
  const int textY = y + (rowH - renderer.getLineHeight(UI_10_FONT_ID)) / 2;
  const std::string fitted = renderer.truncatedText(UI_10_FONT_ID, label, labelW);
  renderer.drawText(UI_10_FONT_ID, x, textY, fitted.c_str());
  const int valueW = renderer.getTextWidth(UI_10_FONT_ID, value, EpdFontFamily::BOLD);
  renderer.drawText(UI_10_FONT_ID, x + w - valueW, textY, value, true, EpdFontFamily::BOLD);
  const int barX = x + labelW + kGap;
  const int barW = std::max(0, w - labelW - valueW - kGap * 2);
  if (maxSeconds > 0 && seconds > 0) {
    const int fillW = std::max(2, static_cast<int>((static_cast<uint64_t>(barW) * seconds) / maxSeconds));
    renderer.fillRect(barX, y + (rowH - kBarH) / 2, fillW, kBarH, true);
  }
}

}  // namespace

int readingLogWeekCount(const ReadingStatsDate& today) {
  const uint32_t todayIndex = readingStatsDayIndex(today);
  const uint32_t oldest = readingLogFirstDay(todayIndex);
  const uint32_t thisWeekStart = todayIndex - readingStatsDayOfWeekIndex(today);
  const uint32_t oldestWeekStart = oldest - static_cast<uint32_t>(dayOfWeekForIndex(today, todayIndex, oldest));
  return static_cast<int>((thisWeekStart - oldestWeekStart) / 7u) + 1;
}

void renderReadingSummaryPage(GfxRenderer& renderer, const MappedInputManager* mappedInput,
                              const DailyReadingWindow* window, const ReadingStatsDate& today,
                              const bool showButtonHints) {
  constexpr int kMaxWeeks = 6;
  constexpr int kRowPadding = 8;
  constexpr int kTopBookLimit = 10;

  drawReadingLogHeader(renderer, mappedInput, tr(STR_STATS_LAST_30_DAYS));
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto& layout = kDefaultLayout;
  const int x = metrics.contentSidePadding;
  const int w = renderer.getScreenWidth() - metrics.contentSidePadding * 2;
  const int bottom = renderer.getScreenHeight() - statsBottomInset(metrics, showButtonHints);
  int y = metrics.topPadding + statsHeaderHeight(metrics, layout, mappedInput) + layout.topGap;

  const uint32_t todayIndex = readingStatsDayIndex(today);
  const uint32_t firstDay = readingLogFirstDay(todayIndex);
  const uint32_t total = secondsInRange(window, firstDay, todayIndex);
  uint32_t daysRead = 0;
  for (uint32_t day = firstDay; day <= todayIndex; ++day) {
    if (secondsInRange(window, day, day) > 0) daysRead++;
  }

  // Totals card: reading time, daily average, and days read.
  constexpr int kTotalsCardH = 92;
  renderer.drawRect(x, y, w, kTotalsCardH);
  char totalText[24];
  char averageText[24];
  char daysText[16];
  formatLogDuration(total, totalText, sizeof(totalText));
  formatLogDuration(total / READING_LOG_WINDOW_DAYS, averageText, sizeof(averageText));
  snprintf(daysText, sizeof(daysText), "%lu/%lu", static_cast<unsigned long>(daysRead),
           static_cast<unsigned long>(READING_LOG_WINDOW_DAYS));
  const int cellW = w / 3;
  drawStatCell(renderer, x, cellW, y, kTotalsCardH, totalText, tr(STR_STATS_TOTAL_READING_TIME_LBL_SHORT));
  drawStatCell(renderer, x + cellW, cellW, y, kTotalsCardH, averageText, tr(STR_STATS_DAILY_AVG_LBL));
  drawStatCell(renderer, x + cellW * 2, w - cellW * 2, y, kTotalsCardH, daysText, tr(STR_STATS_DAYS_READ_LBL));
  y += kTotalsCardH + layout.cardGap;

  // By Week card: one bar per calendar week.
  const int weekCount = std::min(readingLogWeekCount(today), kMaxWeeks);
  const int rowH = renderer.getLineHeight(UI_10_FONT_ID) + kRowPadding;
  std::array<uint32_t, kMaxWeeks> weekSeconds{};
  std::array<uint32_t, kMaxWeeks> weekStarts{};
  uint32_t maxWeek = 0;
  for (int week = 0; week < weekCount; ++week) {
    uint32_t weekFirst = 0;
    uint32_t weekLast = 0;
    weekDayRange(today, week, weekFirst, weekLast, weekStarts[week]);
    weekSeconds[week] = secondsInRange(window, weekFirst, weekLast);
    maxWeek = std::max(maxWeek, weekSeconds[week]);
  }
  const int weekCardH = layout.sectionTitleH + layout.chartTopPadding + layout.chartBottomPadding + weekCount * rowH;
  drawSectionCard(renderer, x, y, w, weekCardH, tr(STR_STATS_BY_WEEK), layout);
  constexpr int kCardInset = 12;
  const int innerX = x + kCardInset;
  const int innerW = w - kCardInset * 2;
  int rowY = y + layout.sectionTitleH + layout.chartTopPadding;
  char label[40];
  for (int week = 0; week < weekCount; ++week) {
    formatWeekLabel(week, weekStarts[week], label, sizeof(label));
    drawLabelBarValueRow(renderer, innerX, innerW, rowY, rowH, label, weekSeconds[week], maxWeek, innerW * 2 / 5);
    rowY += rowH;
  }
  y += weekCardH + layout.cardGap;

  // Top Books card: as many of the most-read books as fit.
  const int availableBookRows =
      (bottom - y - layout.sectionTitleH - layout.chartTopPadding - layout.chartBottomPadding) / rowH;
  const int bookLimit = std::min(kTopBookLimit, availableBookRows);
  if (bookLimit <= 0) {
    drawReadingLogButtonHints(renderer, mappedInput, showButtonHints, true);
    return;
  }
  std::array<uint32_t, DailyReadingWindow::MAX_BOOKS> bookSeconds{};
  std::array<uint8_t, DailyReadingWindow::MAX_BOOKS> bookOrder{};
  const uint8_t bookCount = window ? window->bookCount : 0;
  for (uint16_t i = 0; window && i < window->count; ++i) {
    const auto& entry = window->entries[i];
    if (entry.dayIndex >= firstDay && entry.dayIndex <= todayIndex) bookSeconds[entry.book] += entry.seconds;
  }
  uint8_t readBooks = 0;
  for (uint8_t book = 0; book < bookCount; ++book) {
    if (bookSeconds[book] > 0) bookOrder[readBooks++] = book;
  }
  std::sort(bookOrder.begin(), bookOrder.begin() + readBooks,
            [&bookSeconds](const uint8_t a, const uint8_t b) { return bookSeconds[a] > bookSeconds[b]; });
  const int shownBooks = std::max(1, std::min<int>(readBooks, bookLimit));
  const int booksCardH = layout.sectionTitleH + layout.chartTopPadding + layout.chartBottomPadding + shownBooks * rowH;
  drawSectionCard(renderer, x, y, w, booksCardH, tr(STR_STATS_TOP_BOOKS), layout);
  rowY = y + layout.sectionTitleH + layout.chartTopPadding;
  if (readBooks == 0) {
    drawCenteredLabel(renderer, UI_10_FONT_ID, x, w, rowY + (rowH - renderer.getLineHeight(UI_10_FONT_ID)) / 2, "-");
  }
  for (int i = 0; i < shownBooks && i < readBooks; ++i) {
    const uint8_t book = bookOrder[i];
    drawLabelBarValueRow(renderer, innerX, innerW, rowY, rowH, bookTitleOrUntitled(*window, book), bookSeconds[book],
                         bookSeconds[bookOrder[0]], innerW / 2);
    rowY += rowH;
  }
  drawReadingLogButtonHints(renderer, mappedInput, showButtonHints, true);
}

void renderReadingWeekPage(GfxRenderer& renderer, const MappedInputManager* mappedInput,
                           const DailyReadingWindow* window, const ReadingStatsDate& today, const int weekIndex,
                           const bool showButtonHints, const bool showMoreButton) {
  constexpr int kBookIndent = 16;
  constexpr int kRowPadding = 6;
  constexpr int kDayGap = 6;
  constexpr int kMaxDays = 7;

  uint32_t firstDay = 0;
  uint32_t lastDay = 0;
  uint32_t weekStart = 0;
  weekDayRange(today, weekIndex, firstDay, lastDay, weekStart);
  char title[40];
  formatWeekLabel(weekIndex, weekStart, title, sizeof(title));
  drawReadingLogHeader(renderer, mappedInput, title);

  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto& layout = kDefaultLayout;
  const int x = metrics.contentSidePadding;
  const int w = renderer.getScreenWidth() - metrics.contentSidePadding * 2;
  const int bottom = renderer.getScreenHeight() - statsBottomInset(metrics, showButtonHints);
  int y = metrics.topPadding + statsHeaderHeight(metrics, layout, mappedInput) + layout.topGap;

  // Order this week's entries newest day first, longest book first. Index
  // arrays keep this to a few hundred bytes of stack instead of copying entries.
  std::array<uint8_t, DailyReadingWindow::MAX_ENTRIES> order{};
  uint8_t orderCount = 0;
  for (uint16_t i = 0; window && i < window->count; ++i) {
    const uint32_t day = window->entries[i].dayIndex;
    if (day >= firstDay && day <= lastDay && window->entries[i].seconds > 0) order[orderCount++] = i;
  }
  std::sort(order.begin(), order.begin() + orderCount, [window](const uint8_t a, const uint8_t b) {
    const auto& lhs = window->entries[a];
    const auto& rhs = window->entries[b];
    if (lhs.dayIndex != rhs.dayIndex) return lhs.dayIndex > rhs.dayIndex;
    return lhs.seconds > rhs.seconds;
  });
  const int dayCount = lastDay >= firstDay ? static_cast<int>(lastDay - firstDay) + 1 : 0;
  std::array<uint8_t, kMaxDays> dayStart{};
  std::array<uint8_t, kMaxDays> dayBooks{};
  std::array<uint32_t, kMaxDays> daySeconds{};
  uint32_t weekSeconds = 0;
  for (uint8_t i = 0; i < orderCount; ++i) {
    const auto& entry = window->entries[order[i]];
    const int d = static_cast<int>(lastDay - entry.dayIndex);
    if (dayBooks[d] == 0) dayStart[d] = i;
    dayBooks[d]++;
    daySeconds[d] += entry.seconds;
    weekSeconds += entry.seconds;
  }

  char value[24];
  char range[40];
  char start[16];
  char end[16];
  formatDayIndexDate(weekStart, start, sizeof(start));
  formatDayIndexDate(weekStart + 6, end, sizeof(end));
  snprintf(range, sizeof(range), "%s - %s", start, end);
  const int totalRowH = renderer.getLineHeight(UI_12_FONT_ID) + kRowPadding * 2;
  formatLogDuration(weekSeconds, value, sizeof(value));
  drawLabelValueRow(renderer, UI_12_FONT_ID, x, w, y + kRowPadding, range, value, EpdFontFamily::BOLD);
  y += totalRowH;
  renderer.drawLine(x, y, x + w, y);
  y += kDayGap;

  // Show as many books per day as fit; the rest collapse into one "+N more" row.
  const int dayRowH = renderer.getLineHeight(UI_10_FONT_ID) + kRowPadding;
  const int bookRowH = renderer.getLineHeight(SMALL_FONT_ID) + kRowPadding;
  const int bookRowsAvailable = std::max(0, (bottom - y - dayCount * (dayRowH + kDayGap)) / bookRowH);
  int maxBooksPerDay = dayCount > 0 ? *std::max_element(dayBooks.begin(), dayBooks.begin() + dayCount) : 0;
  for (; maxBooksPerDay > 0; --maxBooksPerDay) {
    int rows = 0;
    for (int d = 0; d < dayCount; ++d) {
      rows += std::min<int>(dayBooks[d], maxBooksPerDay) + (dayBooks[d] > maxBooksPerDay);
    }
    if (rows <= bookRowsAvailable) break;
  }

  const uint32_t todayIndex = readingStatsDayIndex(today);
  char dayLabel[32];
  for (int d = 0; d < dayCount; ++d) {
    const uint32_t dayIndex = lastDay - static_cast<uint32_t>(d);
    if (dayIndex == todayIndex) {
      snprintf(dayLabel, sizeof(dayLabel), "%s", tr(STR_STATS_TODAY));
    } else if (dayIndex + 1 == todayIndex) {
      snprintf(dayLabel, sizeof(dayLabel), "%s", tr(STR_STATS_YESTERDAY));
    } else {
      char shortDate[16];
      formatDayIndexDate(dayIndex, shortDate, sizeof(shortDate));
      snprintf(dayLabel, sizeof(dayLabel), "%s %s",
               I18N.get(DAY_LABELS[dayOfWeekForIndex(today, todayIndex, dayIndex)]), shortDate);
    }
    formatLogDuration(daySeconds[d], value, sizeof(value));
    drawLabelValueRow(renderer, UI_10_FONT_ID, x, w, y, dayLabel, value, EpdFontFamily::BOLD);
    y += dayRowH;

    const int shownBooks = std::min<int>(dayBooks[d], maxBooksPerDay);
    uint32_t hiddenSeconds = daySeconds[d];
    for (int b = 0; b < shownBooks; ++b) {
      const auto& entry = window->entries[order[dayStart[d] + b]];
      hiddenSeconds -= entry.seconds;
      formatLogDuration(entry.seconds, value, sizeof(value));
      drawLabelValueRow(renderer, SMALL_FONT_ID, x + kBookIndent, w - kBookIndent, y,
                        bookTitleOrUntitled(*window, entry.book), value, EpdFontFamily::REGULAR);
      y += bookRowH;
    }
    if (dayBooks[d] > shownBooks) {
      char moreLabel[32];
      snprintf(moreLabel, sizeof(moreLabel), "+%d %s", dayBooks[d] - shownBooks, tr(STR_STATS_MORE_BOOKS));
      formatLogDuration(hiddenSeconds, value, sizeof(value));
      drawLabelValueRow(renderer, SMALL_FONT_ID, x + kBookIndent, w - kBookIndent, y, moreLabel, value,
                        EpdFontFamily::REGULAR);
      y += bookRowH;
    }
    y += kDayGap;
  }

  drawReadingLogButtonHints(renderer, mappedInput, showButtonHints, showMoreButton);
}
