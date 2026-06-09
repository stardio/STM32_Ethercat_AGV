using System.Globalization;
using System.IO.Ports;
using System.Text;
using System.Text.Json;
using UartWeb.Models;

namespace UartWeb.Services;

public sealed class UartGateway : IDisposable
{
    private const int MaxLogs = 500;

    private readonly object _sync = new();
    private readonly StringBuilder _rxBuffer = new();
    private readonly Queue<string> _logs = new();

    private SerialPort? _serial;
    private bool _connected;
    private string _portName = string.Empty;
    private int _baudRate;

    private TelemetrySnapshot _telemetry = new();
    private SettingsSnapshot _settings = new();
    private PressStatusSnapshot _pressStatus = new();
    private ProductionCounterSnapshot _counter = new();
    private CycleResultSnapshot _lastResult = new();
    private AlarmSnapshot _activeAlarm = new();
    private readonly Queue<CycleResultSnapshot> _history = new();
    private readonly Queue<AlarmHistoryEntry> _alarmHistory = new();
    private MaintCounterSnapshot _maint = new();
    private UserLevelResponse _userLevel = new();
    private const int MaxHistory = 100;
    private const int MaxAlarmHistory = 50;

    public string[] GetPorts()
    {
        return SerialPort.GetPortNames()
            .OrderBy(p => p, StringComparer.OrdinalIgnoreCase)
            .ToArray();
    }

    public bool Connect(string portName, int baudRate, out string error)
    {
        lock (_sync)
        {
            error = string.Empty;

            try
            {
                DisconnectNoLock();

                var serial = new SerialPort(portName, baudRate, Parity.None, 8, StopBits.One)
                {
                    Handshake = Handshake.None,
                    NewLine = "\r\n",
                    Encoding = Encoding.ASCII,
                    ReadTimeout = 200,
                    WriteTimeout = 200,
                    DtrEnable = true,
                    RtsEnable = false
                };

                serial.DataReceived += OnDataReceived;
                serial.Open();

                _serial = serial;
                _connected = true;
                _portName = portName;
                _baudRate = baudRate;
                AddLog($"[APP] Connected to {portName} @ {baudRate}");
                return true;
            }
            catch (Exception ex)
            {
                error = ex.Message;
                AddLog($"[APP] Connect failed: {ex.Message}");
                return false;
            }
        }
    }

    public void Disconnect()
    {
        lock (_sync)
        {
            DisconnectNoLock();
        }
    }

    public bool SendCommand(string command, out string error)
    {
        lock (_sync)
        {
            if (_serial is null || !_serial.IsOpen)
            {
                error = "UART is not connected.";
                return false;
            }

            try
            {
                _serial.WriteLine(command);
                ApplyOutgoingCommandNoLock(command);
                AddLog($"> {command}");
                error = string.Empty;
                return true;
            }
            catch (Exception ex)
            {
                error = ex.Message;
                AddLog($"[APP] Send failed: {ex.Message}");
                return false;
            }
        }
    }

