namespace UartWeb.Models;

public sealed class ConnectRequest
{
    public string PortName { get; set; } = string.Empty;
    public int BaudRate { get; set; } = 115200;
}

public sealed class CommandRequest
{
    public string Command { get; set; } = string.Empty;
}

public sealed class TelemetrySnapshot
{
    public DateTime TimestampUtc { get; set; } = DateTime.UtcNow;
    public double Position { get; set; }
    public double PositionHw { get; set; }
    public double Velocity { get; set; }
    public double Torque { get; set; }
    public string Status { get; set; } = "-";
    public bool? RunEnabled { get; set; }
}

public sealed class ManualSettings
{
    public int Position { get; set; }
    public int Speed { get; set; } = 100;
    public int Torque { get; set; } = 20;
    public bool AbsMode { get; set; } = true;
}

public sealed class ParameterSettings
{
    public int JogSpeed { get; set; } = 1000;
    public int Acc { get; set; } = 100;
    public int Dec { get; set; } = 100;
    public int LimitPlus { get; set; } = 100000;
    public int LimitMinus { get; set; } = -100000;
    public int GearRatio { get; set; } = 1;
    public int HomeOffset { get; set; }
    public int PositionGain { get; set; }
    public int BallLeadMm { get; set; } = 1;
    public int EncRes { get; set; } = 1;
}

public sealed class ProgramSettings
{
    public int Pos1 { get; set; }
    public int Pos2 { get; set; }
    public int Pos3 { get; set; }
    public int Speed1 { get; set; } = 100;
    public int Speed2 { get; set; } = 100;
    public int Speed3 { get; set; } = 100;
    public int Torque1 { get; set; } = 20;
    public int Torque2 { get; set; } = 20;
    public int Torque3 { get; set; } = 20;
    public int ReturnSpeed { get; set; } = 100;
    public int Delay1Ms { get; set; }
    public int Delay2Ms { get; set; }
    public int Delay3Ms { get; set; }
    public int RepeatCount { get; set; } = 1;
    public int InterCycleDelayMs { get; set; }
}

public sealed class ProgramProgressSnapshot
{
    public int Current { get; set; }  /* 현재 실행 중인 사이클 번호 (1-based), 0=정지 */
    public int Total   { get; set; }  /* 총 반복 횟수, 0=정지 */
}

public sealed class SettingsSnapshot
{
    public DateTime TimestampUtc { get; set; } = DateTime.UtcNow;
    public ManualSettings Manual { get; set; } = new();
    public ParameterSettings Parameter { get; set; } = new();
    public ProgramSettings Program { get; set; } = new();
}

public sealed class PressStatusSnapshot
{
    public DateTime TimestampUtc { get; set; } = DateTime.MinValue;
    public string Mode { get; set; } = "MANUAL";
    public string Step { get; set; } = "IDLE";
    public bool EstopOk { get; set; } = true;
    public bool DoorOk { get; set; } = true;
    public bool DriveReady { get; set; }
    public bool HomeComplete { get; set; }
}

public sealed class ProductionCounterSnapshot
{
    public DateTime TimestampUtc { get; set; } = DateTime.MinValue;
    public long Total { get; set; }
    public long Ok { get; set; }
    public long Ng { get; set; }
    public long ConsecutiveNg { get; set; }
    public double NgRatePct { get; set; }
}

public sealed class CycleResultSnapshot
{
    public DateTime TimestampUtc { get; set; } = DateTime.MinValue;
    public long CycleNumber { get; set; }
    public string Result { get; set; } = "-";
    public int PeakForcePct { get; set; }
    public long EndPosition { get; set; }
    public long CycleTimeMs { get; set; }
}

public sealed class AlarmSnapshot
{
    public DateTime TimestampUtc { get; set; } = DateTime.MinValue;
    public int Code { get; set; }
    public string Message { get; set; } = "NONE";
    public bool Acked { get; set; }
}

