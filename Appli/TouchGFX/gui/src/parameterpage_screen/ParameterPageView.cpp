#include <gui/parameterpage_screen/ParameterPageView.hpp>
#include <touchgfx/events/ClickEvent.hpp>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

ParameterPageView::ParameterPageView()
    : activeField(kNoActiveField),
      suppressKeyboardEcho(false),
            parameterReadAllWaiting(false),
            parameterReadAllWaitTicks(0U),
      keyboardEnterCallback(this, &ParameterPageView::onKeyboardEnter),
      keyboardChangedCallback(this, &ParameterPageView::onKeyboardBufferChanged)
{
    for (uint8_t i = 0U; i < kFieldCount; i++)
    {
        paramValues[i] = 0;
        paramInputs[i][0] = '0';
        paramInputs[i][1] = '\0';
    }
}

void ParameterPageView::setupScreen()
{
    ParameterPageViewBase::setupScreen();

    gui::configureNumericOverlay(paramTexts[kFieldJogSpeed], Jog_Speed, paramBuffers[kFieldJogSpeed]);
    gui::configureNumericOverlay(paramTexts[kFieldAccTime], Acc_Time, paramBuffers[kFieldAccTime]);
    gui::configureNumericOverlay(paramTexts[kFieldDecTime], Dec_Time, paramBuffers[kFieldDecTime]);
    gui::configureNumericOverlay(paramTexts[kFieldLimitPlus], Limit_Plus, paramBuffers[kFieldLimitPlus]);
    gui::configureNumericOverlay(paramTexts[kFieldLimitMinus], Limit_Minus, paramBuffers[kFieldLimitMinus]);
    gui::configureNumericOverlay(paramTexts[kFieldUnitScale], Unit_Scale, paramBuffers[kFieldUnitScale]);
    gui::configureNumericOverlay(paramTexts[kFieldHomeOffset], Home_Offset, paramBuffers[kFieldHomeOffset]);
    gui::configureNumericOverlay(paramTexts[kFieldPositionGain], PositionGain, paramBuffers[kFieldPositionGain]);

    for (uint8_t i = 0U; i < kFieldCount; i++)
    {
        add(paramTexts[i]);
        paramValues[i] = presenter->notifyGetParameterValue(i);
        (void)snprintf(paramInputs[i], KeyBoard::MAX_BUF, "%ld", static_cast<long>(paramValues[i]));
        updateFieldText(static_cast<int8_t>(i), paramInputs[i]);
    }

    keyBoard1.setEnterCallback(keyboardEnterCallback);
    keyBoard1.setBufferChangedCallback(keyboardChangedCallback);
    remove(keyBoard1);
    add(keyBoard1);
    keyBoard1.setVisible(false);
    keyBoard1.invalidate();
}

void ParameterPageView::tearDownScreen()
{
    hideKeyboard();
    ParameterPageViewBase::tearDownScreen();
}

void ParameterPageView::handleTickEvent()
{
    ParameterPageViewBase::handleTickEvent();

    if (parameterReadAllWaiting == false)
    {
        return;
    }

    if (presenter->notifyFetchReadAllParametersFromDrive())
    {
        for (uint8_t i = 0U; i < kFieldCount; i++)
        {
            const int32_t value = presenter->notifyGetParameterValue(i);
            (void)snprintf(paramInputs[i], KeyBoard::MAX_BUF, "%ld", static_cast<long>(value));
            updateFieldText(static_cast<int8_t>(i), paramInputs[i]);
        }

        parameterReadAllWaiting = false;
        parameterReadAllWaitTicks = 0U;
        return;
    }

    if (parameterReadAllWaitTicks < 1000U)
    {
        parameterReadAllWaitTicks++;
    }

    if (parameterReadAllWaitTicks > 180U)
    {
        parameterReadAllWaiting = false;
        parameterReadAllWaitTicks = 0U;
    }
}