    public UartStatusResponse GetStatus()
    {
        lock (_sync)
        {
            return new UartStatusResponse
            {
                Connected = _connected,
                PortName = _portName,
                BaudRate = _baudRate,
                Telemetry = new TelemetrySnapshot
                {
                    TimestampUtc = _telemetry.TimestampUtc,
                    Position = _telemetry.Position,
                    Velocity = _telemetry.Velocity,
                    Torque = _telemetry.Torque,
                    Status = _telemetry.Status,
                    RunEnabled = _telemetry.RunEnabled
                },
                Settings = new SettingsSnapshot
                {
                    TimestampUtc = _settings.TimestampUtc,
                    Manual = new ManualSettings
                    {
                        Position = _settings.Manual.Position,
                        Speed = _settings.Manual.Speed,
                        Torque = _settings.Manual.Torque,
                        AbsMode = _settings.Manual.AbsMode
                    },
                    Parameter = new ParameterSettings
                    {
                        JogSpeed = _settings.Parameter.JogSpeed,
                        Acc = _settings.Parameter.Acc,
                        Dec = _settings.Parameter.Dec,
                        LimitPlus = _settings.Parameter.LimitPlus,
                        LimitMinus = _settings.Parameter.LimitMinus,
                        GearRatio = _settings.Parameter.GearRatio,
                        HomeOffset = _settings.Parameter.HomeOffset,
                        PositionGain = _settings.Parameter.PositionGain,
                        BallLeadMm = _settings.Parameter.BallLeadMm,
                        EncRes = _settings.Parameter.EncRes
                    },
                    Program = new ProgramSettings
                    {
                        Pos1 = _settings.Program.Pos1,
                        Pos2 = _settings.Program.Pos2,
                        Pos3 = _settings.Program.Pos3,
                        Speed1 = _settings.Program.Speed1,
                        Speed2 = _settings.Program.Speed2,
                        Speed3 = _settings.Program.Speed3,
                        Torque1 = _settings.Program.Torque1,
                        Torque2 = _settings.Program.Torque2,
                        Torque3 = _settings.Program.Torque3,
                        ReturnSpeed = _settings.Program.ReturnSpeed,
                        DelayMs = _settings.Program.DelayMs
                    }
                },
                PressStatus = new PressStatusSnapshot
                {
                    TimestampUtc  = _pressStatus.TimestampUtc,
                    Mode          = _pressStatus.Mode,
                    Step          = _pressStatus.Step,
                    EstopOk       = _pressStatus.EstopOk,
                    DoorOk        = _pressStatus.DoorOk,
                    DriveReady    = _pressStatus.DriveReady,
                    HomeComplete  = _pressStatus.HomeComplete,
                },
                Counter = new ProductionCounterSnapshot
                {
                    TimestampUtc   = _counter.TimestampUtc,
                    Total          = _counter.Total,
                    Ok             = _counter.Ok,
                    Ng             = _counter.Ng,
                    ConsecutiveNg  = _counter.ConsecutiveNg,
                    NgRatePct      = _counter.NgRatePct,
                },
                LastResult = new CycleResultSnapshot
                {
                    TimestampUtc  = _lastResult.TimestampUtc,
                    CycleNumber   = _lastResult.CycleNumber,
                    Result        = _lastResult.Result,
                    PeakForcePct  = _lastResult.PeakForcePct,
                    EndPosition   = _lastResult.EndPosition,
                    CycleTimeMs   = _lastResult.CycleTimeMs,
                },
                ActiveAlarm = new AlarmSnapshot
                {
                    TimestampUtc = _activeAlarm.TimestampUtc,
                    Code         = _activeAlarm.Code,
                    Message      = _activeAlarm.Message,
                    Acked        = _activeAlarm.Acked,
                },
                Logs = _logs.ToArray(),
                MaintCounter = new MaintCounterSnapshot
                {
                    TimestampUtc    = _maint.TimestampUtc,
                    TotalCycles     = _maint.TotalCycles,
                    SinceLastMaint  = _maint.SinceLastMaint,
                    PmThreshold     = _maint.PmThreshold,
                    PmAlert         = _maint.PmAlert,
                },
                UserLevel = new UserLevelResponse
                {
                    Level = _userLevel.Level,
                    Name  = _userLevel.Name,
                },
            };
        }
    }

    public HistoryResponse GetHistory(int maxCount = 100)
    {
        lock (_sync)
        {
            var arr = _history.ToArray();
            int take = Math.Min(maxCount, arr.Length);
            return new HistoryResponse
            {
                Total = arr.Length,
                Records = arr.TakeLast(take).Reverse().ToArray()
            };
        }
    }

    public AlarmHistoryEntry[] GetAlarmHistory()
    {
        lock (_sync)
        {
            return _alarmHistory.ToArray().Reverse().ToArray();
        }
    }

    public string GetCsvHistory()
    {
        lock (_sync)
        {
            var sb = new StringBuilder();
            sb.AppendLine("cycle_no,timestamp_utc,result,peak_force_pct,end_pos,cycle_time_ms");
            foreach (var r in _history.ToArray().Reverse())
            {
                sb.AppendLine(string.Format(CultureInfo.InvariantCulture,
                    "{0},{1},{2},{3},{4},{5}",
                    r.CycleNumber,
                    r.TimestampUtc.ToString("yyyy-MM-ddTHH:mm:ss.fffZ", CultureInfo.InvariantCulture),
                    r.Result,
                    r.PeakForcePct,
                    r.EndPosition,
                    r.CycleTimeMs));
            }
            return sb.ToString();
        }
    }

    public void ClearHistory()
    {
        lock (_sync)
        {
            _history.Clear();
        }
    }

    public void Dispose()
    {
        lock (_sync)
        {
            DisconnectNoLock();
        }
    }

    private void DisconnectNoLock()
    {
        if (_serial is null)
        {
            _connected = false;
            return;
        }

        try
        {
            _serial.DataReceived -= OnDataReceived;
            if (_serial.IsOpen)
            {
                _serial.Close();
            }
            AddLog("[APP] Disconnected");
        }
        catch (Exception ex)
        {
            AddLog($"[APP] Disconnect error: {ex.Message}");
        }
        finally
        {
            _serial.Dispose();
            _serial = null;
            _connected = false;
            _portName = string.Empty;
            _baudRate = 0;
        }
    }

