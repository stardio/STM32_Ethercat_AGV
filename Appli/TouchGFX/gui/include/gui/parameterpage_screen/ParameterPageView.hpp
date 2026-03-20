#ifndef PARAMETERPAGEVIEW_HPP
#define PARAMETERPAGEVIEW_HPP

#include <gui/common/NumberFormat.hpp>
#include <gui_generated/parameterpage_screen/ParameterPageViewBase.hpp>
#include <gui/parameterpage_screen/ParameterPagePresenter.hpp>
#include <touchgfx/widgets/TextAreaWithWildcard.hpp>

class ParameterPageView : public ParameterPageViewBase
{
public:
    ParameterPageView();
    virtual ~ParameterPageView() {}
    virtual void setupScreen();
    virtual void tearDownScreen();
    virtual void handleTickEvent();
    virtual void handleClickEvent(const touchgfx::ClickEvent& evt);
protected:
    enum
    {
        kFieldJogSpeed = 0,
        kFieldAccTime,
        kFieldDecTime,
        kFieldLimitPlus,
        kFieldLimitMinus,
        kFieldUnitScale,
        kFieldHomeOffset,
        kFieldPositionGain,
        kFieldCount,
        kNoActiveField = -1
    };

    touchgfx::TextAreaWithOneWildcard paramTexts[kFieldCount];
    touchgfx::Unicode::UnicodeChar paramBuffers[kFieldCount][gui::kNumericBufferSize];
    int32_t paramValues[kFieldCount];
    char paramInputs[kFieldCount][KeyBoard::MAX_BUF];
    int8_t activeField;
    bool suppressKeyboardEcho;
    bool parameterReadAllWaiting;
    uint16_t parameterReadAllWaitTicks;

    touchgfx::Callback<ParameterPageView, const char*> keyboardEnterCallback;
    touchgfx::Callback<ParameterPageView, const char*> keyboardChangedCallback;

    void showKeyboardForField(int8_t fieldIndex);
    void hideKeyboard();
    void updateFieldText(int8_t fieldIndex, const char* text);
    void applyFieldText(int8_t fieldIndex, const char* text);
    void onKeyboardEnter(const char* text);
    void onKeyboardBufferChanged(const char* text);
    int32_t parseSigned32(const char* text) const;
};

#endif // PARAMETERPAGEVIEW_HPP
