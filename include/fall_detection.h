#ifndef FALL_DETECTION_H
#define FALL_DETECTION_H

#include <Arduino.h>
#include <Wire.h>
#include <math.h>
#include <stdint.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

#include "I2Cdev.h"
#include "MPU6050_6Axis_MotionApps20.h"
#include "fall_model.h"

class FallDetection {
public:
    FallDetection();

    bool begin();
    void calibrate();
    void readSensorData();

    void startMonitoring();
    void stopMonitoring();
    void update();

    // 仅用于 record 观测：窗口就绪时运行一次 CNN，并返回最近概率。
    float serviceCnnOnce();

    int getLastAiDirection() const;
    float getLastAiDirectionConfidence() const;
    float getFallProbability() const;
    unsigned long getLastAiLatencyUs() const;
    float getFallEventAiProbability() const;
    int getFallEventAiDirection() const;
    unsigned long getFallEventAiTime() const;
    bool isOrientationAbnormal() const;

    bool getLatestMotionG(
        float& ax, float& ay, float& az,
        float& gx, float& gy, float& gz
    ) const;

    bool getLatestMagnitudes(
        float& accMagG,
        float& gyroMagDps
    ) const;

    // 回放验证：输入一个已归一化的 200×6 窗口，不改变正常监测状态。
    float runCnnExternal(const float* data, int count, int& direction, float& directionConfidence);

    bool isMonitoringActive() const;
    bool isFallenState() const;
    unsigned long getFallDuration() const;
    int getLongLieLevel() const;
    unsigned long getSedentarySeconds() const;
    bool consumeSedentaryReminder();

    bool getCurrentData(
        Quaternion& q,
        VectorFloat& gravity,
        float (&ypr)[3]
    );

    // 供原始CSV录制使用：取最近一次采样的raw六轴和64位时间戳。
    bool getLatestRawSample(
        uint32_t& sequence,
        int64_t& timeUs,
        int16_t& axRaw,
        int16_t& ayRaw,
        int16_t& azRaw,
        int16_t& gxRaw,
        int16_t& gyRaw,
        int16_t& gzRaw
    );

    using FallDetectionCallback =
        void (*)(const char* message);

    void setFallDetectionCallback(
        FallDetectionCallback callback
    ) {
        fallCallback = callback;
    }

    // 用Python导出的标准样本自检C++前向传播，排除传感器/实时数据干扰。
    void runSelfTest();

    // 板端阈值回放：不改阈值，只把原始CSV逐行喂给同一配对函数。
    void beginThresholdReplay();

    bool replayThresholdSample(
        uint32_t sampleSeq,
        int64_t sampleTimeUs,
        int16_t axRaw,
        int16_t ayRaw,
        int16_t azRaw,
        int16_t gxRaw,
        int16_t gyRaw,
        int16_t gzRaw,
        uint8_t& triggerMode,
        float& accMagG,
        float& gyroMagDps
    );

private:
    // ========================================================
    // 冻结的首次跌倒阈值规则
    // ========================================================

    static constexpr float ACC_THRESHOLD_G = 1.25f;
    static constexpr float GYRO_THRESHOLD_DPS = 310.0f;

    static constexpr uint32_t SAMPLE_PERIOD_MS = 5U;

    static constexpr int64_t PAIR_WINDOW_US = 20000LL;

    // 采样目标5ms；超过约6个周期才清除历史，避免100Hz采集时每帧误判为断档。
    static constexpr int64_t MAX_SAMPLE_GAP_US = 30000LL;

    // 传感器量程：加速度±16g（2048 LSB/g），陀螺仪±2000°/s。
    // 与SisFall的ADXL345 ±16g、THR5真机数据口径保持一致，避免跌倒冲击削顶。
    static constexpr float ACCEL_LSB_PER_G = 2048.0f;
    static constexpr float GYRO_LSB_PER_DPS = 16.384f;

    // 当前新MPU6050的六面实测零偏（raw counts）。
    // 更换模块后必须重新做六面测试并更新这三个值。
    static constexpr int32_t ACCEL_OFFSET_X = 5455;
    static constexpr int32_t ACCEL_OFFSET_Y = 871;
    static constexpr int32_t ACCEL_OFFSET_Z = -522;

    // 六轴软件姿态估计的互补滤波时间常数。
    static constexpr float ATTITUDE_FILTER_TAU_S = 0.50f;

    // ========================================================
    // 倒地状态维持和恢复
    // 姿态角不参与首次跌倒触发，只参与恢复判断。
    // ========================================================

    static constexpr float STABLE_ANGLE_THRESHOLD = 40.0f;

    // 跌倒触发后的冷静期：避免跌倒过程中角度短暂低于阈值就误恢复。
    static const unsigned long
        RECOVERY_GUARD_MS = 1500UL;

    // 佩戴方向：X+右、Y+下、Z+前。
    // 正常直立时重力沿 -Y，所以直立参考轴取 -Y。
    static constexpr float UPRIGHT_AXIS_Y_SIGN = -1.0f;

    static const unsigned long ALERT_INTERVAL = 30000UL;

    // 长躺分级：30s 提醒、60s 警告、5min 严重；临床“long lie”常指 >1h。
    static const unsigned long LONG_LIE_LEVEL1_MS = 30000UL;
    static const unsigned long LONG_LIE_LEVEL2_MS = 60000UL;
    static const unsigned long LONG_LIE_LEVEL3_MS = 300000UL;

