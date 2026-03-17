#ifndef PROGRAMPAGEPRESENTER_HPP
#define PROGRAMPAGEPRESENTER_HPP

#include <gui/model/ModelListener.hpp>
#include <mvp/Presenter.hpp>

using namespace touchgfx;

class ProgramPageView;

class ProgramPagePresenter : public touchgfx::Presenter, public ModelListener
{
public:
    ProgramPagePresenter(ProgramPageView& v);

    virtual void activate();
    virtual void deactivate();
    virtual ~ProgramPagePresenter() {}

    virtual void onMotionDataUpdated(int32_t position, int32_t speed, int16_t torque);
    void notifySetProgramValue(uint8_t index, int32_t value);
    int32_t notifyGetProgramValue(uint8_t index);
    bool notifySaveProgramPageToUiFlash();
    bool notifyLoadProgramPageFromUiFlash();

private:
    ProgramPagePresenter();
    ProgramPageView& view;
};

#endif // PROGRAMPAGEPRESENTER_HPP
