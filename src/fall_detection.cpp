#include "fall_detection.h"
#include "test_samples.h"

#include <esp_timer.h>
#include <pgmspace.h>
#include <math.h>
#include <string.h>

// ============================================================
// CNN推理辅助函数
// 保留原有模型实现
// ============================================================

static float sigmoidf(float x) {
    return 1.0f / (1.0f + expf(-x));
}

static const char* aiDirectionName(int d) {
    switch (d) {
        case 0: return "前倒";
        case 1: return "后倒";
        case 2: return "侧倒";
        default: return "未知";
    }
}

static void conv1d(
    const float* input,
    int T_in,
    int Cin,
    const float* weights,
    const float* bias,
    int kernel,
    int Cout,
    float* output
) {
    int T_out = T_in - kernel + 1;

    for (int t = 0; t < T_out; t++) {
        for (int co = 0; co < Cout; co++) {
            float sum = pgm_read_float(&bias[co]);

            for (int k = 0; k < kernel; k++) {
                for (int ci = 0; ci < Cin; ci++) {
                    float w = pgm_read_float(
                        &weights[((k * Cin) + ci) * Cout + co]
                    );

                    sum += w * input[(t + k) * Cin + ci];
                }
            }

            output[t * Cout + co] =
                sum > 0.0f ? sum : 0.0f;
        }
    }
}

static void maxpool1d(
    const float* input,
    int T_in,
    int C,
    int pool,
    float* output
) {
    int T_out = T_in / pool;

    for (int t = 0; t < T_out; t++) {
        for (int c = 0; c < C; c++) {
            float m = input[(t * pool) * C + c];

            for (int p = 1; p < pool; p++) {
                float v = input[(t * pool + p) * C + c];

                if (v > m) {
                    m = v;
                }
            }

            output[t * C + c] = m;
        }
    }
}

static void dense(
    const float* input,
    int in_size,
    const float* weights,
    const float* bias,
    int out_size,
    float* output,
    bool relu
) {
    for (int o = 0; o < out_size; o++) {
        float sum = pgm_read_float(&bias[o]);

        for (int i = 0; i < in_size; i++) {
            sum += pgm_read_float(
                &weights[i * out_size + o]
            ) * input[i];
        }

        output[o] = relu
            ? (sum > 0.0f ? sum : 0.0f)
            : sum;
    }
}

#define CONV1_OUT_T (INPUT_TIMESTEPS - CONV1_KERNEL_SIZE + 1)
#define POOL1_T     (CONV1_OUT_T / 2)
#define CONV2_OUT_T (POOL1_T - CONV2_KERNEL_SIZE + 1)
#define POOL2_T     (CONV2_OUT_T / 2)

// ============================================================
// CNN固定推理缓冲区
// 不再每秒 new/delete，避免长时间运行后的堆碎片和空指针崩溃。
// 四块中间结果按峰值复用同一块arena：conv1+pool1 = 37,632字节。
// ============================================================
static constexpr int CONV1_SIZE  = CONV1_OUT_T * CONV1_OUT_CH;
static constexpr int POOL1_SIZE  = POOL1_T * CONV1_OUT_CH;
static constexpr int CONV2_SIZE  = CONV2_OUT_T * CONV2_OUT_CH;
static constexpr int FLATTEN_SIZE = POOL2_T * CONV2_OUT_CH;
static float cnnArena[CONV1_SIZE + POOL1_SIZE];

static constexpr float DEG_TO_RAD_F =
    0.01745329251994329577f;

static constexpr float RAD_TO_DEG_F =
    57.295779513082320876f;


// ============================================================
// FallDetection构造函数
// ============================================================

FallDetection::FallDetection()
    : mpu(0x68),
      sensorReady(false),
      poseDataValid(false),
      poseSequence(0),
      lastProcessedPoseSequence(0),
      isMonitoring(false),
      fallenState(false),
      orientationAbnormal(false),
      orientationAbnormalSince(0),
      orientationNormalSince(0),
      fallStartTime(0),
      lastFallAlert(0),
      stableStartTime(0),

      sedentaryStartTime(0),
      sedentaryReminderPending(false),
      fallCallback(nullptr),
      sensorMutex(nullptr),
      snapshotMutex(nullptr),
      stateMutex(nullptr),
      samplingTaskHandle(nullptr),
      sensorMaintenanceActive(false),
      clearThresholdHistoryRequested(false),
      latestAxRaw(0),
      latestAyRaw(0),
      latestAzRaw(0),
      latestGxRaw(0),
      latestGyRaw(0),
      latestGzRaw(0),
      latestAccMagG(0.0f),
      latestGyroMagDps(0.0f),
      latestSampleTimeUs(0),
      latestSampleIntervalUs(0),
      latestSampleSequence(0),
      sampleGapCount(0),
      invalidSampleCount(0),
      sampleCount(0),
      fallProbability(0.0f),
      aiWindowReady(false),
      lastAiDirection(3),
      lastAiDirectionConfidence(0.0f),
      lastAiLatencyUs(0),
      fallEventAiProbability(0.0f),
      fallEventAiDirection(3),
      fallEventAiTime(0),
      pendingFallEvent(false),
      triggerSequence(0),
      triggerTimeUs(0),
      triggerAccMagG(0.0f),
      triggerGyroMagDps(0.0f),
      triggerMode(0) {


    ypr[0] = 0.0f;
    ypr[1] = 0.0f;
    ypr[2] = 0.0f;

    q.w = 1.0f;
    q.x = 0.0f;
    q.y = 0.0f;
    q.z = 0.0f;

    gravity.x = 0.0f;
    gravity.y = 0.0f;
    gravity.z = 1.0f;
}


// ============================================================
// 初始化
// ============================================================

