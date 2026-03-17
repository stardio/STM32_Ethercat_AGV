#ifndef MANUALPAGEVIEW_HPP
#define MANUALPAGEVIEW_HPP

#include <stdint.h>
#include <gui/common/NumberFormat.hpp>
#include <gui_generated/manualpage_screen/ManualPageViewBase.hpp>
#include <gui/manualpage_screen/ManualPagePresenter.hpp>
#include <touchgfx/widgets/TextAreaWithWildcard.hpp>

class ManualPageView : public ManualPageViewBase
{
public:
    ManualPageView();
    virtual ~ManualPageView() {}
    virtual void setupScreen();
    virtual void tearDownScreen();
    virtual void handleTickEvent();
    virtual void handleClickEvent(const touchgfx::ClickEvent& evt);

    virtual void function1();
    virtual void function2();
    virtual void function3();
    virtual void function4();
    virtual void function5();

    void updateMotionData(int32_t position, int32_t speed, int16_t torque);
protected:
    enum
    {
        kOneCycleFieldPosition = 0,
        kOneCycleFieldSpeed = 1,
        kOneCycleFieldTorque = 2,
        kOneCycleFieldCount = 3,
        kNoActiveField = -1
    };

    int jogStepCounts;

    // Current-value readback: [0]=Position, [1]=Speed, [2]=Torque
    touchgfx::TextAreaWithOneWildcard numericTexts[3];
    touchgfx::Unicode::UnicodeChar numericBuffers[3][gui::kNumericBufferSize];

    // 1Cycle setting overlays: [0]=Position, [1]=Speed, [2]=Torque
    touchgfx::TextAreaWithOneWildcard cycleTexts[kOneCycleFieldCount];
    touchgfx::Unicode::UnicodeChar cycleBuffers[kOneCycleFieldCount][gui::kNumericBufferSize];
    int32_t cycleValues[kOneCycleFieldCount];
    uint8_t cycleAbsMode;
    int8_t activeCycleField;
    bool suppressKeyboardEcho;
    char cycleInputValues[kOneCycleFieldCount][KeyBoard::MAX_BUF];
    int32_t lastActualPosition;

    touchgfx::Callback<ManualPageView, const char*> keyboardEnterCallback;
    touchgfx::Callback<ManualPageView, const char*> keyboardChangedCallback;

    void showKeyboardForCycleField(int8_t fieldIndex);
    void hideKeyboard();
    void updateCycleFieldText(int8_t fieldIndex, const char* text);
    void applyCycleFieldText(int8_t fieldIndex, const char* text);
    void applyAbsIncVisual();
    void onKeyboardEnter(const char* text);
    void onKeyboardBufferChanged(const char* text);
    int32_t parseSigned32(const char* text) const;
};

#endif // MANUALPAGEVIEW_HPP
