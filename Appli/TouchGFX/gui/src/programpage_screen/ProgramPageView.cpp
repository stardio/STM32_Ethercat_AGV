#include <gui/programpage_screen/ProgramPageView.hpp>
#include <gui/common/NumberFormat.hpp>
#include <touchgfx/Unicode.hpp>
#include <touchgfx/events/ClickEvent.hpp>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

ProgramPageView::ProgramPageView()
        : activeInputField(kNoActiveField),
            suppressKeyboardEcho(false),
            keyboardEnterCallback(this, &ProgramPageView::onKeyboardEnter),
            keyboardChangedCallback(this, &ProgramPageView::onKeyboardBufferChanged),
            pageButtonCallback(this, &ProgramPageView::onPageButtonPressed)
{
        for (uint8_t i = 0U; i < kAllInputFieldCount; i++)
        {
                fieldInputValues[i][0] = '0';
                fieldInputValues[i][1] = '\0';
        }
}

void ProgramPageView::setupScreen()
{
    ProgramPageViewBase::setupScreen();

    // Current-value readback: [0]=Position, [1]=Speed, [2]=Torque
    gui::configureNumericOverlay(numericTexts[0], CurrentPosition,       numericBuffers[0]);
    gui::configureNumericOverlay(numericTexts[1], CurrentPosition_3_1_1, numericBuffers[1]);
    gui::configureNumericOverlay(numericTexts[2], CurrentTorque,         numericBuffers[2]);

    add(numericTexts[0]);
    add(numericTexts[1]);
    add(numericTexts[2]);

    for (uint8_t index = 0U; index < 3U; index++)
    {
        gui::formatUnsignedWithCommas(0U, numericBuffers[index], gui::kNumericBufferSize);
    }

    // Target-value input overlays: [0-2]=Position1-3, [3-5]=Speed1-3, [6-8]=Torque1-3, [9]=ReturnSpeed
    gui::configureNumericOverlay(targetTexts[0], TargetPosition1, targetBuffers[0]);
    gui::configureNumericOverlay(targetTexts[1], TargetPosition2, targetBuffers[1]);
    gui::configureNumericOverlay(targetTexts[2], TargetPosition3, targetBuffers[2]);
    gui::configureNumericOverlay(targetTexts[3], TargetSpeed1,    targetBuffers[3]);
    gui::configureNumericOverlay(targetTexts[4], TargetSpeed2,    targetBuffers[4]);
    gui::configureNumericOverlay(targetTexts[5], TargetSpeed3,    targetBuffers[5]);
    gui::configureNumericOverlay(targetTexts[6], TargetTorque1,   targetBuffers[6]);
    gui::configureNumericOverlay(targetTexts[7], TargetTorque2,   targetBuffers[7]);
    gui::configureNumericOverlay(targetTexts[8], TargetTorque3,   targetBuffers[8]);
    gui::configureNumericOverlay(targetTexts[9], ReturnSpeed,     targetBuffers[9]);

    // Delay time overlay (msec)
    gui::configureNumericOverlay(delayText, DelyTime, delayBuffer);

    for (uint8_t i = 0U; i < kTargetFieldCount; i++)
    {
        gui::formatUnsignedWithCommas(0U, targetBuffers[i], gui::kNumericBufferSize);
        add(targetTexts[i]);
    }
    gui::formatUnsignedWithCommas(0U, delayBuffer, gui::kNumericBufferSize);
    add(delayText);

    keyBoard1.setEnterCallback(keyboardEnterCallback);
    keyBoard1.setBufferChangedCallback(keyboardChangedCallback);
    Main_button_1.setAction(pageButtonCallback);
    Main_button_1_1.setAction(pageButtonCallback);
    Main_button_1_2.setAction(pageButtonCallback);
    Main_button_1_2_1.setAction(pageButtonCallback);
    remove(keyBoard1);
    add(keyBoard1);
    keyBoard1.setVisible(false);
    keyBoard1.invalidate();

    for (uint8_t i = 0U; i < kAllInputFieldCount; i++)
    {
        const int32_t savedValue = presenter->notifyGetProgramValue(i);
        (void)snprintf(fieldInputValues[i], KeyBoard::MAX_BUF, "%ld", static_cast<long>(savedValue));
        updateFieldText(static_cast<int8_t>(i), fieldInputValues[i]);
    }
}

void ProgramPageView::tearDownScreen()
{
    hideKeyboard();
    ProgramPageViewBase::tearDownScreen();
}

void ProgramPageView::handleTickEvent()
{
    ProgramPageViewBase::handleTickEvent();
}

void ProgramPageView::handleClickEvent(const touchgfx::ClickEvent& evt)
{
    ProgramPageViewBase::handleClickEvent(evt);

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

    const bool inKeyboard = keyBoard1.isVisible() && inRect(keyBoard1);
    if (inKeyboard)
    {
        return;
    }

    // Ignore other buttons to avoid interfering with field input
    if (inRect(Main_button) ||
        inRect(Main_button_1))
    {
        return;
    }

    for (int8_t i = 0; i < kTargetFieldCount; i++)
    {
        if (touchX >= targetTexts[i].getX() && touchX < (targetTexts[i].getX() + targetTexts[i].getWidth()) &&
            touchY >= targetTexts[i].getY() && touchY < (targetTexts[i].getY() + targetTexts[i].getHeight()))
        {
            showKeyboardForField(i);
            return;
        }
    }

    if (touchX >= delayText.getX() && touchX < (delayText.getX() + delayText.getWidth()) &&
        touchY >= delayText.getY() && touchY < (delayText.getY() + delayText.getHeight()))
    {
        showKeyboardForField(static_cast<int8_t>(kTargetFieldCount));
        return;
    }

    if (activeInputField != kNoActiveField)
    {
        applyFieldText(activeInputField, fieldInputValues[activeInputField]);
    }
    hideKeyboard();
}

