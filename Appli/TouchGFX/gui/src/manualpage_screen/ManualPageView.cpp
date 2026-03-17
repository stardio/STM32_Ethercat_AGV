#include <gui/manualpage_screen/ManualPageView.hpp>
#include <gui/manualpage_screen/ManualPagePresenter.hpp>
#include <gui/common/NumberFormat.hpp>
#include <touchgfx/Unicode.hpp>
#include <touchgfx/events/ClickEvent.hpp>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

ManualPageView::ManualPageView()
    : jogStepCounts(1000),
      cycleAbsMode(1U),
      activeCycleField(kNoActiveField),
      suppressKeyboardEcho(false),
      lastActualPosition(0),
      keyboardEnterCallback(this, &ManualPageView::onKeyboardEnter),
      keyboardChangedCallback(this, &ManualPageView::onKeyboardBufferChanged)
{
    for (uint8_t i = 0U; i < kOneCycleFieldCount; i++)
    {
        cycleValues[i] = 0;
        cycleInputValues[i][0] = '0';
        cycleInputValues[i][1] = '\0';
    }
}

void ManualPageView::setupScreen()
{
    ManualPageViewBase::setupScreen();
    jogStepCounts = presenter->notifyGetJogStepCounts();

    cycleValues[kOneCycleFieldPosition] = presenter->notifyGetManualCyclePosition();
    cycleValues[kOneCycleFieldSpeed] = presenter->notifyGetManualCycleSpeed();
    cycleValues[kOneCycleFieldTorque] = presenter->notifyGetManualCycleTorque();
    cycleAbsMode = presenter->notifyGetManualCycleAbsMode();
    if (cycleAbsMode > 1U)
    {
        cycleAbsMode = 1U;
    }

    gui::configureNumericOverlay(numericTexts[0], CurrentPosition, numericBuffers[0]);
    gui::configureNumericOverlay(numericTexts[1], CurrentPosition_1, numericBuffers[1]);
    gui::configureNumericOverlay(numericTexts[2], CurrentPosition_2, numericBuffers[2]);

    gui::configureNumericOverlay(cycleTexts[kOneCycleFieldPosition], CurrentPosition_3, cycleBuffers[kOneCycleFieldPosition]);
    gui::configureNumericOverlay(cycleTexts[kOneCycleFieldSpeed], CurrentPosition_1_1, cycleBuffers[kOneCycleFieldSpeed]);
    gui::configureNumericOverlay(cycleTexts[kOneCycleFieldTorque], CurrentPosition_2_1, cycleBuffers[kOneCycleFieldTorque]);

    add(numericTexts[0]);
    add(numericTexts[1]);
    add(numericTexts[2]);

    add(cycleTexts[kOneCycleFieldPosition]);
    add(cycleTexts[kOneCycleFieldSpeed]);
    add(cycleTexts[kOneCycleFieldTorque]);

    gui::formatSignedWithCommas(0, numericBuffers[0], gui::kNumericBufferSize);
    gui::formatUnsignedWithCommas(0, numericBuffers[1], gui::kNumericBufferSize);
    gui::formatUnsignedWithCommas(0, numericBuffers[2], gui::kNumericBufferSize);

    for (uint8_t i = 0U; i < kOneCycleFieldCount; i++)
    {
        (void)snprintf(cycleInputValues[i], KeyBoard::MAX_BUF, "%ld", static_cast<long>(cycleValues[i]));
        updateCycleFieldText(static_cast<int8_t>(i), cycleInputValues[i]);
    }

    keyBoard1.setEnterCallback(keyboardEnterCallback);
    keyBoard1.setBufferChangedCallback(keyboardChangedCallback);
    remove(keyBoard1);
    add(keyBoard1);
    keyBoard1.setVisible(false);
    keyBoard1.invalidate();

    applyAbsIncVisual();
}

void ManualPageView::tearDownScreen()
{
    hideKeyboard();
    ManualPageViewBase::tearDownScreen();
}

void ManualPageView::handleTickEvent()
{
    if (JogREV.getPressedState())
    {
        presenter->notifySendPositionDelta(-jogStepCounts);
    }
    else if (JogFWD.getPressedState())
    {
        presenter->notifySendPositionDelta(jogStepCounts);
    }

    ManualPageViewBase::handleTickEvent();
}

