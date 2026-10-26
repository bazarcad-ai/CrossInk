#pragma once

#include <HalClock.h>

#include <memory>
#include <string>

#include "../Activity.h"
#include "BookReadingStats.h"
#include "DailyReadingLog.h"
#include "GlobalReadingStats.h"

class BookStatsActivity final : public Activity {
  enum class Page : uint8_t { PerBook, ThisDevice, AllDevices, ReadingSummary, ReadingWeek, EditDates };

  std::string bookTitle;
  std::string bookCachePath;
  BookReadingStats stats;
  GlobalReadingStats globalStats;
  GlobalReadingStats allDevicesStats;
  bool showAllDevicesStats = false;
  bool returnToHomeOnExit = false;
  float progressPercent = -1.0f;
  bool hasEstimatedTimeLeft = false;
  uint32_t estimatedTimeLeftSeconds = 0;
  GfxRenderer::Orientation previousOrientation = GfxRenderer::Orientation::Portrait;
  Page page = Page::PerBook;
  int selectedEditField = 0;
  bool didChangeStats = false;
  BookReadingStats dateEditStatsSnapshot;
  GlobalReadingStats dateEditGlobalStatsSnapshot;
  bool dateEditSnapshotValid = false;
  bool didChangeStatsBeforeDateEdit = false;
  bool ignoreInitialBackRelease = false;
  bool ignoreInitialConfirmRelease = false;
  bool ignoreInitialPowerRelease = false;
  // Loaded when the reading log pages are first shown; ~4.2 KB, so heap-owned.
  std::unique_ptr<DailyReadingWindow> readingWindow;
  ReadingStatsDate readingToday;
  bool hasReadingToday = false;
  int readingWeekCount = 0;
  int readingWeek = 0;
  std::string pendingSessionTitle;
  ReadingStatsDateTime pendingSessionStart;
  uint32_t pendingSessionSeconds = 0;

  bool hasEditableBook() const { return !bookCachePath.empty() && halClock.isAvailable(); }
  bool usesNoRtcSingleScreenLayout() const { return !halClock.isAvailable(); }
  void refreshAllDevicesStats();
  void saveStats();
  void beginDateEditing();
  void finishDateEditing(bool saveChanges);
  void cycleEditField();
  void adjustSelectedDateField(int delta);
  void applyCompletedState(bool completed);
  ReadingStatsDate defaultDateForField(bool finishedField) const;
  void clearEditedDate(bool finishedField);
  bool shouldClearDateOnAdjust(const ReadingStatsDate& date, bool finishedField, int fieldIndex, int delta) const;
  void normalizeEditedDates(const bool editedFinishedField);
  void exitStatsActivity();
  bool showPreviousStatsPage();
  bool showNextStatsPage();
  bool selectEditFieldFromTouchTarget(int touchTarget);
  void ensureReadingWindowLoaded();
  int pageIndicatorIndex() const;
  int pageIndicatorCount() const;

 public:
  BookStatsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const std::string& title,
                    const std::string& bookCachePath, const BookReadingStats& stats, float progressPercent,
                    bool hasEstimatedTimeLeft, uint32_t estimatedTimeLeftSeconds, const GlobalReadingStats& globalStats,
                    bool returnToHomeOnExit = false);
  BookStatsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const std::string& title,
                    const std::string& bookCachePath, const BookReadingStats& stats, float progressPercent,
                    bool hasEstimatedTimeLeft, uint32_t estimatedTimeLeftSeconds, const GlobalReadingStats& globalStats,
                    const GlobalReadingStats& allDevicesStats, bool returnToHomeOnExit = false);

  // Adds the reader's not-yet-committed session to the reading log pages only.
  void setPendingSession(const std::string& title, const ReadingStatsDateTime& localStart, uint32_t seconds);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool allowPowerAsConfirmInReaderMode() const override { return true; }
};