void ProgramPageView::updateMotionData(int32_t position, int32_t speed, int16_t torque)
{
    gui::formatSignedWithCommas(position,  numericBuffers[0], gui::kNumericBufferSize);
    gui::formatAbsoluteWithCommas(speed,   numericBuffers[1], gui::kNumericBufferSize);
    gui::formatTorquePercent(torque,        numericBuffers[2], gui::kNumericBufferSize);
    numericTexts[0].invalidate();
    numericTexts[1].invalidate();
    numericTexts[2].invalidate();
}

void ProgramPageView::showKeyboardForField(int8_t fieldIndex)
{
    if (fieldIndex < 0 || fieldIndex >= kAllInputFieldCount)
    {
        return;
    }

    if (activeInputField != kNoActiveField && activeInputField != fieldIndex)
    {
        applyFieldText(activeInputField, fieldInputValues[activeInputField]);
    }

    activeInputField = fieldIndex;
    suppressKeyboardEcho = true;
    keyBoard1.clearBuffer();
    suppressKeyboardEcho = false;
    remove(keyBoard1);
    add(keyBoard1);
    keyBoard1.setVisible(true);
    keyBoard1.invalidate();
}

void ProgramPageView::hideKeyboard()
{
    keyBoard1.setVisible(false);
    keyBoard1.invalidate();
    activeInputField = kNoActiveField;
}

void ProgramPageView::onPageButtonPressed(const touchgfx::AbstractButton& src)
{
    if (&src == &Main_button_1)
    {
        if (activeInputField != kNoActiveField)
        {
            applyFieldText(activeInputField, fieldInputValues[activeInputField]);
            hideKeyboard();
        }
        (void)presenter->notifyStartProgramSequence();
        return;
    }

    if (&src == &Main_button_1_1)
    {
        presenter->notifyStopProgramSequence();
        return;
    }

    if (&src == &Main_button_1_2)
    {
        if (activeInputField != kNoActiveField)
        {
            applyFieldText(activeInputField, fieldInputValues[activeInputField]);
            hideKeyboard();
        }
        (void)presenter->notifySaveProgramPageToUiFlash();
        return;
    }

    if (&src == &Main_button_1_2_1)
    {
        if (activeInputField != kNoActiveField)
        {
            hideKeyboard();
        }

        if (presenter->notifyLoadProgramPageFromUiFlash())
        {
            for (uint8_t i = 0U; i < kAllInputFieldCount; i++)
            {
                const int32_t loadedValue = presenter->notifyGetProgramValue(i);
                (void)snprintf(fieldInputValues[i], KeyBoard::MAX_BUF, "%ld", static_cast<long>(loadedValue));
                updateFieldText(static_cast<int8_t>(i), fieldInputValues[i]);
            }
        }
    }
}

void ProgramPageView::applyFieldText(int8_t fieldIndex, const char* text)
{
    if (fieldIndex < 0 || fieldIndex >= kAllInputFieldCount)
    {
        return;
    }

    const char* safeText = (text != 0 && text[0] != '\0') ? text : "0";
    const int32_t parsed = parseSigned32(safeText);

    updateFieldText(fieldIndex, safeText);
    presenter->notifySetProgramValue(static_cast<uint8_t>(fieldIndex), parsed);
}

void ProgramPageView::updateFieldText(int8_t fieldIndex, const char* text)
{
    if (fieldIndex < 0 || fieldIndex >= kAllInputFieldCount)
    {
        return;
    }

    const char* safeText = (text != 0 && text[0] != '\0') ? text : "0";
    const int32_t parsed = parseSigned32(safeText);

    if (fieldIndex < kTargetFieldCount)
    {
        gui::formatSignedWithCommas(parsed, targetBuffers[fieldIndex], gui::kNumericBufferSize);
        targetTexts[fieldIndex].invalidate();
    }
    else
    {
        gui::formatSignedWithCommas(parsed, delayBuffer, gui::kNumericBufferSize);
        delayText.invalidate();
    }
}

void ProgramPageView::onKeyboardBufferChanged(const char* text)
{
    if (suppressKeyboardEcho || activeInputField == kNoActiveField)
    {
        return;
    }

    const char* safeText = (text != 0 && text[0] != '\0') ? text : "0";
    memset(fieldInputValues[activeInputField], 0, sizeof(fieldInputValues[activeInputField]));
    strncpy(fieldInputValues[activeInputField], safeText, static_cast<size_t>(KeyBoard::MAX_BUF - 1));
    updateFieldText(activeInputField, fieldInputValues[activeInputField]);
}

void ProgramPageView::onKeyboardEnter(const char* text)
{
    if (activeInputField == kNoActiveField)
    {
        return;
    }

    const char* safeText = (text != 0 && text[0] != '\0') ? text : "0";
    memset(fieldInputValues[activeInputField], 0, sizeof(fieldInputValues[activeInputField]));
    strncpy(fieldInputValues[activeInputField], safeText, static_cast<size_t>(KeyBoard::MAX_BUF - 1));

    applyFieldText(activeInputField, fieldInputValues[activeInputField]);
    hideKeyboard();
}

int32_t ProgramPageView::parseSigned32(const char* text) const
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

