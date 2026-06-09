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
        kParamPositionGain,
        kParamCount,
        kProgramValueCount = 11
    };

    enum
    {
        kProgramIdxTargetPos1 = 0,
        kProgramIdxTargetPos2,
        kProgramIdxTargetPos3,
        kProgramIdxTargetSpeed1,
        kProgramIdxTargetSpeed2,
        kProgramIdxTargetSpeed3,
        kProgramIdxTargetTorque1,
        kProgramIdxTargetTorque2,
        kProgramIdxTargetTorque3,
        kProgramIdxReturnSpeed,
        kProgramIdxDelayMs
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

    // View→Model 커맨드 (JOG 속도 Parameter 값 적용)
    void applyJogSpeedToSoem();

    // Jog step counts 영구 저장 (페이지 전환 시에도 유지)
    void setJogStepCounts(int32_t counts);
    int32_t getJogStepCounts() const;

    // Run enable 상태 캐시 (페이지 전환 시에도 유지)
    uint8_t getRunEnable() const;
    int32_t getPositionActual() const;
    int32_t getPositionActualHw() const;

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
    void writeAllParametersToDrive();
    void writeAllParametersToDriveAndSaveHome();
    void requestReadAllParametersFromDrive();
    bool fetchReadAllParametersFromDrive();

    // ProgramPage 설정값 캐시
    void setProgramValue(uint8_t index, int32_t value);
    int32_t getProgramValue(uint8_t index) const;

    // ProgramPage 실행 시퀀스 제어
    bool startProgramSequence();
    void stopProgramSequence();
    bool isProgramSequenceRunning() const;

    // Explicit persistence commit. Settings are only written to flash when this is called.
    void commitPersistentState();

    bool saveProgramPageToUiFlash();
    bool loadProgramPageFromUiFlash();
    bool saveManualPageToUiFlash();
    bool loadManualPageFromUiFlash();
    bool saveParameterPageToUiFlash();
    bool loadParameterPageFromUiFlash();

protected:
    enum ProgramSequenceState
    {
        kProgramSeqIdle = 0,
        kProgramSeqMoveStep1,
        kProgramSeqMoveStep2,
        kProgramSeqMoveStep3,
        kProgramSeqDelayBeforeReturn,
        kProgramSeqReturnToOrigin
    };

    void beginProgramMoveStep(uint8_t stepIndex);
    void tickProgramSequence();
    bool isTargetReached(int32_t target) const;
    bool isHardwareTargetReached(int32_t targetHw, int32_t toleranceHw) const;
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
    ProgramSequenceState programSequenceState_;
    int32_t programStepPositions_[3];
    int32_t programStepSpeeds_[3];
    uint16_t programStepTorques_[3];
    int32_t programOriginPosition_;
    int32_t programOriginPositionHw_;
    int32_t activeProgramTargetPosition_;
    uint32_t programDelayMs_;
    uint32_t programDelayStartMs_;
    bool suppressPersistence_;
    bool persistentDirty_;

    // Position noise filter (2-point moving average - reduced latency)
    static constexpr int kFilterDepth = 2;
    int32_t positionFilterBuffer_[kFilterDepth];
    int32_t positionFilteredValue_;
    uint8_t positionFilterIndex_;
};

#endif // MODEL_HPP
