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
    public int UnitScale { get; set; } = 1;
    public int HomeOffset { get; set; }
    public int PositionGain { get; set; }
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
    public int DelayMs { get; set; }
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
}