bool FallDetection::begin() {
    Serial.println("初始化MPU6050六轴采样模块...");

    if (sensorMutex == nullptr) {
        sensorMutex = xSemaphoreCreateMutex();
    }

    if (snapshotMutex == nullptr) {
        snapshotMutex = xSemaphoreCreateMutex();
    }

    if (stateMutex == nullptr) {
        stateMutex = xSemaphoreCreateMutex();
    }

    if (
        sensorMutex == nullptr ||
        snapshotMutex == nullptr ||
        stateMutex == nullptr
    ) {
        Serial.println("[阈值] 互斥锁创建失败");
        return false;
    }

    if (samplingTaskHandle != nullptr) {
        Serial.println("[阈值] 采样任务已存在");
        return true;
    }

    // I2C只在此处初始化一次。
    if (!Wire.begin(10, 11, 100000)) {
        Serial.println("[MPU6050] I2C初始化失败");
        return false;
    }

    delay(100);

    mpu.initialize();

    int retryCount = 0;
    const int maxRetries = 3;

    while (
        !mpu.testConnection() &&
        retryCount < maxRetries
    ) {
        Serial.printf(
            "MPU6050连接失败，重试 %d/%d...\n",
            retryCount + 1,
            maxRetries
        );

        delay(1000);
        retryCount++;
    }

    if (!mpu.testConnection()) {
        Serial.println("[MPU6050] 连接失败");
        return false;
    }

    // 明确设置量程，保证物理单位换算系数与配置一致。
    // ±16g：跌倒冲击可达3~6g，±2g会削顶；同时匹配SisFall的ADXL345口径。
    mpu.setFullScaleAccelRange(MPU6050_ACCEL_FS_16);
    mpu.setFullScaleGyroRange(MPU6050_GYRO_FS_2000);

    Serial.printf(
        "[MPU6050] 量程配置：ACC=±16g(%0.0f LSB/g), GYRO=±2000dps(%0.3f LSB/dps)\n",
        ACCEL_LSB_PER_G,
        GYRO_LSB_PER_DPS
    );
    Serial.printf(
        "[MPU6050] 回读 AFS_SEL=%d (0=±2g,1=±4g,2=±8g,3=±16g), GFS_SEL=%d\n",
        mpu.getFullScaleAccelRange(),
        mpu.getFullScaleGyroRange()
    );
    Serial.printf(
        "[规则] A>=%.2fg G>=%.0fdps W<=%lldus gap>%lldus\n",
        ACC_THRESHOLD_G,
        GYRO_THRESHOLD_DPS,
        (long long)PAIR_WINDOW_US,
        (long long)MAX_SAMPLE_GAP_US
    );

    // 关键：MPU6050寄存器在ESP32复位后不会自动清零。
    // 上一版CalibrateAccel写入的偏移会一直保留，这里显式清零，
    // 再在软件里扣除本模块六面实测零偏（getMotion6不会应用MPU内部offset）。
    mpu.setXAccelOffset(0);
    mpu.setYAccelOffset(0);
    mpu.setZAccelOffset(0);
    Serial.printf(
        "[MPU6050] 硬件偏移清零完成；软件零偏：X=%ld Y=%ld Z=%ld counts\n",
        (long)ACCEL_OFFSET_X,
        (long)ACCEL_OFFSET_Y,
        (long)ACCEL_OFFSET_Z
    );

    // 初始化完成、采样任务启动之前进行校准。
    calibrate();

    if (
        xSemaphoreTake(
            snapshotMutex,
            portMAX_DELAY
        ) == pdTRUE
    ) {
        poseDataValid = false;
        poseSequence = 0;
        lastProcessedPoseSequence = 0;

        latestSampleTimeUs = 0;
        latestSampleIntervalUs = 0;
        latestSampleSequence = 0;

        xSemaphoreGive(snapshotMutex);
    }

    if (
        xSemaphoreTake(
            stateMutex,
            portMAX_DELAY
        ) == pdTRUE
    ) {
        sampleGapCount = 0;
        invalidSampleCount = 0;

        isMonitoring = false;
        fallenState = false;

        fallEventAiProbability = 0.0f;
        fallEventAiDirection = 3;
        fallEventAiTime = 0;

        fallStartTime = 0;
        lastFallAlert = 0;
        stableStartTime = 0;
        orientationAbnormal = false;
        orientationAbnormalSince = 0;
        orientationNormalSince = 0;

        clearThresholdHistoryRequested = true;
        pendingFallEvent = false;
        suspectActive = false;
        probeActive = false;

        sampleCount = 0;
        aiWindowReady = false;
        fallProbability = 0.0f;
        lastAiDirection = 3;
        lastAiDirectionConfidence = 0.0f;
        fallEventAiProbability = 0.0f;
        fallEventAiDirection = 3;
        fallEventAiTime = 0;

        xSemaphoreGive(stateMutex);
    }

    sensorReady = true;

    BaseType_t taskResult = xTaskCreatePinnedToCore(
        FallDetection::samplingTaskEntry,
        "FallThresholdSampler",
        6144,
        this,
        3,
        &samplingTaskHandle,
        1
    );

    if (taskResult != pdPASS) {
        samplingTaskHandle = nullptr;
        sensorReady = false;

        Serial.println("[阈值] 采样任务创建失败");
        return false;
    }

    Serial.println(
        "[阈值] 六轴采样任务启动，目标周期5ms"
    );

    Serial.printf(
        "[阈值] 规则：A>=%.2fg，G>=%.0f°/s，时间差<=%lldus\n",
        ACC_THRESHOLD_G,
        GYRO_THRESHOLD_DPS,
        (long long)PAIR_WINDOW_US
    );

    return true;
}


// ============================================================
// 校准
// ============================================================

void FallDetection::calibrate() {
    if (stateMutex != nullptr) {
        if (
            xSemaphoreTake(
                stateMutex,
                portMAX_DELAY
            ) == pdTRUE
        ) {
            sensorMaintenanceActive = true;
            clearThresholdHistoryRequested = true;

            xSemaphoreGive(stateMutex);
        }
    }

    bool lockedSensor = false;

    if (sensorMutex != nullptr) {
        if (
            xSemaphoreTake(
                sensorMutex,
                portMAX_DELAY
            ) == pdTRUE
        ) {
            lockedSensor = true;
        }
    } else {
        // 仅在互斥锁尚未创建的初始化场景下允许直接操作。
        lockedSensor = true;
    }

    if (lockedSensor) {
        // 注意：MPU6050库的CalibrateAccel(6)在±16g下容易过冲。
        // 本次实机日志出现X=-3472/Y=1419/Z=4590的异常偏移，导致静止A=1.95g。
        // 先只做陀螺零偏；加速度零偏改用后续的静止均值校准，避免把量程/姿态带偏。
        mpu.CalibrateGyro(6);

        Serial.printf(
            "加速度偏移量(未启用): X=%d Y=%d Z=%d\n",
            mpu.getXAccelOffset(),
            mpu.getYAccelOffset(),
            mpu.getZAccelOffset()
        );

        if (sensorMutex != nullptr) {
            xSemaphoreGive(sensorMutex);
        }
    } else {
        Serial.println("[校准] 无法取得传感器锁");
    }

    if (stateMutex != nullptr) {
        if (
            xSemaphoreTake(
                stateMutex,
                portMAX_DELAY
            ) == pdTRUE
        ) {
            sensorMaintenanceActive = false;
            clearThresholdHistoryRequested = true;

            xSemaphoreGive(stateMutex);
        }
    }

    Serial.println("六轴传感器校准完成");
}


// ============================================================
// 读取最新传感器数据
// 不在主循环中再次读取I2C硬件。
// ============================================================

void FallDetection::readSensorData() {
    if (snapshotMutex == nullptr) {
        return;
    }

    int16_t ax, ay, az, gx, gy, gz;

    float accMag;
    float gyroMag;

    uint32_t seq;
    int64_t timeUs;

    if (
        xSemaphoreTake(
            snapshotMutex,
            pdMS_TO_TICKS(20)
        ) != pdTRUE
    ) {
        Serial.println("[传感器] 获取数据快照超时");
        return;
    }

    ax = latestAxRaw;
    ay = latestAyRaw;
    az = latestAzRaw;

    gx = latestGxRaw;
    gy = latestGyRaw;
    gz = latestGzRaw;

    accMag = latestAccMagG;
    gyroMag = latestGyroMagDps;

    seq = latestSampleSequence;
    timeUs = latestSampleTimeUs;

    xSemaphoreGive(snapshotMutex);

    Serial.printf(
        "[六轴] seq=%lu time_us=%lld "
        "acc=(%d,%d,%d) gyro=(%d,%d,%d) "
        "A=%.3fg G=%.2f°/s\n",
        static_cast<unsigned long>(seq),
        static_cast<long long>(timeUs),
        ax, ay, az,
        gx, gy, gz,
        accMag,
        gyroMag
    );
}


// ============================================================
// 监测开关
// ============================================================

void FallDetection::startMonitoring() {
    if (stateMutex == nullptr) {
        return;
    }

    if (
        xSemaphoreTake(
            stateMutex,
            portMAX_DELAY
        ) == pdTRUE
    ) {
        isMonitoring = true;

        clearThresholdHistoryRequested = true;

        if (!aiWindowReady) {
            sampleCount = 0;
        }

        xSemaphoreGive(stateMutex);
    }

    Serial.println("跌倒监测已启动");
}

void FallDetection::stopMonitoring() {
    if (stateMutex == nullptr) {
        return;
    }

    if (
        xSemaphoreTake(
            stateMutex,
            portMAX_DELAY
        ) == pdTRUE
    ) {
        isMonitoring = false;
        // 关闭监测时同步清除跌倒锁存，避免恢复逻辑被 isMonitoring 永久挡住。
        fallenState = false;
        suspectActive = false;
        probeActive = false;
        stableStartTime = 0;
        fallStartTime = 0;
        pendingFallEvent = false;
        fallEventAiProbability = 0.0f;
        fallEventAiDirection = 3;
        fallEventAiTime = 0;

        clearThresholdHistoryRequested = true;

        if (!aiWindowReady) {
            sampleCount = 0;
        }

        xSemaphoreGive(stateMutex);
    }

    Serial.println("跌倒监测已停止");
}