void ManualPageView::handleClickEvent(const touchgfx::ClickEvent& evt)
{
    ManualPageViewBase::handleClickEvent(evt);

    if (evt.getType() != touchgfx::ClickEvent::PRESSED)
    {
        return;
    }

    const int16_t touchX = static_cast<int16_t>(evt.getX());
    const int16_t touchY = static_cast<int16_t>(evt.getY());
    auto inRect = [touchX, touchY](const touchgfx::Drawable& widget) -> bool
    {
        return touchX >= widget.getX() &&
               touchX < (widget.getX() + widget.getWidth()) &&
               touchY >= widget.getY() &&
               touchY < (widget.getY() + widget.getHeight());
    };

    if (keyBoard1.isVisible() && inRect(keyBoard1))
    {
        return;
    }

    if (inRect(Main_button_1))
    {
        if (activeCycleField != kNoActiveField)
        {
            applyCycleFieldText(activeCycleField, cycleInputValues[activeCycleField]);
            hideKeyboard();
        }
        (void)presenter->notifySaveManualPageToUiFlash();
        return;
    }

    if (inRect(Main_button_1_1))
    {
        if (activeCycleField != kNoActiveField)
        {
            hideKeyboard();
        }

        if (presenter->notifyLoadManualPageFromUiFlash())
        {
            cycleValues[kOneCycleFieldPosition] = presenter->notifyGetManualCyclePosition();
            cycleValues[kOneCycleFieldSpeed] = presenter->notifyGetManualCycleSpeed();
            cycleValues[kOneCycleFieldTorque] = presenter->notifyGetManualCycleTorque();
            cycleAbsMode = presenter->notifyGetManualCycleAbsMode();
            if (cycleAbsMode > 1U)
            {
                cycleAbsMode = 1U;
            }

            for (uint8_t i = 0U; i < kOneCycleFieldCount; i++)
            {
                (void)snprintf(cycleInputValues[i], KeyBoard::MAX_BUF, "%ld", static_cast<long>(cycleValues[i]));
                updateCycleFieldText(static_cast<int8_t>(i), cycleInputValues[i]);
            }
            applyAbsIncVisual();
        }
        return;
    }

    if (inRect(Main_button) || inRect(JogFWD) || inRect(JogREV))
    {
        return;
    }

    if (inRect(ManualStart))
    {
        function1();
        return;
    }

    if (inRect(ManualStop))
    {
        function2();
        return;
    }

    if (inRect(AbsInc))
    {
        function3();
        return;
    }

    for (int8_t i = 0; i < kOneCycleFieldCount; i++)
    {
        if (inRect(cycleTexts[i]))
        {
            showKeyboardForCycleField(i);
            return;
        }
    }

    if (activeCycleField != kNoActiveField)
    {
        applyCycleFieldText(activeCycleField, cycleInputValues[activeCycleField]);
    }
    hideKeyboard();
}

void ManualPageView::function1()
{
    if (activeCycleField != kNoActiveField)
    {
        const char* liveInput = keyBoard1.getBuffer();
        if (liveInput != 0 && liveInput[0] != '\0')
        {
            memset(cycleInputValues[activeCycleField], 0, sizeof(cycleInputValues[activeCycleField]));
            strncpy(cycleInputValues[activeCycleField], liveInput, static_cast<size_t>(KeyBoard::MAX_BUF - 1));
            applyCycleFieldText(activeCycleField, cycleInputValues[activeCycleField]);
        }
        hideKeyboard();
    }

    /* Ensure one-cycle profile settings are applied right before start. */
    presenter->notifySetManualCycleSpeed(cycleValues[kOneCycleFieldSpeed]);
    presenter->notifySetManualCycleTorque(static_cast<int16_t>(cycleValues[kOneCycleFieldTorque]));

    if (cycleAbsMode != 0U)
    {
        presenter->notifySetTargetPositionAbs(cycleValues[kOneCycleFieldPosition]);
    }
    else
    {
        const int32_t actualNow = presenter->notifyGetPositionActual();
        int64_t targetInc = static_cast<int64_t>(actualNow) +
                            static_cast<int64_t>(cycleValues[kOneCycleFieldPosition]);
        if (targetInc > INT32_MAX)
        {
            targetInc = INT32_MAX;
        }
        else if (targetInc < INT32_MIN)
        {
            targetInc = INT32_MIN;
        }
        presenter->notifySetTargetPositionAbs(static_cast<int32_t>(targetInc));
    }
}

void ManualPageView::function2()
{
    presenter->notifySetTargetPositionAbs(presenter->notifyGetPositionActual());
    hideKeyboard();
}

void ManualPageView::function3()
{
    cycleAbsMode = (cycleAbsMode == 0U) ? 1U : 0U;
    presenter->notifySetManualCycleAbsMode(cycleAbsMode);
    applyAbsIncVisual();
}

void ManualPageView::function4()
{
    /* JogFWD: handled by hold-to-jog in handleTickEvent */
}

void ManualPageView::function5()
{
    /* JogREV: handled by hold-to-jog in handleTickEvent */
}

void ManualPageView::updateMotionData(int32_t position, int32_t speed, int16_t torque)
{
    lastActualPosition = position;
    gui::formatSignedWithCommas(position, numericBuffers[0], gui::kNumericBufferSize);
    gui::formatAbsoluteWithCommas(speed, numericBuffers[1], gui::kNumericBufferSize);
    gui::formatTorquePercent(torque, numericBuffers[2], gui::kNumericBufferSize);
    numericTexts[0].invalidate();
    numericTexts[1].invalidate();
    numericTexts[2].invalidate();
}

