using UartWeb.Models;
using UartWeb.Services;

var builder = WebApplication.CreateBuilder(args);
builder.Logging.AddFilter("Microsoft.AspNetCore", LogLevel.Warning);
builder.Logging.AddFilter("Microsoft.Hosting", LogLevel.Warning);
builder.Services.AddSingleton<UartGateway>();

var app = builder.Build();

app.UseDefaultFiles();
app.UseStaticFiles();

app.MapGet("/api/ports", (UartGateway gateway) => Results.Ok(gateway.GetPorts()));

app.MapPost("/api/connect", (ConnectRequest request, UartGateway gateway) =>
{
    if (string.IsNullOrWhiteSpace(request.PortName))
    {
        return Results.BadRequest(new { error = "PortName is required." });
    }

    var baud = request.BaudRate <= 0 ? 115200 : request.BaudRate;
    if (!gateway.Connect(request.PortName.Trim(), baud, out var error))
    {
        return Results.BadRequest(new { error });
    }

    return Results.Ok(new { ok = true });
});

app.MapPost("/api/disconnect", (UartGateway gateway) =>
{
    gateway.Disconnect();
    return Results.Ok(new { ok = true });
});

app.MapPost("/api/cmd", (CommandRequest request, UartGateway gateway) =>
{
    if (string.IsNullOrWhiteSpace(request.Command))
    {
        return Results.BadRequest(new { error = "Command is empty." });
    }

    if (!gateway.SendCommand(request.Command.Trim(), out var error))
    {
        return Results.BadRequest(new { error });
    }

    return Results.Ok(new { ok = true });
});

app.MapGet("/api/status", (UartGateway gateway) => Results.Ok(gateway.GetStatus()));

/* ---- Demo mode: simulated status for UI testing without real hardware ---- */
app.MapGet("/api/demo/status", () =>
{
    var rng = Random.Shared;
    var cycleNum = rng.Next(100, 500);
    var isOk = rng.NextDouble() > 0.15;
    var modes = new[] { "MANUAL", "AUTO", "SETUP" };
    var steps = new[] { "IDLE", "APPROACH", "CONTACT", "PRESS", "DWELL", "RETURN" };
    return Results.Ok(new UartStatusResponse
    {
        Connected = true,
        PortName = "DEMO",
        BaudRate = 115200,
        Telemetry = new TelemetrySnapshot
        {
            TimestampUtc = DateTime.UtcNow,
            Position = rng.Next(-12000, 0),
            Velocity = rng.Next(0, 500),
            Torque = rng.Next(0, 80),
            Status = "OP_ENABLED",
            RunEnabled = true,
        },
        Settings = new SettingsSnapshot
        {
            Manual = new ManualSettings { Position = -5000, Speed = 200, Torque = 30, AbsMode = true },
            Parameter = new ParameterSettings { JogSpeed = 1000, Acc = 100, Dec = 100, LimitPlus = 100000, LimitMinus = -100000 },
            Program = new ProgramSettings { Pos1 = -5000, Pos2 = -8000, Pos3 = -10000, Speed1 = 300, Speed2 = 100, Speed3 = 50 },
        },
        PressStatus = new PressStatusSnapshot
        {
            TimestampUtc = DateTime.UtcNow,
            Mode = modes[rng.Next(modes.Length)],
            Step = steps[rng.Next(steps.Length)],
            EstopOk = true,
            DoorOk = true,
            DriveReady = true,
            HomeComplete = true,
        },
        Counter = new ProductionCounterSnapshot
        {
            TimestampUtc = DateTime.UtcNow,
            Total = cycleNum,
            Ok = (long)(cycleNum * 0.87),
            Ng = (long)(cycleNum * 0.13),
            ConsecutiveNg = 0,
            NgRatePct = 13.0,
        },
        LastResult = new CycleResultSnapshot
        {
            TimestampUtc = DateTime.UtcNow,
            CycleNumber = cycleNum,
            Result = isOk ? "OK" : "NG_FORCE_HIGH",
            PeakForcePct = rng.Next(35, 75),
            EndPosition = rng.Next(-11000, -9000),
            CycleTimeMs = rng.Next(2000, 4000),
        },
        ActiveAlarm = new AlarmSnapshot
        {
            TimestampUtc = DateTime.MinValue,
            Code = 0,
            Message = "NONE",
            Acked = false,
        },
        MaintCounter = new MaintCounterSnapshot
        {
            TimestampUtc = DateTime.UtcNow,
            TotalCycles = cycleNum + 50000,
            SinceLastMaint = cycleNum + 3200,
            PmThreshold = 100000,
            PmAlert = false,
        },
        UserLevel = new UserLevelResponse { Level = 0, Name = "OPERATOR" },
        Logs = new[]
        {
            "[10:31:22] PST,mode=AUTO,step=IDLE,estop=1,door=1,drive=1,home=1",
            "[10:31:22] CNT,total=312,ok=272,ng=40,cng=0,rate=12.8",
            "[10:31:21] RST,cycle=312,result=OK,force=48,pos=-10120,ms=2850",
            "[10:31:20] [PSM] RETURN → CYCLE_END",
            "[10:31:18] [PSM] DWELL complete → RETURN",
            "[10:31:15] [PSM] PRESS → DWELL (peak=48%)",
        },
    });
});

