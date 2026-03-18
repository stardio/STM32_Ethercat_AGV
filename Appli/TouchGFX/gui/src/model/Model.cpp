#include <gui/model/Model.hpp>
#include <gui/model/ModelListener.hpp>
#include "settings_persistence.h"
#include "ui_flash_storage.h"
#include <stdint.h>

#if !defined(SIMULATOR)
extern "C"
{
uint8_t  SOEM_GetPdoReady(void);
uint8_t  SOEM_GetRunEnable(void);
uint16_t SOEM_GetStatusword(void);
int32_t  SOEM_GetPositionActual(void);
int32_t  SOEM_GetVelocityActual(void);
int16_t  SOEM_GetTorqueActual(void);
void     SOEM_SetRunEnable(uint8_t enable);
void     SOEM_SetTargetPositionDelta(int32_t delta);
void     SOEM_SetTargetPositionAbs(int32_t pos);
void     SOEM_SetProfileVelocity(int32_t velocity);
void     SOEM_SetTorqueLimitPercent(uint16_t percent);
void     SOEM_SetProfileAcceleration(int32_t acceleration);
void     SOEM_SetProfileDeceleration(int32_t deceleration);
void     SOEM_SetSoftwareLimitPlus(int32_t limitPlus);
void     SOEM_SetSoftwareLimitMinus(int32_t limitMinus);
void     SOEM_SetUnitScale(int32_t scale);
void     SOEM_SetHomeOffset(int32_t offset);
void     SOEM_SetHomePosition(void);
}
#endif

namespace
{
#if defined(SIMULATOR)
uint8_t simRunEnable = 0U;
int32_t simPositionActual = 0;
int32_t simVelocityActual = 0;
int16_t simTorqueActual = 0;
int32_t simProfileVelocity = 0;
uint16_t simTorqueLimitPercent = 0U;
int32_t simProfileAcceleration = 0;
int32_t simProfileDeceleration = 0;
int32_t simLimitPlus = 0;
int32_t simLimitMinus = 0;
int32_t simUnitScale = 1;
int32_t simHomeOffset = 0;

uint8_t modelGetPdoReady()
{
    return 1U;
}

uint8_t modelGetRunEnable()
{
    return simRunEnable;
}

uint16_t modelGetStatusword()
{
    return (simRunEnable != 0U) ? 0x0027U : 0x0021U;
}

int32_t modelGetPositionActual()
{
    return simPositionActual;
}

int32_t modelGetVelocityActual()
{
    return simVelocityActual;
}

int16_t modelGetTorqueActual()
{
    return simTorqueActual;
}

void modelSetRunEnable(uint8_t enable)
{
    simRunEnable = (enable != 0U) ? 1U : 0U;
}

void modelSetTargetPositionDelta(int32_t delta)
{
    simPositionActual += delta;
    simVelocityActual = (delta < 0) ? -delta : delta;

    // Keep torque in a reasonable display range for simulator-only UI checks.
    int32_t torqueCandidate = simVelocityActual / 10;
    if (torqueCandidate > 200)
    {
        torqueCandidate = 200;
    }
    simTorqueActual = static_cast<int16_t>(torqueCandidate);
}

void modelSetTargetPositionAbs(int32_t pos)
{
    simPositionActual = pos;
}

void modelSetProfileVelocity(int32_t velocity)
{
    simProfileVelocity = velocity;
}

void modelSetTorqueLimitPercent(uint16_t percent)
{
    simTorqueLimitPercent = percent;
    (void)simTorqueLimitPercent;
}

void modelSetProfileAcceleration(int32_t acceleration)
{
    simProfileAcceleration = acceleration;
}

void modelSetProfileDeceleration(int32_t deceleration)
{
    simProfileDeceleration = deceleration;
}

void modelSetSoftwareLimitPlus(int32_t limitPlus)
{
    simLimitPlus = limitPlus;
}

void modelSetSoftwareLimitMinus(int32_t limitMinus)
{
    simLimitMinus = limitMinus;
}

void modelSetUnitScale(int32_t scale)
{
    simUnitScale = scale;
    (void)simUnitScale;
}

void modelSetHomeOffset(int32_t offset)
{
    simHomeOffset = offset;
    (void)simHomeOffset;
}

void modelSetHomePosition()
{
    simPositionActual = 0;
}
#else
uint8_t modelGetPdoReady()
{
    return SOEM_GetPdoReady();
}

uint8_t modelGetRunEnable()
{
    return SOEM_GetRunEnable();
}

uint16_t modelGetStatusword()
{
    return SOEM_GetStatusword();
}

int32_t modelGetPositionActual()
{
    return SOEM_GetPositionActual();
}

int32_t modelGetVelocityActual()
{
    return SOEM_GetVelocityActual();
}

int16_t modelGetTorqueActual()
{
    return SOEM_GetTorqueActual();
}

void modelSetRunEnable(uint8_t enable)
{
    SOEM_SetRunEnable(enable);
}

void modelSetTargetPositionDelta(int32_t delta)
{
    SOEM_SetTargetPositionDelta(delta);
}

void modelSetTargetPositionAbs(int32_t pos)
{
    SOEM_SetTargetPositionAbs(pos);
}

void modelSetProfileVelocity(int32_t velocity)
{
    SOEM_SetProfileVelocity(velocity);
}

void modelSetTorqueLimitPercent(uint16_t percent)
{
    SOEM_SetTorqueLimitPercent(percent);
}

void modelSetProfileAcceleration(int32_t acceleration)
{
    SOEM_SetProfileAcceleration(acceleration);
}

void modelSetProfileDeceleration(int32_t deceleration)
{
    SOEM_SetProfileDeceleration(deceleration);
}

void modelSetSoftwareLimitPlus(int32_t limitPlus)
{
    SOEM_SetSoftwareLimitPlus(limitPlus);
}

void modelSetSoftwareLimitMinus(int32_t limitMinus)
{
    SOEM_SetSoftwareLimitMinus(limitMinus);
}

void modelSetUnitScale(int32_t scale)
{
    SOEM_SetUnitScale(scale);
}

void modelSetHomeOffset(int32_t offset)
{
    SOEM_SetHomeOffset(offset);
}

void modelSetHomePosition()
{
    SOEM_SetHomePosition();
}
#endif
}

