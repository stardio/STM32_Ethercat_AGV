#ifndef PARAMETERPAGEPRESENTER_HPP
#define PARAMETERPAGEPRESENTER_HPP

#include <gui/model/ModelListener.hpp>
#include <mvp/Presenter.hpp>

using namespace touchgfx;

class ParameterPageView;

class ParameterPagePresenter : public touchgfx::Presenter, public ModelListener
{
public:
    ParameterPagePresenter(ParameterPageView& v);

    /**
     * The activate function is called automatically when this screen is "switched in"
     * (ie. made active). Initialization logic can be placed here.
     */
    virtual void activate();

    /**
     * The deactivate function is called automatically when this screen is "switched out"
     * (ie. made inactive). Teardown functionality can be placed here.
     */
    virtual void deactivate();

    virtual ~ParameterPagePresenter() {}

    void notifySetParameterValue(uint8_t index, int32_t value);
    int32_t notifyGetParameterValue(uint8_t index);
    bool notifySaveParameterPageToUiFlash();
    bool notifyLoadParameterPageFromUiFlash();
    void notifyWriteAllParametersToDrive();
    void notifyRequestReadAllParametersFromDrive();
    bool notifyFetchReadAllParametersFromDrive();

private:
    ParameterPagePresenter();

    ParameterPageView& view;
};

#endif // PARAMETERPAGEPRESENTER_HPP