app.MapGet("/api/demo/history", () =>
{
    var rng = Random.Shared;
    var results = new[] { "OK", "OK", "OK", "NG_FORCE_HIGH", "OK", "NG_FORCE_LOW", "OK", "NG_POS_HIGH" };
    var records = Enumerable.Range(1, 30).Select(i => new CycleResultSnapshot
    {
        TimestampUtc = DateTime.UtcNow.AddMinutes(-i * 2),
        CycleNumber = 300 - i,
        Result = results[rng.Next(results.Length)],
        PeakForcePct = rng.Next(30, 75),
        EndPosition = rng.Next(-11000, -9000),
        CycleTimeMs = rng.Next(2000, 4000),
    }).ToArray();
    return Results.Ok(new HistoryResponse { Total = records.Length, Records = records });
});

app.MapGet("/api/demo/alarms", () =>
{
    return Results.Ok(new[]
    {
        new AlarmHistoryEntry { TimestampUtc = DateTime.UtcNow.AddHours(-1), Code = 101, Message = "E-Stop activated", Acked = true },
        new AlarmHistoryEntry { TimestampUtc = DateTime.UtcNow.AddHours(-3), Code = 401, Message = "Consecutive NG limit reached", Acked = true },
        new AlarmHistoryEntry { TimestampUtc = DateTime.UtcNow.AddHours(-6), Code = 102, Message = "Safety door opened", Acked = true },
    });
});

/* ---- Phase 3: Cycle history ---- */
app.MapGet("/api/history", (UartGateway gateway, int n = 100) =>
    Results.Ok(gateway.GetHistory(n)));

app.MapGet("/api/history/csv", (UartGateway gateway) =>
{
    var csv = gateway.GetCsvHistory();
    return Results.Content(csv, "text/csv; charset=utf-8");
});

app.MapPost("/api/history/clear", (UartGateway gateway) =>
{
    gateway.ClearHistory();
    return Results.Ok(new { ok = true });
});

/* ---- Phase 3: Alarm history ---- */
app.MapGet("/api/alarm/list", (UartGateway gateway) =>
    Results.Ok(gateway.GetAlarmHistory()));

app.MapPost("/api/alarm/ack", (UartGateway gateway) =>
{
    if (!gateway.SendCommand("CMD,alarm_ack=1", out var error))
    {
        return Results.BadRequest(new { error });
    }
    return Results.Ok(new { ok = true });
});

app.MapPost("/api/alarm/reset", (UartGateway gateway) =>
{
    if (!gateway.SendCommand("CMD,alarm_reset=1", out var error))
    {
        return Results.BadRequest(new { error });
    }
    return Results.Ok(new { ok = true });
});

app.MapPost("/api/alarm/fetch", (UartGateway gateway) =>
{
    if (!gateway.SendCommand("CMD,alarm_history_read=20", out var error))
    {
        return Results.BadRequest(new { error });
    }
    return Results.Ok(new { ok = true });
});

/* ---- Phase 4: User Auth ---- */
app.MapGet("/api/user/level", (UartGateway gateway) =>
{
    var status = gateway.GetStatus();
    return Results.Ok(status.UserLevel);
});

app.MapPost("/api/user/login", async (HttpContext ctx, UartGateway gateway) =>
{
    using var reader = new System.IO.StreamReader(ctx.Request.Body);
    var body = await reader.ReadToEndAsync();
    var doc = System.Text.Json.JsonDocument.Parse(body);
    if (!doc.RootElement.TryGetProperty("pin", out var pinEl))
    {
        return Results.BadRequest(new { error = "pin required" });
    }
    var pin = pinEl.GetInt32();
    if (!gateway.SendCommand($"CMD,user_login={pin}", out var error))
    {
        return Results.BadRequest(new { error });
    }
    return Results.Ok(new { ok = true });
});

app.MapPost("/api/user/logout", (UartGateway gateway) =>
{
    if (!gateway.SendCommand("CMD,user_logout=1", out var error))
    {
        return Results.BadRequest(new { error });
    }
    return Results.Ok(new { ok = true });
});

/* ---- Phase 4: Maintenance ---- */
app.MapGet("/api/maintenance", (UartGateway gateway) =>
{
    var status = gateway.GetStatus();
    return Results.Ok(status.MaintCounter);
});

app.MapPost("/api/maintenance/refresh", (UartGateway gateway) =>
{
    if (!gateway.SendCommand("CMD,maint_read=1", out var error))
    {
        return Results.BadRequest(new { error });
    }
    return Results.Ok(new { ok = true });
});

app.MapPost("/api/maintenance/reset", (UartGateway gateway) =>
{
    if (!gateway.SendCommand("CMD,maint_reset=1", out var error))
    {
        return Results.BadRequest(new { error });
    }
    return Results.Ok(new { ok = true });
});

app.MapPost("/api/maintenance/threshold", async (HttpContext ctx, UartGateway gateway) =>
{
    using var reader = new System.IO.StreamReader(ctx.Request.Body);
    var body = await reader.ReadToEndAsync();
    var doc = System.Text.Json.JsonDocument.Parse(body);
    if (!doc.RootElement.TryGetProperty("threshold", out var thrEl))
    {
        return Results.BadRequest(new { error = "threshold required" });
    }
    var thr = thrEl.GetInt64();
    if (!gateway.SendCommand($"CMD,maint_thresh={thr}", out var error))
    {
        return Results.BadRequest(new { error });
    }
    return Results.Ok(new { ok = true });
});

app.Run("http://localhost:5100");
