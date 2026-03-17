#ifndef MAINPAGEVIEW_HPP
#define MAINPAGEVIEW_HPP

#include <stdint.h>
#include <gui/common/NumberFormat.hpp>
#include <gui_generated/mainpage_screen/MainPageViewBase.hpp>
#include <gui/mainpage_screen/MainPagePresenter.hpp>
#include <touchgfx/widgets/TextAreaWithWildcard.hpp>
#include <touchgfx/widgets/ButtonWithIcon.hpp>

class MainPageView : public MainPageViewBase
{
public:
    MainPageView();
    virtual ~MainPageView() {}
    virtual void setupScreen();
    virtual void tearDownScreen();
    virtual void handleTickEvent();
    virtual void handleClickEvent(const touchgfx::ClickEvent& evt);

    virtual void function1();
    virtual void function2();
    virtual void function3();
    virtual void function4(int value);
    virtual void function5();
    virtual void function6();
    virtual void function7();

    void updateMotionData(int32_t position, int32_t speed, int16_t torque);
    void updateRunEnable(uint8_t enabled);
protected:
    int jogStepCounts;
    uint8_t runUiState;
    uint8_t runAppliedState;
    uint8_t runDebounceCount;
    touchgfx::TextAreaWithOneWildcard numericTexts[3];
    touchgfx::Unicode::UnicodeChar numericBuffers[3][gui::kNumericBufferSize];
    touchgfx::ButtonWithIcon saveButton;
    touchgfx::Callback<MainPageView, const touchgfx::AbstractButton&> saveButtonCallback;

    void onSaveButtonPressed(const touchgfx::AbstractButton& src);
};

#endif // MAINPAGEVIEW_HPP
