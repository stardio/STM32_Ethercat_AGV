#ifndef MANUALPAGEPRESENTER_HPP
#define MANUALPAGEPRESENTER_HPP

#include <gui/model/ModelListener.hpp>
#include <mvp/Presenter.hpp>

using namespace touchgfx;

class ManualPageView;

class ManualPagePresenter : public touchgfx::Presenter, public ModelListener
{
public:
    ManualPagePresenter(ManualPageView& v);

    virtual void activate();
    virtual void deactivate();
    virtual ~ManualPagePresenter() {}

    virtual void onMotionDataUpdated(int32_t position, int32_t speed, int16_t torque);

    void notifySendPositionDelta(int32_t delta);
    void notifySetTargetPositionAbs(int32_t pos);
    int32_t notifyGetPositionActual();
    int32_t notifyGetJogStepCounts();
    void notifySetManualCyclePosition(int32_t position);
    int32_t notifyGetManualCyclePosition();
    void notifySetManualCycleSpeed(int32_t speed);
    int32_t notifyGetManualCycleSpeed();
    void notifySetManualCycleTorque(int16_t torque);
    int16_t notifyGetManualCycleTorque();
    void notifySetManualCycleAbsMode(uint8_t absMode);
    uint8_t notifyGetManualCycleAbsMode();
    bool notifySaveManualPageToUiFlash();
    bool notifyLoadManualPageFromUiFlash();

private:
    ManualPagePresenter();
    ManualPageView& view;
};

#endif // MANUALPAGEPRESENTER_HPP
