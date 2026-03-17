#include <gui/parameterpage_screen/ParameterPageView.hpp>
#include <gui/parameterpage_screen/ParameterPagePresenter.hpp>

ParameterPagePresenter::ParameterPagePresenter(ParameterPageView& v)
    : view(v)
{

}

void ParameterPagePresenter::activate()
{

}

void ParameterPagePresenter::deactivate()
{

}

void ParameterPagePresenter::notifySetParameterValue(uint8_t index, int32_t value)
{
    model->setParameterValue(index, value);
}

int32_t ParameterPagePresenter::notifyGetParameterValue(uint8_t index)
{
    return model->getParameterValue(index);
}

bool ParameterPagePresenter::notifySaveParameterPageToUiFlash()
{
    return model->saveParameterPageToUiFlash();
}

bool ParameterPagePresenter::notifyLoadParameterPageFromUiFlash()
{
    return model->loadParameterPageFromUiFlash();
}
