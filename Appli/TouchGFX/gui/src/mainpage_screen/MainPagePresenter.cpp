#include <gui/mainpage_screen/MainPageView.hpp>
#include <gui/mainpage_screen/MainPagePresenter.hpp>

MainPagePresenter::MainPagePresenter(MainPageView& v)
    : view(v)
{
}

void MainPagePresenter::activate()
{
}

void MainPagePresenter::deactivate()
{
}

void MainPagePresenter::onMotionDataUpdated(int32_t position, int32_t speed, int16_t torque)
{
    view.updateMotionData(position, speed, torque);
}

void MainPagePresenter::onRunEnableChanged(uint8_t enabled)
{
    view.updateRunEnable(enabled);
}

void MainPagePresenter::notifySetRunEnable(uint8_t enable)
{
    model->setRunEnable(enable);
}

void MainPagePresenter::notifySetJogStepCounts(int32_t counts)
{
    model->setJogStepCounts(counts);
}

void MainPagePresenter::notifyCommitPersistentState()
{
    model->commitPersistentState();
}

int32_t MainPagePresenter::notifyGetJogStepCounts()
{
    return model->getJogStepCounts();
}

uint8_t MainPagePresenter::notifyGetRunEnable()
{
    return model->getRunEnable();
}

void MainPagePresenter::notifySendPositionDelta(int32_t delta)
{
    model->sendPositionDelta(delta);
}