// ============================================================
// 状态查询
// ============================================================

bool FallDetection::isMonitoringActive() const {
    if (stateMutex == nullptr) {
        return false;
    }

    if (
        xSemaphoreTake(
            stateMutex,
            pdMS_TO_TICKS(20)
        ) != pdTRUE
    ) {
        return false;
    }

    const bool value = isMonitoring;

    xSemaphoreGive(stateMutex);

    return value;
}

bool FallDetection::isFallenState() const {
    if (stateMutex == nullptr) {
        return false;
    }

    if (
        xSemaphoreTake(
            stateMutex,
            pdMS_TO_TICKS(20)
        ) != pdTRUE
    ) {
        return false;
    }

    const bool value = fallenState;

    xSemaphoreGive(stateMutex);

    return value;
}

unsigned long FallDetection::getFallDuration() const {
    if (stateMutex == nullptr) {
        return 0;
    }

    if (
        xSemaphoreTake(
            stateMutex,
            pdMS_TO_TICKS(20)
        ) != pdTRUE
    ) {
        return 0;
    }

    const unsigned long duration =
        fallenState
            ? millis() - fallStartTime
            : 0;

    xSemaphoreGive(stateMutex);

    return duration;
}


int FallDetection::getLongLieLevel() const {
    const unsigned long duration = getFallDuration();

    if (duration >= LONG_LIE_LEVEL3_MS) {
        return 3;
    }
    if (duration >= LONG_LIE_LEVEL2_MS) {
        return 2;
    }
    if (duration >= LONG_LIE_LEVEL1_MS) {
        return 1;
    }
    return 0;
}


unsigned long FallDetection::getSedentarySeconds() const {
    if (stateMutex == nullptr || sedentaryStartTime == 0) {
        return 0;
    }
    return (millis() - sedentaryStartTime) / 1000UL;
}


bool FallDetection::consumeSedentaryReminder() {
    if (stateMutex == nullptr) {
        return false;
    }
    if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(20)) != pdTRUE) {
        return false;
    }
    const bool pending = sedentaryReminderPending;
    sedentaryReminderPending = false;
    xSemaphoreGive(stateMutex);
    return pending;
}


// ============================================================
// 获取姿态数据快照
// ypr[0]是无绝对参考的航向角占位值0；
// ypr[1]/ypr[2]分别为软件估计的俯仰角/横滚角，单位弧度。
// ============================================================

bool FallDetection::getCurrentData(
    Quaternion& outQ,
    VectorFloat& outGravity,
    float (&outYpr)[3]
) {
    if (
        !sensorReady ||
        snapshotMutex == nullptr
    ) {
        return false;
    }

    if (
        xSemaphoreTake(
            snapshotMutex,
            pdMS_TO_TICKS(20)
        ) != pdTRUE
    ) {
        return false;
    }

    if (!poseDataValid) {
        xSemaphoreGive(snapshotMutex);
        return false;
    }

    outQ = q;
    outGravity = gravity;

    outYpr[0] = ypr[0];
    outYpr[1] = ypr[1];
    outYpr[2] = ypr[2];

    xSemaphoreGive(snapshotMutex);

    return true;
}

bool FallDetection::getLatestRawSample(
    uint32_t& sequence,
    int64_t& timeUs,
    int16_t& axRaw,
    int16_t& ayRaw,
    int16_t& azRaw,
    int16_t& gxRaw,
    int16_t& gyRaw,
    int16_t& gzRaw
) {
    if (
        !sensorReady ||
        snapshotMutex == nullptr ||
        xSemaphoreTake(
            snapshotMutex,
            pdMS_TO_TICKS(20)
        ) != pdTRUE
    ) {
        return false;
    }

    if (latestSampleSequence == 0) {
        xSemaphoreGive(snapshotMutex);
        return false;
    }

    sequence = latestSampleSequence;
    timeUs = latestSampleTimeUs;

    axRaw = latestAxRaw;
    ayRaw = latestAyRaw;
    azRaw = latestAzRaw;

    gxRaw = latestGxRaw;
    gyRaw = latestGyRaw;
    gzRaw = latestGzRaw;

    xSemaphoreGive(snapshotMutex);

    return true;
}


// ============================================================
// 采样任务入口
// ============================================================

bool FallDetection::evaluateThresholdPair(
    int64_t sampleTimeUs,
    bool accelHit,
    bool gyroHit,
    ThresholdPairState& state,
    uint8_t& mode
) {
    bool trigger = false;
    mode = 0;

    // 同点双越阈：0ms，允许触发。
    if (accelHit && gyroHit) {
        trigger = true;
        mode = 0;
    }
    // 当前加速度越阈，与此前角速度事件配对。
    else if (
        accelHit &&
        state.hasGyroHit &&
        sampleTimeUs >= state.lastGyroHitTimeUs &&
        sampleTimeUs - state.lastGyroHitTimeUs <= PAIR_WINDOW_US
    ) {
        trigger = true;
        mode = 1;
    }
    // 当前角速度越阈，与此前加速度事件配对。
    else if (
        gyroHit &&
        state.hasAccelHit &&
        sampleTimeUs >= state.lastAccelHitTimeUs &&
        sampleTimeUs - state.lastAccelHitTimeUs <= PAIR_WINDOW_US
    ) {
        trigger = true;
        mode = 2;
    }

    // 每次仍在越阈范围内的有效采样点都会刷新时间，
    // 不采用只记录首次上升沿的方式。
    if (accelHit) {
        state.hasAccelHit = true;
        state.lastAccelHitTimeUs = sampleTimeUs;
    }

    if (gyroHit) {
        state.hasGyroHit = true;
        state.lastGyroHitTimeUs = sampleTimeUs;
    }

    return trigger;
}

void FallDetection::resetThresholdPairState(
    ThresholdPairState& state
) {
    state.lastAccelHitTimeUs = 0;
    state.lastGyroHitTimeUs = 0;
    state.hasAccelHit = false;
    state.hasGyroHit = false;
}

void FallDetection::beginThresholdReplay() {
    resetThresholdPairState(replayPairState);
    replayLastSeq = 0;
    replayLastTimeUs = 0;
    replayHasLastSample = false;
}

bool FallDetection::replayThresholdSample(
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
) {
    (void)sampleSeq;

    // 新试次从seq回退开始；序号缺口或时间断档时清空配对历史，
    // 与生产采样任务的gap处理保持一致。
    if (
        !replayHasLastSample ||
        sampleSeq <= replayLastSeq ||
        sampleTimeUs < replayLastTimeUs ||
        sampleTimeUs - replayLastTimeUs > MAX_SAMPLE_GAP_US
    ) {
        resetThresholdPairState(replayPairState);
    }

    replayHasLastSample = true;
    replayLastSeq = sampleSeq;
    replayLastTimeUs = sampleTimeUs;

    // 回放旧数据时故意不扣当前新模块的零偏：
    // 74/164组数据来自旧传感器，raw本身已经是旧口径。
    const float axG =
        static_cast<float>(axRaw) / ACCEL_LSB_PER_G;
    const float ayG =
        static_cast<float>(ayRaw) / ACCEL_LSB_PER_G;
    const float azG =
        static_cast<float>(azRaw) / ACCEL_LSB_PER_G;

    const float gxDps =
        static_cast<float>(gxRaw) / GYRO_LSB_PER_DPS;
    const float gyDps =
        static_cast<float>(gyRaw) / GYRO_LSB_PER_DPS;
    const float gzDps =
        static_cast<float>(gzRaw) / GYRO_LSB_PER_DPS;

    accMagG = sqrtf(axG * axG + ayG * ayG + azG * azG);
    gyroMagDps = sqrtf(gxDps * gxDps + gyDps * gyDps + gzDps * gzDps);

    const bool accelHit = accMagG >= ACC_THRESHOLD_G;
    const bool gyroHit = gyroMagDps >= GYRO_THRESHOLD_DPS;

    return evaluateThresholdPair(
        sampleTimeUs,
        accelHit,
        gyroHit,
        replayPairState,
        triggerMode
    );
}

