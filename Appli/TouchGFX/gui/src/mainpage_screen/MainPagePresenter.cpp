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

uint8_t MainPagePresenter::notifyGetRunEnable()
{
    return model->getRunEnable();
}