public sealed class MaintCounterSnapshot
{
    public DateTime TimestampUtc { get; set; } = DateTime.MinValue;
    public long TotalCycles { get; set; }
    public long SinceLastMaint { get; set; }
    public long PmThreshold { get; set; } = 100000;
    public bool PmAlert { get; set; }
}

public sealed class UserLevelResponse
{
    public int Level { get; set; }
    public string Name { get; set; } = "OPERATOR";
}

public sealed class AlarmHistoryEntry
{
    public DateTime TimestampUtc { get; set; } = DateTime.MinValue;
    public int Code { get; set; }
    public string Message { get; set; } = "NONE";
    public bool Acked { get; set; }
    public uint OccurredMs { get; set; }
}

public sealed class HistoryResponse
{
    public int Total { get; set; }
    public CycleResultSnapshot[] Records { get; set; } = [];
}

public sealed class UartStatusResponse
{
    public bool Connected { get; set; }
    public string PortName { get; set; } = string.Empty;
    public int BaudRate { get; set; }
    public TelemetrySnapshot Telemetry { get; set; } = new();
    public SettingsSnapshot Settings { get; set; } = new();
    public PressStatusSnapshot PressStatus { get; set; } = new();
    public ProductionCounterSnapshot Counter { get; set; } = new();
    public CycleResultSnapshot LastResult { get; set; } = new();
    public AlarmSnapshot ActiveAlarm { get; set; } = new();
    public string[] Logs { get; set; } = [];
    public MaintCounterSnapshot MaintCounter { get; set; } = new();
    public UserLevelResponse UserLevel { get; set; } = new();
    public ProgramProgressSnapshot ProgramProgress { get; set; } = new();
}

/* ── Press graph (high-res, STM32-sampled) ── */
public sealed class GraphSamplePoint
{
    public int Pos      { get; set; }  /* mm */
    public int Torque   { get; set; }  /* 0.1% units */
    public int Velocity { get; set; }  /* user unit/s */
    public int Ms       { get; set; }  /* elapsed ms since cycle start */
    public int Step     { get; set; }  /* PressState_t: 1=APPROACH 2=CONTACT 3=PRESS 4=DWELL 5=RETURN */
}

public sealed class GraphDataResponse
{
    public bool   Ready       { get; set; }
    public long   CycleNumber { get; set; }
    public int    Count       { get; set; }
    public GraphSamplePoint[] Points { get; set; } = [];
}

/* ── Cycle graph DB records ── */
public sealed class CycleDbRecord
{
    public long   Id            { get; set; }
    public long   CycleNo       { get; set; }
    public string Timestamp     { get; set; } = string.Empty;
    public string Result        { get; set; } = "-";
    public int    PeakForce     { get; set; }  /* 0.1% units */
    public long   EndPos        { get; set; }  /* mm */
    public long   CycleMs       { get; set; }
    public int    PointCount    { get; set; }
    public int    TorqueAvg     { get; set; }  /* 0.1% units, average over cycle */
    public string AnomalyType   { get; set; } = "none";  /* ok | peak_high | peak_low | profile | none */
    public double AnomalyScore  { get; set; }
}

public sealed class CycleDbListResponse
{
    public long            Total   { get; set; }
    public CycleDbRecord[] Records { get; set; } = [];
}

public sealed class CycleGraphResponse
{
    public CycleDbRecord?    Cycle  { get; set; }
    public GraphSamplePoint[] Points { get; set; } = [];
}

/* ── Golden Signature Baseline ── */
public sealed class BaselineProfile
{
    public long     Id          { get; set; }
    public string   CreatedAt   { get; set; } = string.Empty;
    public int      SampleCount { get; set; }
    public int      PosMin      { get; set; }
    public int      PosMax      { get; set; }
    public int      Resolution  { get; set; }  /* mm per grid point */
    public double[] TorqueAvg   { get; set; } = [];  /* 0.1% units */
    public double[] TorqueStd   { get; set; } = [];
}