void FallDetection::samplingTaskEntry(void* parameter) {
    FallDetection* self =
        static_cast<FallDetection*>(parameter);

    if (self != nullptr) {
        self->samplingTaskLoop();
    }

    vTaskDelete(nullptr);
}


// ============================================================
// 独立六轴采样与阈值配对
// ============================================================

void FallDetection::samplingTaskLoop() {
    TickType_t lastWakeTime = xTaskGetTickCount();

    TickType_t samplePeriodTicks =
        pdMS_TO_TICKS(SAMPLE_PERIOD_MS);

    if (samplePeriodTicks == 0) {
        samplePeriodTicks = 1;
    }

    uint32_t localSequence = 0;

    int64_t previousSampleTimeUs = 0;

    ThresholdPairState thresholdPairState;

    bool attitudeInitialized = false;

    float pitchRad = 0.0f;
    float rollRad = 0.0f;

    for (;;) {
        bool monitoring = false;
        bool maintenance = false;
        bool alreadyFallen = false;
        bool resetHistory = false;

        if (
            stateMutex == nullptr ||
            xSemaphoreTake(
                stateMutex,
                portMAX_DELAY
            ) != pdTRUE
        ) {
            vTaskDelayUntil(
                &lastWakeTime,
                samplePeriodTicks
            );
            continue;
        }

        monitoring = isMonitoring;
        maintenance = sensorMaintenanceActive;
        alreadyFallen = fallenState;

        resetHistory = clearThresholdHistoryRequested;
        clearThresholdHistoryRequested = false;

        xSemaphoreGive(stateMutex);

        // 采样任务心跳：每约5秒一次，用来判断任务是否还在跑。
        static uint32_t sampleHeartbeat = 0;
        if ((++sampleHeartbeat % 1000U) == 0U) {
            Serial.printf(
                "[采样心跳] mon=%d maint=%d fallen=%d\n",
                monitoring ? 1 : 0,
                maintenance ? 1 : 0,
                alreadyFallen ? 1 : 0
            );
        }

        if (
            resetHistory ||
            !monitoring ||
            maintenance ||
            alreadyFallen
        ) {
            resetThresholdPairState(thresholdPairState);
        }

        if (!monitoring || maintenance) {
            previousSampleTimeUs = 0;
            attitudeInitialized = false;
            probeActive = false;
            probeEndAngleDeg = 0.0f;
            suspectActive = false;

            vTaskDelayUntil(
                &lastWakeTime,
                samplePeriodTicks
            );

            continue;
        }

        const uint32_t thisSequence =
            ++localSequence;

        int16_t ax = 0, ay = 0, az = 0;
        int16_t gx = 0, gy = 0, gz = 0;

        int64_t sampleTimeUs = 0;

        bool readOK = false;

        if (
            sensorMutex != nullptr &&
            xSemaphoreTake(
                sensorMutex,
                pdMS_TO_TICKS(2)
            ) == pdTRUE
        ) {
            mpu.getMotion6(
                &ax, &ay, &az,
                &gx, &gy, &gz
            );

            sampleTimeUs = esp_timer_get_time();

            xSemaphoreGive(sensorMutex);

            readOK = true;
        }

        if (!readOK) {
            static unsigned long lastSensorErrorLog = 0;
            const unsigned long sensorErrorNow = millis();
            if (sensorErrorNow - lastSensorErrorLog >= 5000UL) {
                lastSensorErrorLog = sensorErrorNow;
                Serial.println("[传感器] MPU6050 读取失败：检查VCC/GND/SDA/SCL");
            }
            resetThresholdPairState(thresholdPairState);

            previousSampleTimeUs = 0;
            attitudeInitialized = false;

            if (
                xSemaphoreTake(
                    stateMutex,
                    portMAX_DELAY
                ) == pdTRUE
            ) {
                ++sampleGapCount;

                if (!aiWindowReady) {
                    sampleCount = 0;
                }

                xSemaphoreGive(stateMutex);
            }

            vTaskDelayUntil(
                &lastWakeTime,
                samplePeriodTicks
            );

            continue;
        }

        // 全六轴读数同时为零属于可疑数据，不作为有效样本。
        if (
            ax == 0 && ay == 0 && az == 0 &&
            gx == 0 && gy == 0 && gz == 0
        ) {
            static unsigned long lastZeroSampleLog = 0;
            const unsigned long zeroNow = millis();
            if (zeroNow - lastZeroSampleLog >= 5000UL) {
                lastZeroSampleLog = zeroNow;
                Serial.println("[传感器] 六轴全为0：检查MPU6050供电/接线/地址");
            }
            resetThresholdPairState(thresholdPairState);

            previousSampleTimeUs = 0;
            attitudeInitialized = false;

            if (
                xSemaphoreTake(
                    stateMutex,
                    portMAX_DELAY
                ) == pdTRUE
            ) {
                ++sampleGapCount;
                ++invalidSampleCount;

                if (!aiWindowReady) {
                    sampleCount = 0;
                }

                xSemaphoreGive(stateMutex);
            }

            vTaskDelayUntil(
                &lastWakeTime,
                samplePeriodTicks
            );

            continue;
        }

        // ====================================================
        // 时间间隔与缺口检测
        // ====================================================

        bool sampleGap = false;

        int64_t sampleIntervalUs = 0;

        if (previousSampleTimeUs > 0) {
            const int64_t deltaUs =
                sampleTimeUs - previousSampleTimeUs;

            if (deltaUs <= 0) {
                resetThresholdPairState(thresholdPairState);

                previousSampleTimeUs = 0;
                attitudeInitialized = false;

                if (
                    xSemaphoreTake(
                        stateMutex,
                        portMAX_DELAY
                    ) == pdTRUE
                ) {
                    ++invalidSampleCount;

                    if (!aiWindowReady) {
                        sampleCount = 0;
                    }

                    xSemaphoreGive(stateMutex);
                }

                vTaskDelayUntil(
                    &lastWakeTime,
                    samplePeriodTicks
                );

                continue;
            }

            sampleIntervalUs = deltaUs;

            if (deltaUs >= MAX_SAMPLE_GAP_US) {
                sampleGap = true;

                resetThresholdPairState(thresholdPairState);

                attitudeInitialized = false;
            }
        }

        previousSampleTimeUs = sampleTimeUs;

        if (sampleGap) {
            if (
                xSemaphoreTake(
                    stateMutex,
                    portMAX_DELAY
                ) == pdTRUE
            ) {
                ++sampleGapCount;

                if (!aiWindowReady) {
                    sampleCount = 0;
                }

                xSemaphoreGive(stateMutex);
            }
        }

        // ====================================================
        // 物理单位换算
        // ====================================================

        // 软件扣除六面实测零偏；getMotion6()不会自动应用MPU内部offset。
        const int32_t axCorrected =
            static_cast<int32_t>(ax) - ACCEL_OFFSET_X;

        const int32_t ayCorrected =
            static_cast<int32_t>(ay) - ACCEL_OFFSET_Y;

        const int32_t azCorrected =
            static_cast<int32_t>(az) - ACCEL_OFFSET_Z;

        const float axG =
            static_cast<float>(axCorrected) / ACCEL_LSB_PER_G;

        const float ayG =
            static_cast<float>(ayCorrected) / ACCEL_LSB_PER_G;

        const float azG =
            static_cast<float>(azCorrected) / ACCEL_LSB_PER_G;

        const float gxDps =
            static_cast<float>(gx) / GYRO_LSB_PER_DPS;

        const float gyDps =
            static_cast<float>(gy) / GYRO_LSB_PER_DPS;

        const float gzDps =
            static_cast<float>(gz) / GYRO_LSB_PER_DPS;

        const float accMagG = sqrtf(
            axG * axG +
            ayG * ayG +
            azG * azG
        );

        const float gyroMagDps = sqrtf(
            gxDps * gxDps +
            gyDps * gyDps +
            gzDps * gzDps
        );

        if (
            !isfinite(accMagG) ||
            !isfinite(gyroMagDps)
        ) {
            resetThresholdPairState(thresholdPairState);

            previousSampleTimeUs = 0;
            attitudeInitialized = false;

            if (
                xSemaphoreTake(
                    stateMutex,
                    portMAX_DELAY
                ) == pdTRUE
            ) {
                ++invalidSampleCount;

                if (!aiWindowReady) {
                    sampleCount = 0;
                }

                xSemaphoreGive(stateMutex);
            }

            vTaskDelayUntil(
                &lastWakeTime,
                samplePeriodTicks
            );

            continue;
        }

        // ====================================================
        // 六轴软件姿态估计
        //
        // 加速度提供重力方向参考；陀螺仪用于短期角度积分。
        // 航向角没有磁力计参考，因此不在此估计绝对航向。
        // ====================================================

        const float rollAccRad =
            atan2f(ayG, azG);

        const float pitchAccRad =
            atan2f(
                -axG,
                sqrtf(ayG * ayG + azG * azG)
            );

        float dtSeconds = 0.0f;

        if (attitudeInitialized) {
            const int64_t deltaUs =
                sampleTimeUs -
                latestSampleTimeUs;

            if (
                deltaUs > 0 &&
                deltaUs < MAX_SAMPLE_GAP_US
            ) {
                dtSeconds =
                    static_cast<float>(deltaUs) / 1000000.0f;
            }
        }

        if (
            !attitudeInitialized ||
            dtSeconds <= 0.0f
        ) {
            pitchRad = pitchAccRad;
            rollRad = rollAccRad;

            attitudeInitialized = true;
        } else {
            const float alpha =
                ATTITUDE_FILTER_TAU_S /
                (ATTITUDE_FILTER_TAU_S + dtSeconds);

            const float gyroXRad =
                gxDps * DEG_TO_RAD_F;

            const float gyroYRad =
                gyDps * DEG_TO_RAD_F;

            rollRad =
                alpha * (rollRad + gyroXRad * dtSeconds) +
                (1.0f - alpha) * rollAccRad;

            pitchRad =
                alpha * (pitchRad + gyroYRad * dtSeconds) +
                (1.0f - alpha) * pitchAccRad;
        }

        // 构造仅包含俯仰、横滚的四元数。
        // 它用于兼容原来的数据接口，不表示绝对航向。
        const float halfRoll = rollRad * 0.5f;
        const float halfPitch = pitchRad * 0.5f;

        const float cr = cosf(halfRoll);
        const float sr = sinf(halfRoll);

        const float cp = cosf(halfPitch);
        const float sp = sinf(halfPitch);

        Quaternion qLocal;

        qLocal.w = cr * cp;
        qLocal.x = sr * cp;
        qLocal.y = cr * sp;
        qLocal.z = -sr * sp;

        VectorFloat gravityLocal;

        gravityLocal.x =
            2.0f * (
                qLocal.x * qLocal.z -
                qLocal.w * qLocal.y
            );

        gravityLocal.y =
            2.0f * (
                qLocal.w * qLocal.x +
                qLocal.y * qLocal.z
            );

        gravityLocal.z =
            qLocal.w * qLocal.w -
            qLocal.x * qLocal.x -
            qLocal.y * qLocal.y +
            qLocal.z * qLocal.z;

        // 只记录、不参与报警：触发后1.5秒内的相对重力夹角。
        if (probeActive) {
            const float norm0 = sqrtf(
                probeGravityX * probeGravityX +
                probeGravityY * probeGravityY +
                probeGravityZ * probeGravityZ
            );

            const float norm1 = sqrtf(
                gravityLocal.x * gravityLocal.x +
                gravityLocal.y * gravityLocal.y +
                gravityLocal.z * gravityLocal.z
            );

            if (norm0 > 0.001f && norm1 > 0.001f) {
                float c = (
                    probeGravityX * gravityLocal.x +
                    probeGravityY * gravityLocal.y +
                    probeGravityZ * gravityLocal.z
                ) / (norm0 * norm1);

                c = fmaxf(-1.0f, fminf(1.0f, c));

                const float angle =
                    acosf(c) * RAD_TO_DEG_F;

                if (angle > probeMaxAngleDeg) {
                    probeMaxAngleDeg = angle;
                }

                probeEndAngleDeg = angle;
            }

            if (millis() - probeStartMs >= CONFIRM_GRAVITY_DELAY_MS) {
                bool confirmed = false;

                if (
                    probeEndAngleDeg >= CONFIRM_GRAVITY_CHANGE_DEG &&
                    monitoring &&
                    !maintenance
                ) {
                    if (
                        xSemaphoreTake(stateMutex, portMAX_DELAY) == pdTRUE
                    ) {
                        if (!fallenState && suspectActive) {
                            fallenState = true;
                            suspectActive = false;

                            fallEventAiProbability = fallProbability;
                            fallEventAiDirection = lastAiDirection;
                            fallEventAiTime = millis();

                            fallStartTime = millis();
                            lastFallAlert = fallStartTime;
                            stableStartTime = 0;
                            pendingFallEvent = true;
                            confirmed = true;
                        }

                        xSemaphoreGive(stateMutex);
                    }
                } else if (
                    xSemaphoreTake(stateMutex, portMAX_DELAY) == pdTRUE
                ) {
                    suspectActive = false;
                    xSemaphoreGive(stateMutex);
                }

                Serial.printf(
                    "[确认] %s seq=%lu end_angle=%.1f° max_angle=%.1f° A=%.2fg G=%.1f°/s\n",
                    confirmed ? "跌倒已确认" : "判定为撞击/非跌倒",
                    (unsigned long)probeSequence,
                    probeEndAngleDeg,
                    probeMaxAngleDeg,
                    accMagG,
                    gyroMagDps
                );

                probeActive = false;
            }
        }

        // ====================================================
        // 写入最新数据快照
        // ====================================================

        if (
            snapshotMutex != nullptr &&
            xSemaphoreTake(
                snapshotMutex,
                portMAX_DELAY
            ) == pdTRUE
        ) {
            latestAxRaw = ax;
            latestAyRaw = ay;
            latestAzRaw = az;

            latestGxRaw = gx;
            latestGyRaw = gy;
            latestGzRaw = gz;

            latestAccMagG = accMagG;
            latestGyroMagDps = gyroMagDps;

            latestSampleTimeUs = sampleTimeUs;
            latestSampleIntervalUs = sampleIntervalUs;
            latestSampleSequence = thisSequence;

            q = qLocal;
            gravity = gravityLocal;

            // getCurrentData沿用原来的弧度约定。
            ypr[0] = 0.0f;
            ypr[1] = pitchRad;
            ypr[2] = rollRad;

            poseDataValid = true;

            poseSequence = thisSequence;

            xSemaphoreGive(snapshotMutex);
        }

        // ====================================================
        // CNN输入窗口
        // 保持原有SisFall等效缩放，不参与阈值触发。
        // ====================================================

        if (
            xSemaphoreTake(
                stateMutex,
                portMAX_DELAY
            ) == pdTRUE
        ) {
            if (
                isMonitoring &&
                !sensorMaintenanceActive &&
                !aiWindowReady
            ) {
                const float axG =
                    static_cast<float>(axCorrected) / 2048.0f;

                const float ayG =
                    static_cast<float>(ayCorrected) / 2048.0f;

                const float azG =
                    static_cast<float>(azCorrected) / 2048.0f;

                const float gxK =
                    static_cast<float>(gx) / 16384.0f;

                const float gyK =
                    static_cast<float>(gy) / 16384.0f;

                const float gzK =
                    static_cast<float>(gz) / 16384.0f;

                const int base =
                    sampleCount * INPUT_FEATURES;

                windowBuffer[base + 0] =
                    (axG - pgm_read_float(&model_mean[0])) /
                    pgm_read_float(&model_std[0]);

                windowBuffer[base + 1] =
                    (ayG - pgm_read_float(&model_mean[1])) /
                    pgm_read_float(&model_std[1]);

                windowBuffer[base + 2] =
                    (azG - pgm_read_float(&model_mean[2])) /
                    pgm_read_float(&model_std[2]);

                windowBuffer[base + 3] =
                    (gxK - pgm_read_float(&model_mean[3])) /
                    pgm_read_float(&model_std[3]);

                windowBuffer[base + 4] =
                    (gyK - pgm_read_float(&model_mean[4])) /
                    pgm_read_float(&model_std[4]);

                windowBuffer[base + 5] =
                    (gzK - pgm_read_float(&model_mean[5])) /
                    pgm_read_float(&model_std[5]);

                ++sampleCount;

                if (sampleCount >= INPUT_TIMESTEPS) {
                    sampleCount = 0;
                    aiWindowReady = true;
                }
            }

            xSemaphoreGive(stateMutex);
        }

        // ====================================================
        // 首次跌倒阈值规则
        // ====================================================

        if (alreadyFallen) {
            resetThresholdPairState(thresholdPairState);

            vTaskDelayUntil(
                &lastWakeTime,
                samplePeriodTicks
            );

            continue;
        }

        const bool accelHit =
            accMagG >= ACC_THRESHOLD_G;

        const bool gyroHit =
            gyroMagDps >= GYRO_THRESHOLD_DPS;

        uint8_t currentTriggerMode = 0;
        const bool trigger = evaluateThresholdPair(
            sampleTimeUs,
            accelHit,
            gyroHit,
            thresholdPairState,
            currentTriggerMode
        );

        if (trigger) {
            bool accepted = false;

            if (
                xSemaphoreTake(
                    stateMutex,
                    portMAX_DELAY
                ) == pdTRUE
            ) {
                if (
                    isMonitoring &&
                    !sensorMaintenanceActive &&
                    !fallenState &&
                    !suspectActive
                ) {
                    // 先进入疑似状态，0.8秒后由重力方向变化确认。
                    suspectActive = true;

                    triggerSequence = thisSequence;
                    triggerTimeUs = sampleTimeUs;

                    triggerAccMagG = accMagG;
                    triggerGyroMagDps = gyroMagDps;

                    triggerMode = currentTriggerMode;

                    accepted = true;
                }

                xSemaphoreGive(stateMutex);
            }

            if (accepted) {
                probeActive = true;
                probeSequence = thisSequence;
                probeStartMs = millis();
                probeMaxAngleDeg = 0.0f;
                probeEndAngleDeg = 0.0f;

                probeGravityX = gravityLocal.x;
                probeGravityY = gravityLocal.y;
                probeGravityZ = gravityLocal.z;

                resetThresholdPairState(thresholdPairState);
            }
        }

        vTaskDelayUntil(
            &lastWakeTime,
            samplePeriodTicks
        );
    }
}