Model::Model()
        : modelListener(0),
            jogStepCounts_(1000),
            runEnable_(0),
            manualCyclePosition_(0),
            manualCycleSpeed_(0),
            manualCycleTorque_(0),
            manualCycleAbsMode_(1U),
            suppressPersistence_(false),
            persistentDirty_(false)
{
    for (uint8_t i = 0U; i < kParamCount; i++)
    {
        parameterValues_[i] = 0;
    }
    for (uint8_t i = 0U; i < kProgramValueCount; i++)
    {
        programValues_[i] = 0;
    }
    parameterValues_[kParamJogSpeed] = jogStepCounts_;
    parameterValues_[kParamUnitScale] = 1;

    const bool restoredPersistentState = loadPersistentState();

    if (restoredPersistentState == false)
    {
        const bool parameterLoaded = loadParameterPageFromUiFlash();
        const bool manualLoaded = loadManualPageFromUiFlash();
        const bool programLoaded = loadProgramPageFromUiFlash();

        if (parameterLoaded == false)
        {
            initializeParameterFlashDefaults();
        }

        if (manualLoaded == false)
        {
            manualCyclePosition_ = 0;
            manualCycleSpeed_ = 0;
            manualCycleTorque_ = 0;
            manualCycleAbsMode_ = 1U;
        }

        if (programLoaded == false)
        {
            for (uint8_t i = 0U; i < kProgramValueCount; i++)
            {
                programValues_[i] = 0;
            }
        }
    }
}

void Model::setJogStepCounts(int32_t counts)
{
    if (counts <= 0)
    {
        counts = 10;
    }
    jogStepCounts_ = counts;
    parameterValues_[kParamJogSpeed] = counts;
    markPersistentDirty();
}

int32_t Model::getJogStepCounts() const
{
    return jogStepCounts_;
}

void Model::tick()
{
    if (modelListener == 0)
    {
        return;
    }

    if (modelGetPdoReady() != 0U)
    {
        modelListener->onMotionDataUpdated(
            modelGetPositionActual(),
            modelGetVelocityActual(),
            modelGetTorqueActual());

        runEnable_ = modelGetRunEnable();
        modelListener->onRunEnableChanged(runEnable_);
    }
}

uint8_t Model::getRunEnable() const
{
    return runEnable_;
}

int32_t Model::getPositionActual() const
{
    return modelGetPositionActual();
}

void Model::setRunEnable(uint8_t enable)
{
    modelSetRunEnable(enable);
}

