#include <gui/manualpage_screen/ManualPageView.hpp>
#include <gui/manualpage_screen/ManualPagePresenter.hpp>

ManualPagePresenter::ManualPagePresenter(ManualPageView& v)
    : view(v)
{
}

void ManualPagePresenter::activate()
{
}

void ManualPagePresenter::deactivate()
{
}

void ManualPagePresenter::onMotionDataUpdated(int32_t position, int32_t speed, int16_t torque)
{
    view.updateMotionData(position, speed, torque);
}

void ManualPagePresenter::notifySendPositionDelta(int32_t delta)
{
    model->sendPositionDelta(delta);
}

void ManualPagePresenter::notifySetTargetPositionAbs(int32_t pos)
{
    model->setTargetPositionAbs(pos);
}

int32_t ManualPagePresenter::notifyGetPositionActual()
{
    return model->getPositionActual();
}

int32_t ManualPagePresenter::notifyGetJogStepCounts()
{
    return model->getJogStepCounts();
}

void ManualPagePresenter::notifySetManualCyclePosition(int32_t position)
{
    model->setManualCyclePosition(position);
}

int32_t ManualPagePresenter::notifyGetManualCyclePosition()
{
    return model->getManualCyclePosition();
}

void ManualPagePresenter::notifySetManualCycleSpeed(int32_t speed)
{
    model->setManualCycleSpeed(speed);
}

int32_t ManualPagePresenter::notifyGetManualCycleSpeed()
{
    return model->getManualCycleSpeed();
}

void ManualPagePresenter::notifySetManualCycleTorque(int16_t torque)
{
    model->setManualCycleTorque(torque);
}

int16_t ManualPagePresenter::notifyGetManualCycleTorque()
{
    return model->getManualCycleTorque();
}

void ManualPagePresenter::notifySetManualCycleAbsMode(uint8_t absMode)
{
    model->setManualCycleAbsMode(absMode);
}

uint8_t ManualPagePresenter::notifyGetManualCycleAbsMode()
{
    return model->getManualCycleAbsMode();
}

bool ManualPagePresenter::notifySaveManualPageToUiFlash()
{
    return model->saveManualPageToUiFlash();
}

bool ManualPagePresenter::notifyLoadManualPageFromUiFlash()
{
    return model->loadManualPageFromUiFlash();
}