// ============================================================
// CNN推理
// 保留原始实现
// ============================================================

float FallDetection::runCnnInference() {
    const uint32_t inferenceStartUs = micros();

    float* conv1_out = cnnArena;
    float* pool1_out = cnnArena + CONV1_SIZE;

    conv1d(
        windowBuffer,
        INPUT_TIMESTEPS,
        INPUT_FEATURES,
        conv1_weights,
        conv1_bias,
        CONV1_KERNEL_SIZE,
        CONV1_OUT_CH,
        conv1_out
    );

    maxpool1d(
        conv1_out,
        CONV1_OUT_T,
        CONV1_OUT_CH,
        2,
        pool1_out
    );

    float* conv2_out = cnnArena;

    conv1d(
        pool1_out,
        POOL1_T,
        CONV1_OUT_CH,
        conv2_weights,
        conv2_bias,
        CONV2_KERNEL_SIZE,
        CONV2_OUT_CH,
        conv2_out
    );

    float* pool2_out = cnnArena + CONV2_SIZE;

    maxpool1d(
        conv2_out,
        CONV2_OUT_T,
        CONV2_OUT_CH,
        2,
        pool2_out
    );

    float pooled[CONV2_OUT_CH];

    for (int c = 0; c < CONV2_OUT_CH; ++c) {
        float sum = 0.0f;

        for (int t = 0; t < POOL2_T; ++t) {
            sum += pool2_out[t * CONV2_OUT_CH + c];
        }

        pooled[c] = sum / static_cast<float>(POOL2_T);
    }

    float fallOut[1];
    float dirOut[4];

    dense(
        pooled,
        CONV2_OUT_CH,
        fc_fall_weights,
        fc_fall_bias,
        1,
        fallOut,
        false
    );

    dense(
        pooled,
        CONV2_OUT_CH,
        fc_dir_weights,
        fc_dir_bias,
        4,
        dirOut,
        false
    );

    // 方向 softmax
    float maxLogit = dirOut[0];
    for (int i = 1; i < 4; ++i) {
        if (dirOut[i] > maxLogit) {
            maxLogit = dirOut[i];
        }
    }

    float sumExp = 0.0f;
    float probs[4];

    for (int i = 0; i < 4; ++i) {
        probs[i] = expf(dirOut[i] - maxLogit);
        sumExp += probs[i];
    }

    int best = 0;
    for (int i = 1; i < 4; ++i) {
        if (probs[i] > probs[best]) {
            best = i;
        }
    }

    lastAiDirection = best;
    lastAiDirectionConfidence = probs[best] / sumExp;

    // 温度校准只作用于显示概率，不参与报警。
    lastAiLatencyUs = micros() - inferenceStartUs;
    return sigmoidf(fallOut[0] / MODEL_TEMPERATURE);
}


