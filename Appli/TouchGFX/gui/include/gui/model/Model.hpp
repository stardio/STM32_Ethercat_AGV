#ifndef MODEL_HPP
#define MODEL_HPP

#include <stdint.h>

class ModelListener;

class Model
{
public:
    enum
    {
        kParamJogSpeed = 0,
        kParamAccTime,
        kParamDecTime,
        kParamLimitPlus,
        kParamLimitMinus,
        kParamUnitScale,
        kParamHomeOffset,
        kParamCount,
        kProgramValueCount = 10
    };

    Model();

    void bind(ModelListener* listener)
    {
        modelListener = listener;
    }

    void tick();

    // View→Model 커맨드 (서보 ON/OFF)
    void setRunEnable(uint8_t enable);

    // View→Model 커맨드 (조그)
    void sendPositionDelta(int32_t delta);

    // View→Model 커맨드 (절대 위치 목표)
    void setTargetPositionAbs(int32_t pos);

    // View→Model 커맨드 (홈 설정)
    void setHomePosition();

    // Jog step counts 영구 저장 (페이지 전환 시에도 유지)
    void setJogStepCounts(int32_t counts);
    int32_t getJogStepCounts() const;

    // Run enable 상태 캐시 (페이지 전환 시에도 유지)
    uint8_t getRunEnable() const;
    int32_t getPositionActual() const;

    // 1Cycle 설정값 캐시 (페이지 전환 시에도 유지)
    void setManualCyclePosition(int32_t position);
    int32_t getManualCyclePosition() const;
    void setManualCycleSpeed(int32_t speed);
    int32_t getManualCycleSpeed() const;
    void setManualCycleTorque(int16_t torque);
    int16_t getManualCycleTorque() const;
    void setManualCycleAbsMode(uint8_t absMode);
    uint8_t getManualCycleAbsMode() const;

    // ParameterPage 설정값 캐시 (페이지 전환 시에도 유지)
    void setParameterValue(uint8_t index, int32_t value);
    int32_t getParameterValue(uint8_t index) const;

    // ProgramPage 설정값 캐시
    void setProgramValue(uint8_t index, int32_t value);
    int32_t getProgramValue(uint8_t index) const;

    // Explicit persistence commit. Settings are only written to flash when this is called.
    void commitPersistentState();

    bool saveProgramPageToUiFlash();
    bool loadProgramPageFromUiFlash();
    bool saveManualPageToUiFlash();
    bool loadManualPageFromUiFlash();
    bool saveParameterPageToUiFlash();
    bool loadParameterPageFromUiFlash();

protected:
    void savePersistentState();
    bool loadPersistentState();
    void initializeParameterFlashDefaults();
    void markPersistentDirty();

    ModelListener* modelListener;
    int32_t jogStepCounts_;
    uint8_t runEnable_;
    int32_t manualCyclePosition_;
    int32_t manualCycleSpeed_;
    int16_t manualCycleTorque_;
    uint8_t manualCycleAbsMode_;
    int32_t parameterValues_[kParamCount];
    int32_t programValues_[kProgramValueCount];
    bool suppressPersistence_;
    bool persistentDirty_;
};

#endif // MODEL_HPP