void Model::sendPositionDelta(int32_t delta)
{
    if (modelGetPdoReady() == 0U)
    {
        return;
    }

    if (modelGetRunEnable() == 0U)
    {
        return;
    }

    const uint16_t statusword = modelGetStatusword();
    const bool opEnabled = ((statusword & 0x006FU) == 0x0027U);
    if (!opEnabled)
    {
        return;
    }

    modelSetTargetPositionDelta(delta);
}

void Model::setTargetPositionAbs(int32_t pos)
{
    modelSetTargetPositionAbs(pos);
}

void Model::setHomePosition()
{
    modelSetHomePosition();
}

void Model::setManualCyclePosition(int32_t position)
{
    manualCyclePosition_ = position;
    markPersistentDirty();
}

int32_t Model::getManualCyclePosition() const
{
    return manualCyclePosition_;
}

void Model::setManualCycleSpeed(int32_t speed)
{
    int32_t speedAbs = speed;
    if (speedAbs < 0)
    {
        speedAbs = -speedAbs;
    }
    if (speedAbs <= 0)
    {
        speedAbs = 1;
    }

    manualCycleSpeed_ = speedAbs;
    modelSetProfileVelocity(speedAbs);
    markPersistentDirty();
}

int32_t Model::getManualCycleSpeed() const
{
    return manualCycleSpeed_;
}

void Model::setManualCycleTorque(int16_t torque)
{
    int32_t torqueAbs = torque;
    if (torqueAbs < 0)
    {
        torqueAbs = -torqueAbs;
    }
    if (torqueAbs <= 0)
    {
        torqueAbs = 1;
    }
    if (torqueAbs > 100)
    {
        torqueAbs = 100;
    }

    manualCycleTorque_ = static_cast<int16_t>(torqueAbs);
    modelSetTorqueLimitPercent(static_cast<uint16_t>(torqueAbs));
    markPersistentDirty();
}

int16_t Model::getManualCycleTorque() const
{
    return manualCycleTorque_;
}

void Model::setManualCycleAbsMode(uint8_t absMode)
{
    manualCycleAbsMode_ = (absMode != 0U) ? 1U : 0U;
    markPersistentDirty();
}

uint8_t Model::getManualCycleAbsMode() const
{
    return manualCycleAbsMode_;
}

void Model::setParameterValue(uint8_t index, int32_t value)
{
    if (index >= kParamCount)
    {
        return;
    }

    if (index == kParamJogSpeed)
    {
        setJogStepCounts(value);
        value = jogStepCounts_;
    }
    else if (index == kParamAccTime)
    {
        int32_t accel = value;
        if (accel < 0)
        {
            accel = -accel;
        }
        modelSetProfileAcceleration(accel);
    }
    else if (index == kParamDecTime)
    {
        int32_t decel = value;
        if (decel < 0)
        {
            decel = -decel;
        }
        modelSetProfileDeceleration(decel);
    }
    else if (index == kParamLimitPlus)
    {
        modelSetSoftwareLimitPlus(value);
    }
    else if (index == kParamLimitMinus)
    {
        modelSetSoftwareLimitMinus(value);
    }
    else if (index == kParamUnitScale)
    {
        if (value <= 0)
        {
            value = 1;
        }
        modelSetUnitScale(value);
    }
    else if (index == kParamHomeOffset)
    {
        modelSetHomeOffset(value);
    }

    parameterValues_[index] = value;
    markPersistentDirty();
}

int32_t Model::getParameterValue(uint8_t index) const
{
    if (index >= kParamCount)
    {
        return 0;
    }
    return parameterValues_[index];
}

void Model::setProgramValue(uint8_t index, int32_t value)
{
    if (index >= kProgramValueCount)
    {
        return;
    }

    programValues_[index] = value;
    markPersistentDirty();
}

void Model::commitPersistentState()
{
    if (persistentDirty_ == false)
    {
        return;
    }

    savePersistentState();
    persistentDirty_ = false;
}

bool Model::saveProgramPageToUiFlash()
{
    UiFlashProgramData data;

    for (uint8_t index = 0U; index < kProgramValueCount; index++)
    {
        data.values[index] = programValues_[index];
    }

    return (UiFlashStorage_SaveProgram(&data) != 0U);
}

bool Model::loadProgramPageFromUiFlash()
{
    UiFlashProgramData data;

    if (UiFlashStorage_LoadProgram(&data) == 0U)
    {
        return false;
    }

    suppressPersistence_ = true;
    for (uint8_t index = 0U; index < kProgramValueCount; index++)
    {
        setProgramValue(index, data.values[index]);
    }
    suppressPersistence_ = false;

    return true;
}