int FallDetection::getLastAiDirection() const {
    return lastAiDirection;
}


float FallDetection::getLastAiDirectionConfidence() const {
    return lastAiDirectionConfidence;
}


float FallDetection::getFallProbability() const {
    return fallProbability;
}


unsigned long FallDetection::getLastAiLatencyUs() const {
    return lastAiLatencyUs;
}


float FallDetection::getFallEventAiProbability() const {
    return fallEventAiProbability;
}


int FallDetection::getFallEventAiDirection() const {
    return fallEventAiDirection;
}


unsigned long FallDetection::getFallEventAiTime() const {
    return fallEventAiTime;
}


bool FallDetection::isOrientationAbnormal() const {
    return orientationAbnormal;
}


bool FallDetection::getLatestMagnitudes(
    float& accMagG,
    float& gyroMagDps
) const {
    if (stateMutex == nullptr) {
        return false;
    }

    if (xSemaphoreTake(stateMutex, portMAX_DELAY) != pdTRUE) {
        return false;
    }

    const bool ok = latestSampleSequence != 0;

    if (ok) {
        const float ax = static_cast<float>(latestAxRaw - ACCEL_OFFSET_X) / ACCEL_LSB_PER_G;
        const float ay = static_cast<float>(latestAyRaw - ACCEL_OFFSET_Y) / ACCEL_LSB_PER_G;
        const float az = static_cast<float>(latestAzRaw - ACCEL_OFFSET_Z) / ACCEL_LSB_PER_G;

        const float gx = static_cast<float>(latestGxRaw) / GYRO_LSB_PER_DPS;
        const float gy = static_cast<float>(latestGyRaw) / GYRO_LSB_PER_DPS;
        const float gz = static_cast<float>(latestGzRaw) / GYRO_LSB_PER_DPS;

        accMagG = sqrtf(ax * ax + ay * ay + az * az);
        gyroMagDps = sqrtf(gx * gx + gy * gy + gz * gz);
    }

    xSemaphoreGive(stateMutex);

    return ok;
}