    private void OnDataReceived(object? sender, SerialDataReceivedEventArgs e)
    {
        lock (_sync)
        {
            if (_serial is null)
            {
                return;
            }

            try
            {
                var incoming = _serial.ReadExisting();
                if (incoming.Length == 0)
                {
                    return;
                }

                _rxBuffer.Append(incoming);
                DrainReceivedLinesNoLock();
            }
            catch (Exception ex)
            {
                AddLog($"[APP] RX error: {ex.Message}");
            }
        }
    }

    private void DrainReceivedLinesNoLock()
    {
        var text = _rxBuffer.ToString();
        var lastLf = text.LastIndexOf('\n');
        if (lastLf < 0)
        {
            return;
        }

        var chunk = text[..(lastLf + 1)];
        _rxBuffer.Remove(0, lastLf + 1);

        var lines = chunk.Replace("\r", string.Empty)
            .Split('\n', StringSplitOptions.RemoveEmptyEntries);

        foreach (var rawLine in lines)
        {
            var line = rawLine.Trim();
            if (line.Length == 0)
            {
                continue;
            }

            if (!line.StartsWith("TEL,", StringComparison.OrdinalIgnoreCase) &&
                !line.StartsWith("[TIMING]", StringComparison.OrdinalIgnoreCase) &&
                !line.StartsWith("HSTI,", StringComparison.OrdinalIgnoreCase) &&
                !line.StartsWith("ALME,", StringComparison.OrdinalIgnoreCase))
            {
                AddLog(line);
            }

            if (TryParseConfigSnapshot(line))
            {
                continue;
            }

            if (TryParsePressStatusFrame(line))
            {
                continue;
            }

            if (TryParseCounterFrame(line))
            {
                continue;
            }

            if (TryParseCycleResultFrame(line))
            {
                continue;
            }

            if (TryParseAlarmFrame(line))
            {
                continue;
            }

            if (TryParseMaintFrame(line))
            {
                continue;
            }

            if (TryParseUserLevelFrame(line))
            {
                continue;
            }

            if (!TryParseTelemetry(line, out var parsed))
            {
                continue;
            }

            if (parsed.HasKinematics)
            {
                _telemetry.Position = parsed.Position;
                _telemetry.Velocity = parsed.Velocity;
                _telemetry.Torque = parsed.Torque;
            }

            if (parsed.RunEnabled.HasValue)
            {
                _telemetry.RunEnabled = parsed.RunEnabled;
            }

            _telemetry.Status = parsed.Status;
            _telemetry.TimestampUtc = DateTime.UtcNow;
        }
    }

    private void AddLog(string line)
    {
        var stamp = DateTime.Now.ToString("HH:mm:ss.fff", CultureInfo.InvariantCulture);
        _logs.Enqueue($"[{stamp}] {line}");
        while (_logs.Count > MaxLogs)
        {
            _logs.Dequeue();
        }
    }

    private bool TryParseTelemetry(string line, out ParsedTelemetry parsed)
    {
        parsed = new ParsedTelemetry();
        var normalized = NormalizeTelemetryLine(line);

        if (TryParseJsonTelemetry(normalized, out parsed))
        {
            return true;
        }

        if (TryParseTelTelemetry(normalized, out parsed))
        {
            return true;
        }

        if (TryParseStatuswordLine(normalized, out var swStatus))
        {
            parsed = new ParsedTelemetry
            {
                Position = _telemetry.Position,
                Velocity = _telemetry.Velocity,
                Torque = _telemetry.Torque,
                Status = swStatus,
                RunEnabled = _telemetry.RunEnabled,
                HasKinematics = false
            };
            return true;
        }

        return false;
    }

    private void ApplyOutgoingCommandNoLock(string command)
    {
        var text = command.Trim();
        if (text.StartsWith("CMD,", StringComparison.OrdinalIgnoreCase))
        {
            text = text[4..];
        }

        var eq = text.IndexOf('=');
        if (eq <= 0 || eq >= text.Length - 1)
        {
            return;
        }

        var key = text[..eq].Trim();
        var value = text[(eq + 1)..].Trim();
        if (key.Length == 0)
        {
            return;
        }

        if (string.Equals(key, "manual_pos", StringComparison.OrdinalIgnoreCase) &&
            int.TryParse(value, NumberStyles.Integer, CultureInfo.InvariantCulture, out var mPos))
        {
            _settings.Manual.Position = mPos;
        }
        else if (string.Equals(key, "manual_speed", StringComparison.OrdinalIgnoreCase) &&
                 int.TryParse(value, NumberStyles.Integer, CultureInfo.InvariantCulture, out var mSpeed))
        {
            _settings.Manual.Speed = mSpeed;
        }
        else if (string.Equals(key, "manual_torque", StringComparison.OrdinalIgnoreCase) &&
                 int.TryParse(value, NumberStyles.Integer, CultureInfo.InvariantCulture, out var mTorque))
        {
            _settings.Manual.Torque = mTorque;
        }
        else if (string.Equals(key, "manual_abs", StringComparison.OrdinalIgnoreCase))
        {
            var b = ReadMapBool(new Dictionary<string, string> { ["v"] = value }, "v");
            if (b.HasValue)
            {
                _settings.Manual.AbsMode = b.Value;
            }
        }

        /* Optimistically mirror the outgoing value so in-memory settings stay
         * consistent, but do NOT update TimestampUtc. Updating the timestamp here
         * would trigger applySettingsToForm mid-sequence (while only some parameters
         * have been sent), causing the form to briefly show stale/default values
         * (e.g. gear=1 before param_gear_ratio arrives). The form updates only when
         * a real CFGP arrives from firmware, which carries the complete confirmed state. */
        TryApplyOutgoingParameterKey(key, value);
        TryApplyOutgoingProgramKey(key, value);
    }