    // 久坐/静止提醒：45 分钟；仅作低活动提醒，腰部 IMU 不能严格区分坐和站。
    static const unsigned long SEDENTARY_REMINDER_MS = 45UL * 60UL * 1000UL;
    static constexpr float SEDENTARY_ACC_TOL_G = 0.12f;
    static constexpr float SEDENTARY_GYRO_DPS = 15.0f;
    static constexpr float SEDENTARY_MAX_UPRIGHT_DEG = 55.0f;

    static const unsigned long
        RECOVERY_DURATION_THRESHOLD = 2000UL;

    // ========================================================
    // MPU6050与软件姿态快照
    // 不再调用DMP进行姿态计算。
    // ========================================================

    MPU6050 mpu;

    bool sensorReady;

    Quaternion q;
    VectorFloat gravity;
    float ypr[3];

    bool poseDataValid;

    uint32_t poseSequence;
    uint32_t lastProcessedPoseSequence;

    // ========================================================
    // 监测状态与报警状态
    // ========================================================

    bool isMonitoring;
    bool fallenState;

    // 仅在非跌倒状态下检测佩戴方向异常；不改变 A/G 判断。
    bool orientationAbnormal;
    unsigned long orientationAbnormalSince;
    unsigned long orientationNormalSince;

    unsigned long fallStartTime;
    unsigned long lastFallAlert;
    unsigned long stableStartTime;

    unsigned long sedentaryStartTime;
    bool sedentaryReminderPending;

    FallDetectionCallback fallCallback;

    // ========================================================
    // 线程同步
    // ========================================================

    SemaphoreHandle_t sensorMutex;
    SemaphoreHandle_t snapshotMutex;
    SemaphoreHandle_t stateMutex;

    TaskHandle_t samplingTaskHandle;

    bool sensorMaintenanceActive;
    bool clearThresholdHistoryRequested;

    static void samplingTaskEntry(void* parameter);

    void samplingTaskLoop();

    // ========================================================
    // 最新六轴数据快照
    // ========================================================

    int16_t latestAxRaw;
    int16_t latestAyRaw;
    int16_t latestAzRaw;

    int16_t latestGxRaw;
    int16_t latestGyRaw;
    int16_t latestGzRaw;

    float latestAccMagG;
    float latestGyroMagDps;

    int64_t latestSampleTimeUs;
    int64_t latestSampleIntervalUs;
    uint32_t latestSampleSequence;

    uint32_t sampleGapCount;
    uint32_t invalidSampleCount;

    // ========================================================
    // A/G事件配对状态
    // 采样任务和板端回放共用同一函数，避免两套语义。
    // ========================================================

    struct ThresholdPairState {
        int64_t lastAccelHitTimeUs = 0;
        int64_t lastGyroHitTimeUs = 0;

        bool hasAccelHit = false;
        bool hasGyroHit = false;
    };

    ThresholdPairState replayPairState;

    uint32_t replayLastSeq = 0;
    int64_t replayLastTimeUs = 0;
    bool replayHasLastSample = false;

    // 只记录、不参与报警：A/G触发后1.5秒内的相对重力夹角。
    bool probeActive = false;
    uint32_t probeSequence = 0;
    unsigned long probeStartMs = 0;
    float probeGravityX = 0.0f;
    float probeGravityY = 0.0f;
    float probeGravityZ = 0.0f;
    float probeMaxAngleDeg = 0.0f;
    float probeEndAngleDeg = 0.0f;

    // A/G触发后先进入疑似状态，等待重力方向确认。
    static const unsigned long CONFIRM_GRAVITY_DELAY_MS = 800UL;
    static constexpr float CONFIRM_GRAVITY_CHANGE_DEG = 30.0f;
    bool suspectActive = false;

    void resetThresholdPairState(
        ThresholdPairState& state
    );

    static bool evaluateThresholdPair(
        int64_t sampleTimeUs,
        bool accelHit,
        bool gyroHit,
        ThresholdPairState& state,
        uint8_t& mode
    );

    // ========================================================
    // CNN辅助推理
    // 保留现有模型的输入结构和推理接口。
    // CNN不参与首次跌倒报警。
    // ========================================================

    float windowBuffer[INPUT_TIMESTEPS * INPUT_FEATURES];

    int sampleCount;
    float fallProbability;
    bool aiWindowReady;

    int lastAiDirection;
    float lastAiDirectionConfidence;
    unsigned long lastAiLatencyUs;

    // 本次跌倒事件锁存值：窗口滑走后仍保留跌倒瞬间/过程中的最高 AI 证据。
    float fallEventAiProbability;
    int fallEventAiDirection;
    unsigned long fallEventAiTime;

    float runCnnInference();

    // ========================================================
    // 待处理报警事件
    // 采样任务只写事件，update()负责调用回调。
    // ========================================================

    bool pendingFallEvent;

    uint32_t triggerSequence;
    int64_t triggerTimeUs;

    float triggerAccMagG;
    float triggerGyroMagDps;

    // 0：同点双越阈
    // 1：角速度先出现，加速度后出现
    // 2：加速度先出现，角速度后出现
    uint8_t triggerMode;
};

#endif