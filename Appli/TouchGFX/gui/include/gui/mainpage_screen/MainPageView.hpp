#ifndef MAINPAGEVIEW_HPP
#define MAINPAGEVIEW_HPP

#include <stdint.h>
#include <gui/common/NumberFormat.hpp>
#include <gui_generated/mainpage_screen/MainPageViewBase.hpp>
#include <gui/mainpage_screen/MainPagePresenter.hpp>
#include <touchgfx/widgets/TextAreaWithWildcard.hpp>

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

    void updateMotionData(int32_t position, int32_t speed, int16_t torque);
    void updateRunEnable(uint8_t enabled);
protected:
    uint8_t runUiState;
    uint8_t runAppliedState;
    uint8_t runDebounceCount;
    touchgfx::TextAreaWithOneWildcard numericTexts[3];
    touchgfx::Unicode::UnicodeChar numericBuffers[3][gui::kNumericBufferSize];
};

#endif // MAINPAGEVIEW_HPP