    private bool TryApplyOutgoingParameterKey(string key, string value)
    {
        if (!int.TryParse(value, NumberStyles.Integer, CultureInfo.InvariantCulture, out var n))
        {
            return false;
        }

        if (string.Equals(key, "param_jog_speed", StringComparison.OrdinalIgnoreCase)) { _settings.Parameter.JogSpeed = n; return true; }
        if (string.Equals(key, "param_acc", StringComparison.OrdinalIgnoreCase)) { _settings.Parameter.Acc = n; return true; }
        if (string.Equals(key, "param_dec", StringComparison.OrdinalIgnoreCase)) { _settings.Parameter.Dec = n; return true; }
        if (string.Equals(key, "param_limit_plus", StringComparison.OrdinalIgnoreCase)) { _settings.Parameter.LimitPlus = n; return true; }
        if (string.Equals(key, "param_limit_minus", StringComparison.OrdinalIgnoreCase)) { _settings.Parameter.LimitMinus = n; return true; }
        if (string.Equals(key, "param_gear_ratio", StringComparison.OrdinalIgnoreCase)) { _settings.Parameter.GearRatio = n; return true; }
        if (string.Equals(key, "param_lead", StringComparison.OrdinalIgnoreCase)) { _settings.Parameter.BallLeadMm = n; return true; }
        if (string.Equals(key, "param_enc_res", StringComparison.OrdinalIgnoreCase)) { _settings.Parameter.EncRes = n; return true; }
        if (string.Equals(key, "param_home_offset", StringComparison.OrdinalIgnoreCase)) { _settings.Parameter.HomeOffset = n; return true; }
        if (string.Equals(key, "param_position_gain", StringComparison.OrdinalIgnoreCase)) { _settings.Parameter.PositionGain = n; return true; }
        return false;
    }

    private bool TryApplyOutgoingProgramKey(string key, string value)
    {
        if (!int.TryParse(value, NumberStyles.Integer, CultureInfo.InvariantCulture, out var n))
        {
            return false;
        }

        if (string.Equals(key, "prog_pos1", StringComparison.OrdinalIgnoreCase)) { _settings.Program.Pos1 = n; return true; }
        if (string.Equals(key, "prog_pos2", StringComparison.OrdinalIgnoreCase)) { _settings.Program.Pos2 = n; return true; }
        if (string.Equals(key, "prog_pos3", StringComparison.OrdinalIgnoreCase)) { _settings.Program.Pos3 = n; return true; }
        if (string.Equals(key, "prog_speed1", StringComparison.OrdinalIgnoreCase)) { _settings.Program.Speed1 = n; return true; }
        if (string.Equals(key, "prog_speed2", StringComparison.OrdinalIgnoreCase)) { _settings.Program.Speed2 = n; return true; }
        if (string.Equals(key, "prog_speed3", StringComparison.OrdinalIgnoreCase)) { _settings.Program.Speed3 = n; return true; }
        if (string.Equals(key, "prog_torque1", StringComparison.OrdinalIgnoreCase)) { _settings.Program.Torque1 = n; return true; }
        if (string.Equals(key, "prog_torque2", StringComparison.OrdinalIgnoreCase)) { _settings.Program.Torque2 = n; return true; }
        if (string.Equals(key, "prog_torque3", StringComparison.OrdinalIgnoreCase)) { _settings.Program.Torque3 = n; return true; }
        if (string.Equals(key, "prog_return_speed", StringComparison.OrdinalIgnoreCase)) { _settings.Program.ReturnSpeed = n; return true; }
        if (string.Equals(key, "prog_delay_ms", StringComparison.OrdinalIgnoreCase)) { _settings.Program.DelayMs = n; return true; }
        return false;
    }

