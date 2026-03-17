#ifndef MAINPAGEPRESENTER_HPP
#define MAINPAGEPRESENTER_HPP

#include <gui/model/ModelListener.hpp>
#include <mvp/Presenter.hpp>

using namespace touchgfx;

class MainPageView;

class MainPagePresenter : public touchgfx::Presenter, public ModelListener
{
public:
    MainPagePresenter(MainPageView& v);

    virtual void activate();
    virtual void deactivate();
    virtual ~MainPagePresenter() {}

    // ModelListener 콜백 - Model이 매 tick마다 호출
    virtual void onMotionDataUpdated(int32_t position, int32_t speed, int16_t torque);
    virtual void onRunEnableChanged(uint8_t enabled);

    // View → Model 커맨드
    void notifySetRunEnable(uint8_t enable);
    void notifySendPositionDelta(int32_t delta);
    void notifySetJogStepCounts(int32_t counts);
    void notifyCommitPersistentState();
    int32_t notifyGetJogStepCounts();
    uint8_t notifyGetRunEnable();

private:
    MainPagePresenter();
    MainPageView& view;
};

#endif // MAINPAGEPRESENTER_HPP
