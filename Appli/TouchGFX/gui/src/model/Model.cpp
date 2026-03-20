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
int32_t  SOEM_GetPositionActualHw(void);
int32_t  SOEM_GetVelocityActual(void);
int16_t  SOEM_GetTorqueActual(void);
void     SOEM_SetRunEnable(uint8_t enable);
void     SOEM_SetTargetPositionDelta(int32_t delta);
void     SOEM_SetTargetPositionAbs(int32_t pos);
void     SOEM_SetTargetPositionAbsHw(int32_t hwPos);
void     SOEM_SetProfileVelocity(int32_t velocity);
void     SOEM_SetTorqueLimitPercent(uint16_t percent);
void     SOEM_SetProfileAcceleration(int32_t acceleration);
void     SOEM_SetProfileDeceleration(int32_t deceleration);
void     SOEM_SetSoftwareLimitPlus(int32_t limitPlus);
void     SOEM_SetSoftwareLimitMinus(int32_t limitMinus);
void     SOEM_SetUnitScale(int32_t scale);
void     SOEM_SetHomeOffset(int32_t offset);
void     SOEM_SetHomePosition(void);
int32_t  SOEM_GetHomeOffset(void);
void     SOEM_LoadHomeHwOffset(int32_t hwOffset);
void     SOEM_SetPositionGain(int32_t gain);
void     SOEM_RequestParameterReadAll(void);
uint8_t  SOEM_FetchParameterReadAll(int32_t *valuesOut, uint8_t valueCount);
uint32_t HAL_GetTick(void);
}
#endif