    private bool TryParseConfigSnapshot(string line)
    {
        var trimmed = line.Trim();

        if (trimmed.StartsWith("CFGM,", StringComparison.OrdinalIgnoreCase))
        {
            var map = ParseKeyValueMap(trimmed[5..]);
            _settings.Manual.Position = ReadMapInt(map, _settings.Manual.Position, "pos", "position");
            _settings.Manual.Speed = ReadMapInt(map, _settings.Manual.Speed, "speed");
            _settings.Manual.Torque = ReadMapInt(map, _settings.Manual.Torque, "torque");
            _settings.Manual.AbsMode = ReadMapBool(map, "abs", "absmode") ?? _settings.Manual.AbsMode;
            _settings.TimestampUtc = DateTime.UtcNow;
            return true;
        }

        if (trimmed.StartsWith("CFGP,", StringComparison.OrdinalIgnoreCase))
        {
            var map = ParseKeyValueMap(trimmed[5..]);
            _settings.Parameter.JogSpeed = ReadMapInt(map, _settings.Parameter.JogSpeed, "jog", "jog_speed");
            _settings.Parameter.Acc = ReadMapInt(map, _settings.Parameter.Acc, "acc");
            _settings.Parameter.Dec = ReadMapInt(map, _settings.Parameter.Dec, "dec");
            _settings.Parameter.LimitPlus = ReadMapInt(map, _settings.Parameter.LimitPlus, "lplus", "limit_plus");
            _settings.Parameter.LimitMinus = ReadMapInt(map, _settings.Parameter.LimitMinus, "lminus", "limit_minus");
            _settings.Parameter.GearRatio = ReadMapInt(map, _settings.Parameter.GearRatio, "gear", "gear_ratio");
            _settings.Parameter.BallLeadMm = ReadMapInt(map, _settings.Parameter.BallLeadMm, "lead", "ball_lead");
            _settings.Parameter.EncRes = ReadMapInt(map, _settings.Parameter.EncRes, "enc", "enc_res");
            _settings.Parameter.HomeOffset = ReadMapInt(map, _settings.Parameter.HomeOffset, "home", "home_offset");
            _settings.Parameter.PositionGain = ReadMapInt(map, _settings.Parameter.PositionGain, "gain", "position_gain");
            _settings.TimestampUtc = DateTime.UtcNow;
            return true;
        }

        if (trimmed.StartsWith("CFGR,", StringComparison.OrdinalIgnoreCase))
        {
            var map = ParseKeyValueMap(trimmed[5..]);
            _settings.Program.Pos1 = ReadMapInt(map, _settings.Program.Pos1, "p1", "pos1");
            _settings.Program.Pos2 = ReadMapInt(map, _settings.Program.Pos2, "p2", "pos2");
            _settings.Program.Pos3 = ReadMapInt(map, _settings.Program.Pos3, "p3", "pos3");
            _settings.Program.Speed1 = ReadMapInt(map, _settings.Program.Speed1, "s1", "speed1");
            _settings.Program.Speed2 = ReadMapInt(map, _settings.Program.Speed2, "s2", "speed2");
            _settings.Program.Speed3 = ReadMapInt(map, _settings.Program.Speed3, "s3", "speed3");
            _settings.Program.Torque1 = ReadMapInt(map, _settings.Program.Torque1, "t1", "torque1");
            _settings.Program.Torque2 = ReadMapInt(map, _settings.Program.Torque2, "t2", "torque2");
            _settings.Program.Torque3 = ReadMapInt(map, _settings.Program.Torque3, "t3", "torque3");
            _settings.Program.ReturnSpeed = ReadMapInt(map, _settings.Program.ReturnSpeed, "rs", "return_speed");
            _settings.Program.DelayMs = ReadMapInt(map, _settings.Program.DelayMs, "delay", "delay_ms");
            _settings.TimestampUtc = DateTime.UtcNow;
            return true;
        }

        return false;
    }

    private bool TryParsePressStatusFrame(string line)
    {
        if (!line.StartsWith("PST,", StringComparison.OrdinalIgnoreCase)) { return false; }

        var map = ParseKeyValueMap(line[4..]);
        _pressStatus.Mode         = map.GetValueOrDefault("mode", _pressStatus.Mode);
        _pressStatus.Step         = map.GetValueOrDefault("step", _pressStatus.Step);
        _pressStatus.EstopOk      = ReadMapBool(map, "estop") ?? _pressStatus.EstopOk;
        _pressStatus.DoorOk       = ReadMapBool(map, "door") ?? _pressStatus.DoorOk;
        _pressStatus.DriveReady   = ReadMapBool(map, "drive") ?? _pressStatus.DriveReady;
        _pressStatus.HomeComplete = ReadMapBool(map, "home") ?? _pressStatus.HomeComplete;
        _pressStatus.TimestampUtc = DateTime.UtcNow;
        return true;
    }