bool FallDetection::getLatestMotionG(
    float& ax, float& ay, float& az,
    float& gx, float& gy, float& gz
) const {
    if (stateMutex == nullptr) {
        return false;
    }

    if (xSemaphoreTake(stateMutex, portMAX_DELAY) != pdTRUE) {
        return false;
    }

    const bool ok = latestSampleSequence != 0;

    if (ok) {
        ax = static_cast<float>(latestAxRaw - ACCEL_OFFSET_X) / ACCEL_LSB_PER_G;
        ay = static_cast<float>(latestAyRaw - ACCEL_OFFSET_Y) / ACCEL_LSB_PER_G;
        az = static_cast<float>(latestAzRaw - ACCEL_OFFSET_Z) / ACCEL_LSB_PER_G;

        gx = static_cast<float>(latestGxRaw) / GYRO_LSB_PER_DPS;
        gy = static_cast<float>(latestGyRaw) / GYRO_LSB_PER_DPS;
        gz = static_cast<float>(latestGzRaw) / GYRO_LSB_PER_DPS;
    }

    xSemaphoreGive(stateMutex);

    return ok;
}



float FallDetection::runCnnExternal(
    const float* data,
    int count,
    int& direction,
    float& directionConfidence
) {
    constexpr int expected = INPUT_TIMESTEPS * INPUT_FEATURES;

    if (data == nullptr || count != expected) {
        return -1.0f;
    }

    if (
        xSemaphoreTake(stateMutex, portMAX_DELAY) != pdTRUE
    ) {
        return -1.0f;
    }

    const float oldProb = fallProbability;
    const int oldDir = lastAiDirection;
    const float oldConf = lastAiDirectionConfidence;

    memcpy(windowBuffer, data, sizeof(float) * expected);

    const float p = runCnnInference();

    direction = lastAiDirection;
    directionConfidence = lastAiDirectionConfidence;

    // 回放不改变实时监测显示。
    fallProbability = oldProb;
    lastAiDirection = oldDir;
    lastAiDirectionConfidence = oldConf;

    xSemaphoreGive(stateMutex);

    return p;
}


float FallDetection::serviceCnnOnce() {
    bool ready = false;

    if (
        xSemaphoreTake(stateMutex, portMAX_DELAY) == pdTRUE
    ) {
        ready = aiWindowReady;
        xSemaphoreGive(stateMutex);
    }

    if (!ready) {
        return fallProbability;
    }

    const float result = runCnnInference();

    if (
        xSemaphoreTake(stateMutex, portMAX_DELAY) == pdTRUE
    ) {
        fallProbability = result;
        aiWindowReady = false;
        sampleCount = 0;

        if (fallenState && result > fallEventAiProbability) {
            fallEventAiProbability = result;
            fallEventAiDirection = lastAiDirection;
            fallEventAiTime = millis();
        }

        xSemaphoreGive(stateMutex);
    }

    return result;
}


// ============================================================
// CNN自检
// 用Python导出的标准样本验证C++前向传播，不经过传感器。
// ============================================================
void FallDetection::runSelfTest() {
    Serial.println("[CNN自检] 开始");

    // 参考值来自当前 include/test_samples.h 顶部注释（与当前 fall_model.h 配套）。
    // 旧报告的0.0126/0.9901是另一版权重，已不再适用。
    constexpr float EXPECTED_ADL = 0.01478174f;
    constexpr float EXPECTED_FALL = 0.98344547f;

    memcpy(
        windowBuffer,
        test_sample_adl_raw,
        sizeof(test_sample_adl_raw)
    );
    const float adlResult = runCnnInference();

    memcpy(
        windowBuffer,
        test_sample_fall_raw,
        sizeof(test_sample_fall_raw)
    );
    const float fallResult = runCnnInference();

    Serial.printf(
        "[CNN自检] ADL  = %.4f (参考%.4f, 差值%.4f)\n",
        adlResult,
        EXPECTED_ADL,
        fabsf(adlResult - EXPECTED_ADL)
    );
    Serial.printf(
        "[CNN自检] FALL = %.4f (参考%.4f, 差值%.4f)\n",
        fallResult,
        EXPECTED_FALL,
        fabsf(fallResult - EXPECTED_FALL)
    );

    if (
        fabsf(adlResult - EXPECTED_ADL) < 0.02f &&
        fabsf(fallResult - EXPECTED_FALL) < 0.02f
    ) {
        Serial.println("[CNN自检] PASS：C++与test_samples.h一致");
    } else {
        Serial.println("[CNN自检] FAIL：检查归一化/权重布局");
    }

    sampleCount = 0;
    aiWindowReady = false;
}


// ============================================================
// 主循环更新
//
// 1. 取出采样任务产生的首次阈值事件。
// 2. 主循环调用报警回调，不在采样任务中操作网络。
// 3. 运行CNN辅助推理。
// 4. 处理软件姿态显示和恢复状态。
// 5. 不使用姿态角触发首次跌倒。
// ============================================================

