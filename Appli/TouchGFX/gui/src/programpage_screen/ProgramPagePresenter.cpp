#include <gui/programpage_screen/ProgramPageView.hpp>
#include <gui/programpage_screen/ProgramPagePresenter.hpp>

ProgramPagePresenter::ProgramPagePresenter(ProgramPageView& v)
    : view(v)
{
}

void ProgramPagePresenter::activate()
{
}

void ProgramPagePresenter::deactivate()
{
}

void ProgramPagePresenter::onMotionDataUpdated(int32_t position, int32_t speed, int16_t torque)
{
    view.updateMotionData(position, speed, torque);
}

void ProgramPagePresenter::notifySetProgramValue(uint8_t index, int32_t value)
{
    model->setProgramValue(index, value);
}

int32_t ProgramPagePresenter::notifyGetProgramValue(uint8_t index)
{
    return model->getProgramValue(index);
}

bool ProgramPagePresenter::notifySaveProgramPageToUiFlash()
{
    return model->saveProgramPageToUiFlash();
}

bool ProgramPagePresenter::notifyLoadProgramPageFromUiFlash()
{
    return model->loadProgramPageFromUiFlash();
}

bool ProgramPagePresenter::notifyStartProgramSequence()
{
    return model->startProgramSequence();
}

void ProgramPagePresenter::notifyStopProgramSequence()
{
    model->stopProgramSequence();
}