    private bool TryParseCounterFrame(string line)
    {
        if (!line.StartsWith("CNT,", StringComparison.OrdinalIgnoreCase)) { return false; }

        var map = ParseKeyValueMap(line[4..]);
        _counter.Total         = ReadMapLong(map, 0L, "total");
        _counter.Ok            = ReadMapLong(map, 0L, "ok");
        _counter.Ng            = ReadMapLong(map, 0L, "ng");
        _counter.ConsecutiveNg = ReadMapLong(map, 0L, "cng");
        /* rate is "X.Y" format */
        if (map.TryGetValue("rate", out var rateStr) &&
            double.TryParse(rateStr, NumberStyles.Float, CultureInfo.InvariantCulture, out var rate))
        {
            _counter.NgRatePct = rate;
        }
        _counter.TimestampUtc = DateTime.UtcNow;
        return true;
    }

    private bool TryParseCycleResultFrame(string line)
    {
        string payload;
        if (line.StartsWith("RST,", StringComparison.OrdinalIgnoreCase))
        {
            payload = line[4..];
        }
        else if (line.StartsWith("HSTI,", StringComparison.OrdinalIgnoreCase))
        {
            payload = line[5..];
        }
        else
        {
            return false;
        }

        var map = ParseKeyValueMap(payload);
        var snap = new CycleResultSnapshot
        {
            CycleNumber  = ReadMapLong(map, _lastResult.CycleNumber, "cycle", "n"),
            Result       = map.GetValueOrDefault("result", _lastResult.Result),
            PeakForcePct = (int)ReadMapLong(map, _lastResult.PeakForcePct, "force"),
            EndPosition  = ReadMapLong(map, _lastResult.EndPosition, "pos"),
            CycleTimeMs  = ReadMapLong(map, _lastResult.CycleTimeMs, "ms"),
            TimestampUtc = DateTime.UtcNow
        };
        _lastResult = snap;

        /* push to rolling history */
        _history.Enqueue(snap);
        while (_history.Count > MaxHistory) { _history.Dequeue(); }
        return true;
    }

    private bool TryParseAlarmFrame(string line)
    {
        if (line.StartsWith("ALM,", StringComparison.OrdinalIgnoreCase))
        {
            var map = ParseKeyValueMap(line[4..]);
            var code = (int)ReadMapLong(map, _activeAlarm.Code, "code");
            var msg  = map.GetValueOrDefault("msg", _activeAlarm.Message);
            var acked = ReadMapBool(map, "ack") ?? _activeAlarm.Acked;
            if (code != 0 && code != _activeAlarm.Code)
            {
                /* new alarm event → append to alarm history */
                _alarmHistory.Enqueue(new AlarmHistoryEntry
                {
                    TimestampUtc = DateTime.UtcNow,
                    Code = code,
                    Message = msg,
                    Acked = acked
                });
                while (_alarmHistory.Count > MaxAlarmHistory) { _alarmHistory.Dequeue(); }
            }
            _activeAlarm.Code    = code;
            _activeAlarm.Message = msg;
            _activeAlarm.Acked   = acked;
            _activeAlarm.TimestampUtc = DateTime.UtcNow;
            return true;
        }

        if (line.StartsWith("ALME,", StringComparison.OrdinalIgnoreCase))
        {
            var map = ParseKeyValueMap(line[5..]);
            var entry = new AlarmHistoryEntry
            {
                TimestampUtc = DateTime.UtcNow,
                Code    = (int)ReadMapLong(map, 0L, "code"),
                Message = map.GetValueOrDefault("msg", "NONE"),
                Acked   = ReadMapBool(map, "ack") ?? false,
                OccurredMs = (uint)ReadMapLong(map, 0L, "ms")
            };
            if (entry.Code != 0)
            {
                _alarmHistory.Enqueue(entry);
                while (_alarmHistory.Count > MaxAlarmHistory) { _alarmHistory.Dequeue(); }
            }
            return true;
        }

        return false;
    }

    private bool TryParseMaintFrame(string line)
    {
        if (!line.StartsWith("MCT,", StringComparison.OrdinalIgnoreCase)) { return false; }

        var map = ParseKeyValueMap(line[4..]);
        _maint.TotalCycles    = ReadMapLong(map, _maint.TotalCycles, "total");
        _maint.SinceLastMaint = ReadMapLong(map, _maint.SinceLastMaint, "since");
        _maint.PmThreshold    = ReadMapLong(map, _maint.PmThreshold, "thresh");
        _maint.PmAlert        = ReadMapBool(map, "alert") ?? _maint.PmAlert;
        _maint.TimestampUtc   = DateTime.UtcNow;
        return true;
    }