void FallDetection::update() {
    if (!sensorReady) {
        return;
    }

    bool fireFallEvent = false;

    uint32_t savedTriggerSequence = 0;
    int64_t savedTriggerTimeUs = 0;

    float savedAccMagG = 0.0f;
    float savedGyroMagDps = 0.0f;

    uint8_t savedTriggerMode = 0;

    if (
        xSemaphoreTake(
            stateMutex,
            portMAX_DELAY
        ) == pdTRUE
    ) {
        fireFallEvent = pendingFallEvent;
        pendingFallEvent = false;

        savedTriggerSequence = triggerSequence;
        savedTriggerTimeUs = triggerTimeUs;

        savedAccMagG = triggerAccMagG;
        savedGyroMagDps = triggerGyroMagDps;

        savedTriggerMode = triggerMode;

        xSemaphoreGive(stateMutex);
    }

    // 首次报警先分发，避免CNN推理延迟报警回调。
    if (fireFallEvent) {
        Serial.println();
        Serial.println("\033[1;31m========================================\033[0m");
        Serial.println("\033[1;31m[跌倒] 检测到跌倒！进入报警状态\033[0m");
        Serial.printf(
            "\033[1;31m[跌倒] seq=%lu time_us=%lld A=%.3fg G=%.2f°/s mode=%u AI相似度=%.3f AI方向=%s（仅观察）\033[0m\n",
            static_cast<unsigned long>(savedTriggerSequence),
            static_cast<long long>(savedTriggerTimeUs),
            savedAccMagG,
            savedGyroMagDps,
            static_cast<unsigned int>(savedTriggerMode),
            fallProbability,
            (fallProbability >= 0.5f ? aiDirectionName(lastAiDirection) : "未知")
        );
        Serial.println("\033[1;31m========================================\033[0m");

        if (fallCallback != nullptr) {
            fallCallback(
                "老人跌倒警报！(加速度与角速度阈值触发)"
            );
        }
    }

    // ========================================================
    // CNN辅助推理
    // 当完整窗口准备好后才运行，推理期间采样任务不覆盖窗口。
    // ========================================================

    bool runAI = false;

    if (
        xSemaphoreTake(
            stateMutex,
            portMAX_DELAY
        ) == pdTRUE
    ) {
        runAI = aiWindowReady;

        xSemaphoreGive(stateMutex);
    }

    if (runAI) {
        const float result = runCnnInference();

        if (
            xSemaphoreTake(
                stateMutex,
                portMAX_DELAY
            ) == pdTRUE
        ) {
            fallProbability = result;
            aiWindowReady = false;
            sampleCount = 0;

            if (fallenState && result > fallEventAiProbability) {
                fallEventAiProbability = result;
                fallEventAiDirection = lastAiDirection;
                fallEventAiTime = millis();
            }

            xSemaphoreGive(stateMutex);
        }

        const char* dirText = (
            fallenState && result >= 0.5f
        ) ? aiDirectionName(lastAiDirection) : "未确认";

        static unsigned long lastAiPrintTime = 0;
        const unsigned long aiNow = millis();

        if (!orientationAbnormal && aiNow - lastAiPrintTime >= 15000UL) {
            Serial.printf(
                "[AI观察] 跌倒相似度=%.3f 方向=%s 置信度=%.3f 推理=%.1fms（仅观察，不参与报警）\n",
                result,
                dirText,
                lastAiDirectionConfidence,
                lastAiLatencyUs / 1000.0f
            );

            lastAiPrintTime = aiNow;
        }
    }

    // ========================================================
    // 获取最新软件姿态快照
    // ========================================================

    bool hasPose = false;

    uint32_t currentPoseSequence = 0;

    float currentPitchRad = 0.0f;
    float currentRollRad = 0.0f;

    int64_t currentIntervalUs = 0;
    float currentAccMag = 0.0f;
    float currentGyroMag = 0.0f;

    int16_t currentAx = 0;
    int16_t currentAy = 0;
    int16_t currentAz = 0;

    if (
        snapshotMutex != nullptr &&
        xSemaphoreTake(
            snapshotMutex,
            pdMS_TO_TICKS(20)
        ) == pdTRUE
    ) {
        hasPose = poseDataValid;

        currentPoseSequence = poseSequence;

        currentPitchRad = ypr[1];
        currentRollRad = ypr[2];

        currentIntervalUs = latestSampleIntervalUs;
        currentAccMag = latestAccMagG;
        currentGyroMag = latestGyroMagDps;

        currentAx = latestAxRaw;
        currentAy = latestAyRaw;
        currentAz = latestAzRaw;

        xSemaphoreGive(snapshotMutex);
    }

    if (
        !hasPose ||
        currentPoseSequence == lastProcessedPoseSequence
    ) {
        return;
    }

    lastProcessedPoseSequence =
        currentPoseSequence;

    const float currentPitch =
        currentPitchRad * RAD_TO_DEG_F;

    const float currentRoll =
        currentRollRad * RAD_TO_DEG_F;

    // 恢复判断不要直接用 Pitch/Roll：
    // 当前佩戴方向是 Y+ 向下，直立时 Roll 约 -78°，不是 0°。
    // 用“重力方向与直立轴(-Y)的夹角”判断，避免 ±90/180 回绕。
    const float currentAxG =
        static_cast<float>(currentAx - ACCEL_OFFSET_X) /
        ACCEL_LSB_PER_G;

    const float currentAyG =
        static_cast<float>(currentAy - ACCEL_OFFSET_Y) /
        ACCEL_LSB_PER_G;

    const float currentAzG =
        static_cast<float>(currentAz - ACCEL_OFFSET_Z) /
        ACCEL_LSB_PER_G;

    const float currentAccNorm = sqrtf(
        currentAxG * currentAxG +
        currentAyG * currentAyG +
        currentAzG * currentAzG
    );

    float uprightCos = 0.0f;

    if (currentAccNorm > 0.001f) {
        uprightCos =
            (UPRIGHT_AXIS_Y_SIGN * currentAyG) /
            currentAccNorm;
    }

    uprightCos = fmaxf(-1.0f, fminf(1.0f, uprightCos));

    const float uprightAngle =
        acosf(uprightCos) * RAD_TO_DEG_F;

    const unsigned long now = millis();

    // 久坐/静止提醒：腰部 IMU 只能判断“低活动 + 直立”，
    // 不能严格区分坐和站；因此文案使用“久坐/静止提醒”。
    const bool stillForSedentary =
        fabsf(currentAccMag - 1.0f) <= SEDENTARY_ACC_TOL_G &&
        currentGyroMag <= SEDENTARY_GYRO_DPS &&
        uprightAngle <= SEDENTARY_MAX_UPRIGHT_DEG;

    if (xSemaphoreTake(stateMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        if (!isMonitoring || fallenState) {
            sedentaryStartTime = 0;
            sedentaryReminderPending = false;
        } else if (stillForSedentary) {
            if (sedentaryStartTime == 0) {
                sedentaryStartTime = now;
            }
            if (now - sedentaryStartTime >= SEDENTARY_REMINDER_MS) {
                sedentaryReminderPending = true;
            }
        } else {
            sedentaryStartTime = 0;
            sedentaryReminderPending = false;
        }

        xSemaphoreGive(stateMutex);
    }

    // 仅做佩戴方向自检：不改变 A/G 报警。
    const bool orientationNowAbnormal =
        isMonitoring &&
        !fallenState &&
        currentGyroMag < 12.0f &&
        currentAyG > 0.35f &&
        uprightAngle > 120.0f;

    if (orientationNowAbnormal) {
        orientationNormalSince = 0;

        if (!orientationAbnormal) {
            if (orientationAbnormalSince == 0) {
                orientationAbnormalSince = now;
            } else if (now - orientationAbnormalSince >= 300UL) {
                orientationAbnormal = true;
                Serial.println(
                    "\033[1;33m[传感器] 方向异常：重力Y为正，疑似戴反/上下颠倒；"
                    "AI概率与方向暂停显示，A/G报警不受影响\033[0m"
                );
            }
        }
    } else {
        orientationAbnormalSince = 0;

        if (orientationAbnormal) {
            if (orientationNormalSince == 0) {
                orientationNormalSince = now;
            } else if (now - orientationNormalSince >= 800UL) {
                orientationAbnormal = false;
                orientationNormalSince = 0;
                Serial.println(
                    "\033[1;32m[传感器] 方向恢复正常\033[0m"
                );
            }
        } else {
            orientationNormalSince = 0;
        }
    }

    if (orientationAbnormal) {
        // 戴反时加速度方向已经不可信，AI 概率/方向不再作为参考。
        fallProbability = 0.0f;
        lastAiDirection = 3;
        lastAiDirectionConfidence = 0.0f;
    }

    static unsigned long lastPosePrintTime = 0;

    if (now - lastPosePrintTime >= 30000UL) {
        const bool stateFallen = isFallenState();
        const bool stateMonitoring = isMonitoringActive();

        Serial.printf(
            "[六轴] A=%.2fg G=%.1fdps fallen=%d mon=%d\n",
            currentAccMag,
            currentGyroMag,
            stateFallen ? 1 : 0,
            stateMonitoring ? 1 : 0
        );

        lastPosePrintTime = now;
    }

    bool continueAlert = false;
    bool recoveryEvent = false;

    if (
        xSemaphoreTake(
            stateMutex,
            portMAX_DELAY
        ) == pdTRUE
    ) {
        if (isMonitoring && fallenState) {
            const bool stableAngle =
                uprightAngle <= STABLE_ANGLE_THRESHOLD;

            const bool recoveryGuardElapsed =
                fallStartTime == 0 ||
                now - fallStartTime >= RECOVERY_GUARD_MS;

            if (stableAngle && recoveryGuardElapsed) {
                if (stableStartTime == 0) {
                    stableStartTime = now;

                    Serial.printf(
                        "检测到姿态恢复稳定，直立夹角=%.1f°，开始计时...\n",
                        uprightAngle
                    );
                } else if (
                    now - stableStartTime >=
                    RECOVERY_DURATION_THRESHOLD
                ) {
                    fallenState = false;

                    fallEventAiProbability = 0.0f;
                    fallEventAiDirection = 3;
                    fallEventAiTime = 0;

                    stableStartTime = 0;
                    fallStartTime = 0;

                    clearThresholdHistoryRequested = true;

                    recoveryEvent = true;
                }
            } else {
                stableStartTime = 0;

                if (
                    !stableAngle &&
                    now - lastFallAlert >= ALERT_INTERVAL
                ) {
                    lastFallAlert = now;
                    continueAlert = true;
                }
            }
        }

        xSemaphoreGive(stateMutex);
    }

    if (
        continueAlert &&
        fallCallback != nullptr
    ) {
        fallCallback(
            "老人持续处于倒地状态，请立即查看！"
        );
    }

    if (recoveryEvent) {
        Serial.println();
        Serial.println("\033[1;32m========================================\033[0m");
        Serial.println("\033[1;32m[恢复] 姿态恢复正常，解除跌倒状态\033[0m");
        Serial.println("\033[1;32m========================================\033[0m");

        if (fallCallback != nullptr) {
            fallCallback(
                "老人已恢复正常姿态"
            );
        }
    }
}