#include <gui/mainpage_screen/MainPageView.hpp>
#include <gui/mainpage_screen/MainPagePresenter.hpp>
#include <gui/common/NumberFormat.hpp>
#include <stdint.h>

MainPageView::MainPageView()
        : runUiState(0U),
      runAppliedState(0U),
            runDebounceCount(0U)
{

}

void MainPageView::setupScreen()
{
    MainPageViewBase::setupScreen();

    gui::configureNumericOverlay(numericTexts[0], CurrentPosition, numericBuffers[0]);
    gui::configureNumericOverlay(numericTexts[1], CurrentPosition_1, numericBuffers[1]);
    gui::configureNumericOverlay(numericTexts[2], CurrentPosition_1_1, numericBuffers[2]);
    add(numericTexts[0]);
    add(numericTexts[1]);
    add(numericTexts[2]);
    gui::formatSignedWithCommas(0, numericBuffers[0], gui::kNumericBufferSize);
    gui::formatUnsignedWithCommas(0, numericBuffers[1], gui::kNumericBufferSize);
    gui::formatUnsignedWithCommas(0, numericBuffers[2], gui::kNumericBufferSize);

    const uint8_t savedRun = presenter->notifyGetRunEnable();
    runUiState = savedRun;
    runAppliedState = savedRun;
    runDebounceCount = 0U;
    ServoON.forceState(savedRun != 0U);
}

void MainPageView::tearDownScreen()
{
    MainPageViewBase::tearDownScreen();
}

void MainPageView::handleTickEvent()
{
    const uint8_t currentRunUi = ServoON.getState() ? 1U : 0U;

    if (currentRunUi != runUiState)
    {
        runUiState = currentRunUi;
        runDebounceCount = 0U;
    }
    else if (runDebounceCount < 20U)
    {
        runDebounceCount++;
    }

    if ((runDebounceCount >= 5U) && (runAppliedState != runUiState))
    {
        runAppliedState = runUiState;
        presenter->notifySetRunEnable(runAppliedState);
    }

    MainPageViewBase::handleTickEvent();
}

void MainPageView::handleClickEvent(const touchgfx::ClickEvent& evt)
{
    MainPageViewBase::handleClickEvent(evt);
}

void MainPageView::function1()
{
    /* RUN is applied via debounced polling in handleTickEvent(). */
}

void MainPageView::updateMotionData(int32_t position, int32_t speed, int16_t torque)
{
    gui::formatSignedWithCommas(position, numericBuffers[0], gui::kNumericBufferSize);
    gui::formatAbsoluteWithCommas(speed, numericBuffers[1], gui::kNumericBufferSize);
    gui::formatTorquePercent(torque, numericBuffers[2], gui::kNumericBufferSize);
    numericTexts[0].invalidate();
    numericTexts[1].invalidate();
    numericTexts[2].invalidate();
}

void MainPageView::updateRunEnable(uint8_t enabled)
{
    const uint8_t runFeedback = (enabled != 0U) ? 1U : 0U;

    /* Only resync the toggle when there is no local UI transition in progress.
     * This avoids fighting the ToggleButton's own click/release state machine. */
    if ((runDebounceCount >= 5U) && (runAppliedState == runUiState) && (runAppliedState != runFeedback))
    {
        runUiState = runFeedback;
        runAppliedState = runFeedback;
        runDebounceCount = 0U;
        ServoON.forceState(runFeedback != 0U);
    }
}