namespace
{
// Larger tolerance avoids visible dwell between sequence points caused by fine settle jitter.
#define PROGRAM_SEQUENCE_POSITION_TOLERANCE 100
#define PROGRAM_RETURN_POSITION_TOLERANCE_HW 5

static const uint8_t kParameterReadAllValueCount = 8U;

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
uint32_t simSystemMs = 0U;

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

int32_t modelGetPositionActualHw()
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

void modelSetTargetPositionAbsHw(int32_t hwPos)
{
    simPositionActual = hwPos;
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

int32_t modelGetHomeOffset()
{
    return simHomeOffset;
}

void modelLoadHomeHwOffset(int32_t hwOffset)
{
    simHomeOffset = hwOffset;
    (void)simHomeOffset;
}

void modelSetPositionGain(int32_t gain)
{
    (void)gain;
}

void modelRequestParameterReadAll()
{
}

uint8_t modelFetchParameterReadAll(int32_t *valuesOut, uint8_t valueCount)
{
    (void)valuesOut;
    (void)valueCount;
    return 0U;
}

uint32_t modelGetSystemMs()
{
    return simSystemMs;
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

int32_t modelGetPositionActualHw()
{
    return SOEM_GetPositionActualHw();
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

void modelSetTargetPositionAbsHw(int32_t hwPos)
{
    SOEM_SetTargetPositionAbsHw(hwPos);
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

int32_t modelGetHomeOffset()
{
    return SOEM_GetHomeOffset();
}

void modelLoadHomeHwOffset(int32_t hwOffset)
{
    SOEM_LoadHomeHwOffset(hwOffset);
}

void modelSetPositionGain(int32_t gain)
{
    SOEM_SetPositionGain(gain);
}

void modelRequestParameterReadAll()
{
    SOEM_RequestParameterReadAll();
}

uint8_t modelFetchParameterReadAll(int32_t *valuesOut, uint8_t valueCount)
{
    return SOEM_FetchParameterReadAll(valuesOut, valueCount);
}

uint32_t modelGetSystemMs()
{
    return HAL_GetTick();
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
            programSequenceState_(kProgramSeqIdle),
            programOriginPosition_(0),
            programOriginPositionHw_(0),
            activeProgramTargetPosition_(0),
            programDelayMs_(0U),
            programDelayStartMs_(0U),
            suppressPersistence_(false),
            persistentDirty_(false),
            positionFilteredValue_(0),
            positionFilterIndex_(0)
{
    for (uint8_t i = 0U; i < kParamCount; i++)
    {
        parameterValues_[i] = 0;
    }
    for (uint8_t i = 0U; i < kProgramValueCount; i++)
    {
        programValues_[i] = 0;
    }
    for (uint8_t i = 0U; i < 3U; i++)
    {
        programStepPositions_[i] = 0;
        programStepSpeeds_[i] = 1;
        programStepTorques_[i] = 1U;
    }
    // Initialize position filter buffer
    for (uint8_t i = 0U; i < kFilterDepth; i++)
    {
        positionFilterBuffer_[i] = 0;
    }
    parameterValues_[kParamJogSpeed] = jogStepCounts_;
    parameterValues_[kParamUnitScale] = 1;
    
    // Set default ReturnSpeed to a reasonable value (same as Step3 speed)
    programValues_[kProgramIdxReturnSpeed] = 100;  // Default return speed

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
            // Set default ReturnSpeed when no saved program exists
            programValues_[kProgramIdxReturnSpeed] = 100;
        }
    }

    // Keep runtime conversion and drive-side parameters aligned with the
    // persisted Parameter Page values after boot.
    writeAllParametersToDrive();

    // Restore the hardware home offset saved by the Set-Home button press.
    // This must run AFTER writeAllParametersToDrive() so it overrides any
    // parameter-page HomeOffset that may have been applied above.
    {
        int32_t savedHwOffset = 0;
        if (UiFlashStorage_LoadHome(&savedHwOffset) != 0U)
        {
            modelLoadHomeHwOffset(savedHwOffset);
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
#if defined(SIMULATOR)
    simSystemMs += 16U;
#endif

    const bool pdoReady = (modelGetPdoReady() != 0U);
    if (pdoReady)
    {
        runEnable_ = modelGetRunEnable();
        tickProgramSequence();
        
        // Update position filter (4-point moving average to reduce PDO noise)
        int32_t rawPosition = modelGetPositionActual();
        positionFilterBuffer_[positionFilterIndex_] = rawPosition;
        positionFilterIndex_ = (positionFilterIndex_ + 1) % kFilterDepth;
        
        int64_t sum = 0;
        for (uint8_t i = 0; i < kFilterDepth; i++)
        {
            sum += (int64_t)positionFilterBuffer_[i];
        }
        positionFilteredValue_ = (int32_t)(sum / (int64_t)kFilterDepth);
    }

    if (modelListener == 0)
    {
        return;
    }

    if (pdoReady)
    {
        modelListener->onMotionDataUpdated(
            positionFilteredValue_,
            modelGetVelocityActual(),
            modelGetTorqueActual());

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

int32_t Model::getPositionActualHw() const
{
    return modelGetPositionActualHw();
}

void Model::setRunEnable(uint8_t enable)
{
    modelSetRunEnable(enable);

    if (enable == 0U)
    {
        stopProgramSequence();
    }
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
    // Persist the raw hardware home offset so it survives power cycles.
    int32_t hwOffset = modelGetHomeOffset();
    (void)UiFlashStorage_SaveHome(hwOffset);
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
        value = accel;
    }
    else if (index == kParamDecTime)
    {
        int32_t decel = value;
        if (decel < 0)
        {
            decel = -decel;
        }
        value = decel;
    }
    else if (index == kParamUnitScale)
    {
        if (value <= 0)
        {
            value = 1;
        }
    }
    else if (index == kParamPositionGain)
    {
        int32_t gain = value;
        if (gain < 0)
        {
            gain = 0;
        }
        value = gain;
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

void Model::writeAllParametersToDrive()
{
    int32_t acceleration = parameterValues_[kParamAccTime];
    int32_t deceleration = parameterValues_[kParamDecTime];
    int32_t unitScale = parameterValues_[kParamUnitScale];
    int32_t homeOffset = parameterValues_[kParamHomeOffset];
    int32_t positionGain = parameterValues_[kParamPositionGain];

    if (acceleration < 0)
    {
        acceleration = -acceleration;
    }
    if (deceleration < 0)
    {
        deceleration = -deceleration;
    }
    if (unitScale <= 0)
    {
        unitScale = 1;
    }
    if (positionGain < 0)
    {
        positionGain = 0;
    }

    parameterValues_[kParamAccTime] = acceleration;
    parameterValues_[kParamDecTime] = deceleration;
    parameterValues_[kParamUnitScale] = unitScale;
    parameterValues_[kParamPositionGain] = positionGain;

    modelSetProfileAcceleration(acceleration);
    modelSetProfileDeceleration(deceleration);
    modelSetUnitScale(unitScale);
    modelSetHomeOffset(homeOffset);
    modelSetSoftwareLimitPlus(parameterValues_[kParamLimitPlus]);
    modelSetSoftwareLimitMinus(parameterValues_[kParamLimitMinus]);
    modelSetPositionGain(positionGain);
}

void Model::requestReadAllParametersFromDrive()
{
    modelRequestParameterReadAll();
}

bool Model::fetchReadAllParametersFromDrive()
{
    int32_t values[kParameterReadAllValueCount] = {0};
    if (modelFetchParameterReadAll(values, kParameterReadAllValueCount) == 0U)
    {
        return false;
    }

    parameterValues_[kParamAccTime] = values[1];
    parameterValues_[kParamDecTime] = values[2];
    parameterValues_[kParamLimitPlus] = values[3];
    parameterValues_[kParamLimitMinus] = values[4];
    parameterValues_[kParamUnitScale] = values[5];
    parameterValues_[kParamHomeOffset] = values[6];
    parameterValues_[kParamPositionGain] = values[7];
    return true;
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

bool Model::startProgramSequence()
{
    if (programSequenceState_ != kProgramSeqIdle)
    {
        return false;
    }

    if (modelGetPdoReady() == 0U)
    {
        return false;
    }

    if (modelGetRunEnable() == 0U)
    {
        return false;
    }

    const uint16_t statusword = modelGetStatusword();
    const bool opEnabled = ((statusword & 0x006FU) == 0x0027U);
    if (!opEnabled)
    {
        return false;
    }

    for (uint8_t index = 0U; index < 3U; index++)
    {
        int32_t speedAbs = programValues_[kProgramIdxTargetSpeed1 + index];
        if (speedAbs < 0)
        {
            speedAbs = -speedAbs;
        }
        if (speedAbs <= 0)
        {
            speedAbs = 1;
        }

        int32_t torqueAbs = programValues_[kProgramIdxTargetTorque1 + index];
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

        programStepPositions_[index] = programValues_[kProgramIdxTargetPos1 + index];
        programStepSpeeds_[index] = speedAbs;
        programStepTorques_[index] = static_cast<uint16_t>(torqueAbs);
    }

    const int32_t delayValue = programValues_[kProgramIdxDelayMs];
    programDelayMs_ = (delayValue > 0) ? static_cast<uint32_t>(delayValue) : 0U;

    // Capture origin in both user and HW units to avoid integer truncation
    // on the user->HW round-trip (scale division loses remainder).
    programOriginPosition_ = modelGetPositionActual();
    programOriginPositionHw_ = modelGetPositionActualHw();
    programDelayStartMs_ = 0U;

    beginProgramMoveStep(0U);
    return true;
}

void Model::stopProgramSequence()
{
    if (programSequenceState_ == kProgramSeqIdle)
    {
        return;
    }

    // Stop with current position (using user units)
    modelSetTargetPositionAbs(modelGetPositionActual());
    programSequenceState_ = kProgramSeqIdle;
    programDelayStartMs_ = 0U;
}

bool Model::isProgramSequenceRunning() const
{
    return (programSequenceState_ != kProgramSeqIdle);
}

void Model::beginProgramMoveStep(uint8_t stepIndex)
{
    if (stepIndex >= 3U)
    {
        return;
    }

    modelSetProfileVelocity(programStepSpeeds_[stepIndex]);
    modelSetTorqueLimitPercent(programStepTorques_[stepIndex]);

    activeProgramTargetPosition_ = programStepPositions_[stepIndex];
    modelSetTargetPositionAbs(activeProgramTargetPosition_);

    if (stepIndex == 0U)
    {
        programSequenceState_ = kProgramSeqMoveStep1;
    }
    else if (stepIndex == 1U)
    {
        programSequenceState_ = kProgramSeqMoveStep2;
    }
    else
    {
        programSequenceState_ = kProgramSeqMoveStep3;
    }
}

bool Model::isTargetReached(int32_t target) const
{
    const int64_t delta = static_cast<int64_t>(modelGetPositionActual()) - static_cast<int64_t>(target);
    const int64_t absDelta = (delta < 0) ? -delta : delta;
    return (absDelta <= static_cast<int64_t>(PROGRAM_SEQUENCE_POSITION_TOLERANCE));
}

bool Model::isHardwareTargetReached(int32_t targetHw, int32_t toleranceHw) const
{
    int32_t toleranceAbs = toleranceHw;
    if (toleranceAbs < 0)
    {
        toleranceAbs = -toleranceAbs;
    }

    const int64_t delta = static_cast<int64_t>(modelGetPositionActualHw()) - static_cast<int64_t>(targetHw);
    const int64_t absDelta = (delta < 0) ? -delta : delta;
    return (absDelta <= static_cast<int64_t>(toleranceAbs));
}

void Model::tickProgramSequence()
{
    if (programSequenceState_ == kProgramSeqIdle)
    {
        return;
    }

    if (programSequenceState_ == kProgramSeqMoveStep1)
    {
        if (isTargetReached(activeProgramTargetPosition_))
        {
            beginProgramMoveStep(1U);
        }
        return;
    }

    if (programSequenceState_ == kProgramSeqMoveStep2)
    {
        if (isTargetReached(activeProgramTargetPosition_))
        {
            beginProgramMoveStep(2U);
        }
        return;
    }

    if (programSequenceState_ == kProgramSeqMoveStep3)
    {
        if (isTargetReached(activeProgramTargetPosition_))
        {
            programDelayStartMs_ = modelGetSystemMs();
            programSequenceState_ = kProgramSeqDelayBeforeReturn;
        }
        return;
    }

    if (programSequenceState_ == kProgramSeqDelayBeforeReturn)
    {
        const uint32_t elapsed = modelGetSystemMs() - programDelayStartMs_;
        if (elapsed >= programDelayMs_)
        {
            int32_t returnSpeedAbs = programValues_[kProgramIdxReturnSpeed];
            if (returnSpeedAbs < 0)
            {
                returnSpeedAbs = -returnSpeedAbs;
            }
            if (returnSpeedAbs <= 0)
            {
                returnSpeedAbs = 1;
            }

            modelSetProfileVelocity(returnSpeedAbs);
            modelSetTorqueLimitPercent(programStepTorques_[2]);
            activeProgramTargetPosition_ = programOriginPosition_;
            modelSetTargetPositionAbsHw(programOriginPositionHw_);
            programSequenceState_ = kProgramSeqReturnToOrigin;
        }
        return;
    }

    if (programSequenceState_ == kProgramSeqReturnToOrigin)
    {
        // Use tight HW-unit tolerance so the motor has truly converged
        // before the cycle ends.  This prevents drift when the next cycle
        // re-captures the origin from the current (not-yet-settled) position.
        if (isHardwareTargetReached(programOriginPositionHw_,
                                    PROGRAM_RETURN_POSITION_TOLERANCE_HW))
        {
            programSequenceState_ = kProgramSeqIdle;
            programDelayStartMs_ = 0U;
        }
    }
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
    setParameterValue(kParamPositionGain, savedState.parameterValues[kParamPositionGain]);

    for (uint8_t index = 0U; index < kProgramValueCount; index++)
    {
        setProgramValue(index, savedState.programValues[index]);
    }

    suppressPersistence_ = false;
    persistentDirty_ = false;

    return true;
}