    private bool TryParseUserLevelFrame(string line)
    {
        if (!line.StartsWith("ULV,", StringComparison.OrdinalIgnoreCase)) { return false; }

        var map = ParseKeyValueMap(line[4..]);
        _userLevel.Level = (int)ReadMapLong(map, _userLevel.Level, "level");
        _userLevel.Name  = map.GetValueOrDefault("name", _userLevel.Name);
        return true;
    }

    private static long ReadMapLong(Dictionary<string, string> map, long fallback, params string[] keys)
    {
        foreach (var k in keys)
        {
            if (map.TryGetValue(k, out var v) &&
                long.TryParse(v, NumberStyles.Integer, CultureInfo.InvariantCulture, out var n))
            {
                return n;
            }
        }
        return fallback;
    }

    private static Dictionary<string, string> ParseKeyValueMap(string payload)
    {
        var map = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
        foreach (var token in payload.Split(new[] { ',', ';', ' ' }, StringSplitOptions.RemoveEmptyEntries))
        {
            var idx = token.IndexOf('=');
            if (idx <= 0 || idx >= token.Length - 1)
            {
                continue;
            }

            var key = token[..idx].Trim();
            var value = token[(idx + 1)..].Trim();
            if (key.Length > 0)
            {
                map[key] = value;
            }
        }

        return map;
    }

    private static string NormalizeTelemetryLine(string line)
    {
        var trimmed = line.Trim();

        var telIndex = trimmed.IndexOf("TEL", StringComparison.OrdinalIgnoreCase);
        if (telIndex >= 0)
        {
            return trimmed[telIndex..];
        }

        var jsonIndex = trimmed.IndexOf('{');
        if (jsonIndex >= 0)
        {
            return trimmed[jsonIndex..];
        }

        return trimmed;
    }

    private static bool TryParseJsonTelemetry(string line, out ParsedTelemetry parsed)
    {
        parsed = new ParsedTelemetry();

        if (!line.StartsWith('{'))
        {
            return false;
        }

        try
        {
            using var doc = JsonDocument.Parse(line);
            var root = doc.RootElement;

            var pos = ReadJsonDouble(root, "pos", "position", "position_actual");
            var vel = ReadJsonDouble(root, "vel", "velocity", "velocity_actual");
            var tq = ReadJsonDouble(root, "torque", "tq", "torque_actual");

            if (double.IsNaN(pos) && double.IsNaN(vel) && double.IsNaN(tq))
            {
                return false;
            }

            parsed = new ParsedTelemetry
            {
                Position = double.IsNaN(pos) ? 0.0 : pos,
                Velocity = double.IsNaN(vel) ? 0.0 : vel,
                Torque = double.IsNaN(tq) ? 0.0 : tq,
                Status = ReadJsonString(root, "status", "state") ?? "JSON",
                RunEnabled = ReadJsonBool(root, "run", "run_enable", "enabled"),
                HasKinematics = true
            };

            return true;
        }
        catch
        {
            return false;
        }
    }

    private static bool TryParseTelTelemetry(string line, out ParsedTelemetry parsed)
    {
        parsed = new ParsedTelemetry();

        var payload = line;
        var telIndex = line.IndexOf("TEL", StringComparison.OrdinalIgnoreCase);
        if (telIndex >= 0)
        {
            var telLine = line[telIndex..];
            payload = telLine.Length > 3 ? telLine[3..] : string.Empty;
        }

        payload = payload.TrimStart(',', ' ', ';', ':');
        if (payload.Length == 0)
        {
            return false;
        }

        var map = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
        foreach (var token in payload.Split(new[] { ',', ';', ' ' }, StringSplitOptions.RemoveEmptyEntries))
        {
            var idx = token.IndexOf('=');
            if (idx <= 0 || idx >= token.Length - 1)
            {
                continue;
            }

            var key = token[..idx].Trim();
            var value = token[(idx + 1)..].Trim();
            if (key.Length > 0)
            {
                map[key] = value;
            }
        }

        var pos = ReadMapDouble(map, "pos", "position", "position_actual");
        var vel = ReadMapDouble(map, "vel", "velocity", "velocity_actual");
        var tq = ReadMapDouble(map, "torque", "tq", "torque_actual");

        if (double.IsNaN(pos) && double.IsNaN(vel) && double.IsNaN(tq))
        {
            return false;
        }

        parsed = new ParsedTelemetry
        {
            Position = double.IsNaN(pos) ? 0.0 : pos,
            Velocity = double.IsNaN(vel) ? 0.0 : vel,
            Torque = double.IsNaN(tq) ? 0.0 : tq,
            Status = map.TryGetValue("status", out var st) ? st : "TEL",
            RunEnabled = ReadMapBool(map, "run", "run_enable", "enabled"),
            HasKinematics = true
        };

        return true;
    }