void ParameterPageView::handleClickEvent(const touchgfx::ClickEvent& evt)
{
    ParameterPageViewBase::handleClickEvent(evt);

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
        if (activeField != kNoActiveField)
        {
            applyFieldText(activeField, paramInputs[activeField]);
            hideKeyboard();
        }
        (void)presenter->notifySaveParameterPageToUiFlash();
        return;
    }

    if (inRect(Main_button_1_1))
    {
        if (activeField != kNoActiveField)
        {
            hideKeyboard();
        }

        if (presenter->notifyLoadParameterPageFromUiFlash())
        {
            for (uint8_t i = 0U; i < kFieldCount; i++)
            {
                const int32_t loadedValue = presenter->notifyGetParameterValue(i);
                (void)snprintf(paramInputs[i], KeyBoard::MAX_BUF, "%ld", static_cast<long>(loadedValue));
                updateFieldText(static_cast<int8_t>(i), paramInputs[i]);
            }
        }
        return;
    }

    if (inRect(Main_button))
    {
        return;
    }

    if (inRect(ReadAll))
    {
        if (activeField != kNoActiveField)
        {
            applyFieldText(activeField, paramInputs[activeField]);
            hideKeyboard();
        }

        presenter->notifyRequestReadAllParametersFromDrive();
        parameterReadAllWaiting = true;
        parameterReadAllWaitTicks = 0U;
        return;
    }

    if (inRect(WriteAll))
    {
        if (activeField != kNoActiveField)
        {
            applyFieldText(activeField, paramInputs[activeField]);
            hideKeyboard();
        }

        presenter->notifyWriteAllParametersToDrive();
        return;
    }

    for (int8_t i = 0; i < kFieldCount; i++)
    {
        if (inRect(paramTexts[i]))
        {
            showKeyboardForField(i);
            return;
        }
    }

    if (activeField != kNoActiveField)
    {
        applyFieldText(activeField, paramInputs[activeField]);
    }
    hideKeyboard();
}

void ParameterPageView::showKeyboardForField(int8_t fieldIndex)
{
    if (fieldIndex < 0 || fieldIndex >= kFieldCount)
    {
        return;
    }

    if (activeField != kNoActiveField && activeField != fieldIndex)
    {
        applyFieldText(activeField, paramInputs[activeField]);
    }

    activeField = fieldIndex;
    suppressKeyboardEcho = true;
    keyBoard1.clearBuffer();
    suppressKeyboardEcho = false;
    remove(keyBoard1);
    add(keyBoard1);
    keyBoard1.setVisible(true);
    keyBoard1.invalidate();
}

void ParameterPageView::hideKeyboard()
{
    keyBoard1.setVisible(false);
    keyBoard1.invalidate();
    activeField = kNoActiveField;
}

void ParameterPageView::applyFieldText(int8_t fieldIndex, const char* text)
{
    if (fieldIndex < 0 || fieldIndex >= kFieldCount)
    {
        return;
    }

    const char* safeText = (text != 0 && text[0] != '\0') ? text : "0";
    const int32_t parsed = parseSigned32(safeText);

    updateFieldText(fieldIndex, safeText);
    presenter->notifySetParameterValue(static_cast<uint8_t>(fieldIndex), parsed);
}

void ParameterPageView::updateFieldText(int8_t fieldIndex, const char* text)
{
    if (fieldIndex < 0 || fieldIndex >= kFieldCount)
    {
        return;
    }

    const char* safeText = (text != 0 && text[0] != '\0') ? text : "0";
    const int32_t parsed = parseSigned32(safeText);

    gui::formatSignedWithCommas(parsed, paramBuffers[fieldIndex], gui::kNumericBufferSize);
    paramTexts[fieldIndex].invalidate();

    paramValues[fieldIndex] = parsed;
}

void ParameterPageView::onKeyboardEnter(const char* text)
{
    if (activeField == kNoActiveField)
    {
        return;
    }

    const char* safeText = (text != 0 && text[0] != '\0') ? text : "0";
    memset(paramInputs[activeField], 0, sizeof(paramInputs[activeField]));
    strncpy(paramInputs[activeField], safeText, static_cast<size_t>(KeyBoard::MAX_BUF - 1));
    applyFieldText(activeField, paramInputs[activeField]);
    hideKeyboard();
}

void ParameterPageView::onKeyboardBufferChanged(const char* text)
{
    if (suppressKeyboardEcho || activeField == kNoActiveField)
    {
        return;
    }

    const char* safeText = (text != 0 && text[0] != '\0') ? text : "0";
    memset(paramInputs[activeField], 0, sizeof(paramInputs[activeField]));
    strncpy(paramInputs[activeField], safeText, static_cast<size_t>(KeyBoard::MAX_BUF - 1));
    updateFieldText(activeField, paramInputs[activeField]);
}

int32_t ParameterPageView::parseSigned32(const char* text) const
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