void ManualPageView::showKeyboardForCycleField(int8_t fieldIndex)
{
    if (fieldIndex < 0 || fieldIndex >= kOneCycleFieldCount)
    {
        return;
    }

    if (activeCycleField != kNoActiveField && activeCycleField != fieldIndex)
    {
        applyCycleFieldText(activeCycleField, cycleInputValues[activeCycleField]);
    }

    activeCycleField = fieldIndex;
    suppressKeyboardEcho = true;
    keyBoard1.clearBuffer();
    suppressKeyboardEcho = false;
    remove(keyBoard1);
    add(keyBoard1);
    keyBoard1.setVisible(true);
    keyBoard1.invalidate();
}

void ManualPageView::hideKeyboard()
{
    keyBoard1.setVisible(false);
    keyBoard1.invalidate();
    activeCycleField = kNoActiveField;
}

void ManualPageView::applyCycleFieldText(int8_t fieldIndex, const char* text)
{
    if (fieldIndex < 0 || fieldIndex >= kOneCycleFieldCount)
    {
        return;
    }

    const char* safeText = (text != 0 && text[0] != '\0') ? text : "0";
    const int32_t parsed = parseSigned32(safeText);

    updateCycleFieldText(fieldIndex, safeText);

    if (fieldIndex == kOneCycleFieldPosition)
    {
        presenter->notifySetManualCyclePosition(parsed);
    }
    else if (fieldIndex == kOneCycleFieldSpeed)
    {
        presenter->notifySetManualCycleSpeed(parsed);
    }
    else
    {
        presenter->notifySetManualCycleTorque(static_cast<int16_t>(parsed));
    }
}

void ManualPageView::updateCycleFieldText(int8_t fieldIndex, const char* text)
{
    if (fieldIndex < 0 || fieldIndex >= kOneCycleFieldCount)
    {
        return;
    }

    const char* safeText = (text != 0 && text[0] != '\0') ? text : "0";
    const int32_t parsed = parseSigned32(safeText);
    gui::formatSignedWithCommas(parsed, cycleBuffers[fieldIndex], gui::kNumericBufferSize);
    cycleTexts[fieldIndex].invalidate();

    cycleValues[fieldIndex] = parsed;
}

void ManualPageView::applyAbsIncVisual()
{
    if (cycleAbsMode != 0U)
    {
        AbsInc.setBoxWithBorderColors(
            touchgfx::Color::getColorFromRGB(0, 102, 153),
            touchgfx::Color::getColorFromRGB(0, 153, 204),
            touchgfx::Color::getColorFromRGB(0, 51, 102),
            touchgfx::Color::getColorFromRGB(51, 102, 153));
    }
    else
    {
        AbsInc.setBoxWithBorderColors(
            touchgfx::Color::getColorFromRGB(153, 102, 0),
            touchgfx::Color::getColorFromRGB(204, 153, 0),
            touchgfx::Color::getColorFromRGB(102, 51, 0),
            touchgfx::Color::getColorFromRGB(153, 102, 51));
    }
    AbsInc.invalidate();
}

void ManualPageView::onKeyboardEnter(const char* text)
{
    if (activeCycleField == kNoActiveField)
    {
        return;
    }

    const char* safeText = (text != 0 && text[0] != '\0') ? text : "0";
    memset(cycleInputValues[activeCycleField], 0, sizeof(cycleInputValues[activeCycleField]));
    strncpy(cycleInputValues[activeCycleField], safeText, static_cast<size_t>(KeyBoard::MAX_BUF - 1));

    applyCycleFieldText(activeCycleField, cycleInputValues[activeCycleField]);
    hideKeyboard();
}

void ManualPageView::onKeyboardBufferChanged(const char* text)
{
    if (suppressKeyboardEcho || activeCycleField == kNoActiveField)
    {
        return;
    }

    const char* safeText = (text != 0 && text[0] != '\0') ? text : "0";
    memset(cycleInputValues[activeCycleField], 0, sizeof(cycleInputValues[activeCycleField]));
    strncpy(cycleInputValues[activeCycleField], safeText, static_cast<size_t>(KeyBoard::MAX_BUF - 1));
    updateCycleFieldText(activeCycleField, cycleInputValues[activeCycleField]);
}

int32_t ManualPageView::parseSigned32(const char* text) const
{
    if (text == 0 || text[0] == '\0')
    {
        return 0;
    }

    char* endPtr = 0;
    long parsed = strtol(text, &endPtr, 10);
    if (endPtr == text)
    {
        return 0;
    }
    if (parsed > INT32_MAX)
    {
        parsed = INT32_MAX;
    }
    else if (parsed < INT32_MIN)
    {
        parsed = INT32_MIN;
    }
    return static_cast<int32_t>(parsed);
}