    private static bool TryParseStatuswordLine(string line, out string statusText)
    {
        statusText = string.Empty;
        var swIndex = line.IndexOf("SW=0x", StringComparison.OrdinalIgnoreCase);
        if (swIndex < 0)
        {
            return false;
        }

        var start = swIndex + 6;
        var end = start;
        while (end < line.Length && Uri.IsHexDigit(line[end]))
        {
            end++;
        }

        if (end <= start)
        {
            return false;
        }

        statusText = "0x" + line[start..end].ToUpperInvariant();
        return true;
    }

    private static double ReadJsonDouble(JsonElement root, params string[] keys)
    {
        foreach (var key in keys)
        {
            if (!root.TryGetProperty(key, out var prop))
            {
                continue;
            }

            if (prop.ValueKind == JsonValueKind.Number && prop.TryGetDouble(out var n))
            {
                return n;
            }

            if (prop.ValueKind == JsonValueKind.String &&
                double.TryParse(prop.GetString(), NumberStyles.Float, CultureInfo.InvariantCulture, out var s))
            {
                return s;
            }
        }

        return double.NaN;
    }

    private static string? ReadJsonString(JsonElement root, params string[] keys)
    {
        foreach (var key in keys)
        {
            if (!root.TryGetProperty(key, out var prop))
            {
                continue;
            }

            return prop.ValueKind switch
            {
                JsonValueKind.String => prop.GetString(),
                JsonValueKind.Number => prop.GetRawText(),
                _ => null
            };
        }

        return null;
    }

    private static bool? ReadJsonBool(JsonElement root, params string[] keys)
    {
        foreach (var key in keys)
        {
            if (!root.TryGetProperty(key, out var prop))
            {
                continue;
            }

            if (prop.ValueKind == JsonValueKind.True)
            {
                return true;
            }

            if (prop.ValueKind == JsonValueKind.False)
            {
                return false;
            }

            if (prop.ValueKind == JsonValueKind.Number && prop.TryGetInt32(out var n))
            {
                return n != 0;
            }

            if (prop.ValueKind == JsonValueKind.String)
            {
                var s = prop.GetString();
                if (string.Equals(s, "1", StringComparison.OrdinalIgnoreCase) ||
                    string.Equals(s, "true", StringComparison.OrdinalIgnoreCase) ||
                    string.Equals(s, "on", StringComparison.OrdinalIgnoreCase))
                {
                    return true;
                }

                if (string.Equals(s, "0", StringComparison.OrdinalIgnoreCase) ||
                    string.Equals(s, "false", StringComparison.OrdinalIgnoreCase) ||
                    string.Equals(s, "off", StringComparison.OrdinalIgnoreCase))
                {
                    return false;
                }
            }
        }

        return null;
    }

    private static double ReadMapDouble(Dictionary<string, string> map, params string[] keys)
    {
        foreach (var key in keys)
        {
            if (map.TryGetValue(key, out var value) &&
                double.TryParse(value, NumberStyles.Float, CultureInfo.InvariantCulture, out var result))
            {
                return result;
            }
        }

        return double.NaN;
    }

    private static int ReadMapInt(Dictionary<string, string> map, int fallback, params string[] keys)
    {
        foreach (var key in keys)
        {
            if (map.TryGetValue(key, out var value) &&
                int.TryParse(value, NumberStyles.Integer, CultureInfo.InvariantCulture, out var result))
            {
                return result;
            }
        }

        return fallback;
    }

    private static bool? ReadMapBool(Dictionary<string, string> map, params string[] keys)
    {
        foreach (var key in keys)
        {
            if (!map.TryGetValue(key, out var value))
            {
                continue;
            }

            if (string.Equals(value, "1", StringComparison.OrdinalIgnoreCase) ||
                string.Equals(value, "true", StringComparison.OrdinalIgnoreCase) ||
                string.Equals(value, "on", StringComparison.OrdinalIgnoreCase))
            {
                return true;
            }

            if (string.Equals(value, "0", StringComparison.OrdinalIgnoreCase) ||
                string.Equals(value, "false", StringComparison.OrdinalIgnoreCase) ||
                string.Equals(value, "off", StringComparison.OrdinalIgnoreCase))
            {
                return false;
            }
        }

        return null;
    }

    private sealed class ParsedTelemetry
    {
        public double Position { get; set; }
        public double Velocity { get; set; }
        public double Torque { get; set; }
        public string Status { get; set; } = "-";
        public bool? RunEnabled { get; set; }
        public bool HasKinematics { get; set; }
    }
}
