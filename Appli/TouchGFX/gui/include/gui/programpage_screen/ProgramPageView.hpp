#ifndef PROGRAMPAGEVIEW_HPP
#define PROGRAMPAGEVIEW_HPP

#include <gui/common/NumberFormat.hpp>
#include <gui_generated/programpage_screen/ProgramPageViewBase.hpp>
#include <gui/programpage_screen/ProgramPagePresenter.hpp>
#include <touchgfx/widgets/TextAreaWithWildcard.hpp>

class ProgramPageView : public ProgramPageViewBase
{
public:
    ProgramPageView();
    virtual ~ProgramPageView() {}
    virtual void setupScreen();
    virtual void tearDownScreen();
    virtual void handleTickEvent();
    virtual void handleClickEvent(const touchgfx::ClickEvent& evt);
    void updateMotionData(int32_t position, int32_t speed, int16_t torque, int32_t actualPositionHw);
protected:
    enum
    {
        kTargetFieldCount = 10,
        kAllInputFieldCount = 11,
        kNoActiveField = -1
    };

    // Current-value readback overlays: [0]=Position, [1]=Speed, [2]=Torque, [3]=ActualPosition
    touchgfx::TextAreaWithOneWildcard numericTexts[4];
    touchgfx::Unicode::UnicodeChar numericBuffers[4][gui::kNumericBufferSize];

    // Target-value input overlays: [0-2]=Position1-3, [3-5]=Speed1-3, [6-8]=Torque1-3, [9]=ReturnSpeed
    touchgfx::TextAreaWithOneWildcard targetTexts[kTargetFieldCount];
    touchgfx::Unicode::UnicodeChar targetBuffers[kTargetFieldCount][gui::kNumericBufferSize];

    // Delay time overlay
    touchgfx::TextAreaWithOneWildcard delayText;
    touchgfx::Unicode::UnicodeChar delayBuffer[gui::kNumericBufferSize];

    int8_t activeInputField;
    bool suppressKeyboardEcho;
    char fieldInputValues[kAllInputFieldCount][KeyBoard::MAX_BUF];

    touchgfx::Callback<ProgramPageView, const char*> keyboardEnterCallback;
    touchgfx::Callback<ProgramPageView, const char*> keyboardChangedCallback;
    touchgfx::Callback<ProgramPageView, const touchgfx::AbstractButton&> pageButtonCallback;

    void showKeyboardForField(int8_t fieldIndex);
    void hideKeyboard();
    void onPageButtonPressed(const touchgfx::AbstractButton& src);
    void updateFieldText(int8_t fieldIndex, const char* text);
    void applyFieldText(int8_t fieldIndex, const char* text);
    void onKeyboardEnter(const char* text);
    void onKeyboardBufferChanged(const char* text);
    int32_t parseSigned32(const char* text) const;
};

#endif // PROGRAMPAGEVIEW_HPP