bool Model::saveManualPageToUiFlash()
{
    UiFlashManualData data;

    data.position = manualCyclePosition_;
    data.speed = manualCycleSpeed_;
    data.torque = manualCycleTorque_;
    data.absMode = manualCycleAbsMode_;
    data.reserved[0] = 0U;
    data.reserved[1] = 0U;
    data.reserved[2] = 0U;
    data.reserved[3] = 0U;
    data.reserved[4] = 0U;

    return (UiFlashStorage_SaveManual(&data) != 0U);
}

bool Model::loadManualPageFromUiFlash()
{
    UiFlashManualData data;

    if (UiFlashStorage_LoadManual(&data) == 0U)
    {
        return false;
    }

    suppressPersistence_ = true;
    setManualCyclePosition(data.position);
    setManualCycleSpeed(data.speed);
    setManualCycleTorque(data.torque);
    setManualCycleAbsMode(data.absMode);
    suppressPersistence_ = false;

    return true;
}

bool Model::saveParameterPageToUiFlash()
{
    UiFlashParameterData data;

    for (uint8_t index = 0U; index < kParamCount; index++)
    {
        data.values[index] = parameterValues_[index];
    }

    return (UiFlashStorage_SaveParameter(&data) != 0U);
}

bool Model::loadParameterPageFromUiFlash()
{
    UiFlashParameterData data;

    if (UiFlashStorage_LoadParameter(&data) == 0U)
    {
        return false;
    }

    suppressPersistence_ = true;
    for (uint8_t index = 0U; index < kParamCount; index++)
    {
        setParameterValue(index, data.values[index]);
    }
    suppressPersistence_ = false;

    return true;
}

int32_t Model::getProgramValue(uint8_t index) const
{
    if (index >= kProgramValueCount)
    {
        return 0;
    }

    return programValues_[index];
}

void Model::savePersistentState()
{
    PersistentSettingsData savedState;

    savedState.magic = 0U;
    savedState.version = 0U;
    savedState.checksum = 0U;
    savedState.jogStepCounts = jogStepCounts_;
    savedState.manualCyclePosition = manualCyclePosition_;
    savedState.manualCycleSpeed = manualCycleSpeed_;
    savedState.manualCycleTorque = manualCycleTorque_;
    savedState.manualCycleAbsMode = manualCycleAbsMode_;
    savedState.reserved0 = 0U;

    for (uint8_t index = 0U; index < kParamCount; index++)
    {
        savedState.parameterValues[index] = parameterValues_[index];
    }
    for (uint8_t index = 0U; index < kProgramValueCount; index++)
    {
        savedState.programValues[index] = programValues_[index];
    }

    SettingsPersistence_Save(&savedState);
}

void Model::markPersistentDirty()
{
    if (!suppressPersistence_)
    {
        persistentDirty_ = true;
    }
}

void Model::initializeParameterFlashDefaults()
{
    (void)saveParameterPageToUiFlash();
}

bool Model::loadPersistentState()
{
    PersistentSettingsData savedState;

    if (SettingsPersistence_Load(&savedState) == 0U)
    {
        return false;
    }

    suppressPersistence_ = true;

    setJogStepCounts(savedState.jogStepCounts);
    setManualCyclePosition(savedState.manualCyclePosition);
    setManualCycleSpeed(savedState.manualCycleSpeed);
    setManualCycleTorque(savedState.manualCycleTorque);
    setManualCycleAbsMode(savedState.manualCycleAbsMode);

    setParameterValue(kParamJogSpeed, savedState.parameterValues[kParamJogSpeed]);
    setParameterValue(kParamAccTime, savedState.parameterValues[kParamAccTime]);
    setParameterValue(kParamDecTime, savedState.parameterValues[kParamDecTime]);
    setParameterValue(kParamUnitScale, savedState.parameterValues[kParamUnitScale]);
    setParameterValue(kParamHomeOffset, savedState.parameterValues[kParamHomeOffset]);
    setParameterValue(kParamLimitPlus, savedState.parameterValues[kParamLimitPlus]);
    setParameterValue(kParamLimitMinus, savedState.parameterValues[kParamLimitMinus]);

    for (uint8_t index = 0U; index < kProgramValueCount; index++)
    {
        setProgramValue(index, savedState.programValues[index]);
    }

    suppressPersistence_ = false;
    persistentDirty_ = false;

    return true;
}
