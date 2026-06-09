using System.Text.Json;
using UartWeb.Models;
using UartWeb.Services;

var builder = WebApplication.CreateBuilder(args);
builder.Logging.AddFilter("Microsoft.AspNetCore", LogLevel.Warning);
builder.Logging.AddFilter("Microsoft.Hosting", LogLevel.Warning);
builder.Services.AddSingleton<GraphDb>();
builder.Services.AddSingleton<UartGateway>();

var app = builder.Build();

app.UseDefaultFiles();
app.UseStaticFiles(new StaticFileOptions
{
    OnPrepareResponse = ctx =>
    {
        var ext = Path.GetExtension(ctx.File.Name).ToLowerInvariant();
        if (ext == ".html" || ext == ".htm")
        {
            ctx.Context.Response.Headers["Cache-Control"] = "no-cache, no-store, must-revalidate";
            ctx.Context.Response.Headers["Pragma"] = "no-cache";
            ctx.Context.Response.Headers["Expires"] = "0";
        }
    }
});

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

/* SSE: 텔레메트리를 UART 수신 즉시 브라우저에 푸시 (폴링 없음) */
app.MapGet("/api/telemetry/stream", async (UartGateway gateway, HttpContext ctx, CancellationToken ct) =>
{
    ctx.Response.Headers["Content-Type"]      = "text/event-stream";
    ctx.Response.Headers["Cache-Control"]     = "no-cache";
    ctx.Response.Headers["X-Accel-Buffering"] = "no";
    ctx.Response.Headers["Connection"]        = "keep-alive";

    await foreach (var snap in gateway.SubscribeTelemetry(ct))
    {
        var json = JsonSerializer.Serialize(snap);
        await ctx.Response.WriteAsync($"data: {json}\n\n", ct);
        await ctx.Response.Body.FlushAsync(ct);
    }
});

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

/* ---- Press Parameter JSON persistence ---- */
static string DataPath(string filename) =>
    Path.Combine(Directory.GetCurrentDirectory(), filename);

app.MapGet("/api/press-params", () =>
{
    var path = DataPath("press-params.json");
    if (!File.Exists(path)) return Results.Ok(new { });
    var json = File.ReadAllText(path);
    return Results.Content(json, "application/json");
});

app.MapPost("/api/press-params", (JsonElement body) =>
{
    File.WriteAllText(DataPath("press-params.json"), body.GetRawText());
    return Results.Ok(new { ok = true });
});

/* ---- Program Parameter JSON persistence ---- */
app.MapGet("/api/program", () =>
{
    var path = DataPath("program.json");
    if (!File.Exists(path)) return Results.Ok(new { });
    var json = File.ReadAllText(path);
    return Results.Content(json, "application/json");
});

app.MapPost("/api/program", (JsonElement body) =>
{
    File.WriteAllText(DataPath("program.json"), body.GetRawText());
    return Results.Ok(new { ok = true });
});

/* ---- High-res graph (STM32-sampled, live) ---- */
app.MapGet("/api/graph", (UartGateway gateway) =>
    Results.Ok(gateway.GetGraphData()));

/* ---- Cycle graph DB ---- */
app.MapPost("/api/db/test-insert", (GraphDb db) =>
{
    var rng = Random.Shared;
    var cycleNo = (long)rng.Next(1, 9999);
    var snap = new CycleResultSnapshot
    {
        CycleNumber  = cycleNo,
        Result       = rng.NextDouble() > 0.2 ? "OK" : "NG_FORCE_HIGH",
        PeakForcePct = rng.Next(25, 85),
        EndPosition  = rng.Next(-12000, -7000),
        CycleTimeMs  = rng.Next(1200, 5000),
        TimestampUtc = DateTime.UtcNow,
    };
    var pts = Enumerable.Range(0, 120).Select(i =>
    {
        int step = i < 20 ? 1 : i < 50 ? 2 : i < 80 ? 3 : i < 95 ? 4 : 5;
        return new GraphSamplePoint
        {
            Pos    = (int)(-i * 95L - rng.Next(0, 200)),
            Torque = rng.Next(0, step == 3 ? 800 : 200),
            Step   = step,
        };
    }).ToArray();
    var id = db.SaveCycle(snap, pts);
    return Results.Ok(new { ok = true, id, cycle_no = cycleNo, result = snap.Result, pts = pts.Length });
});

app.MapGet("/api/db/cycles", (GraphDb db, int limit = 100, int offset = 0) =>
    Results.Ok(db.ListCycles(limit, offset)));

app.MapGet("/api/db/cycles/{id:long}", (long id, GraphDb db) =>
{
    var cycle = db.GetCycle(id);
    if (cycle is null) return Results.NotFound();
    var points = db.GetPoints(id);
    return Results.Ok(new CycleGraphResponse { Cycle = cycle, Points = points });
});

app.MapDelete("/api/db/cycles/{id:long}", (long id, GraphDb db) =>
{
    if (!db.DeleteCycle(id)) return Results.NotFound();
    return Results.Ok(new { ok = true });
});

app.MapGet("/api/db/cycles/{id:long}/csv", (long id, GraphDb db) =>
{
    var csv = db.ExportCycleCsv(id);
    if (csv.Length == 0) return Results.NotFound();
    return Results.Content(csv, "text/csv; charset=utf-8");
});

app.MapGet("/api/db/export/csv", (GraphDb db) =>
    Results.Content(db.ExportAllSummaryCsv(), "text/csv; charset=utf-8"));

/* ── Golden Signature Baseline ── */
app.MapPost("/api/baseline/build", (GraphDb db, int count = 10) =>
{
    var profile = db.BuildBaseline(count);
    return profile is null ? Results.BadRequest(new { error = "사이클 데이터 없음" }) : Results.Ok(profile);
});

app.MapGet("/api/baseline", (GraphDb db) =>
{
    var profile = db.GetActiveBaseline();
    return profile is null ? Results.NoContent() : Results.Ok(profile);
});

app.MapDelete("/api/baseline", (GraphDb db) =>
    Results.Ok(new { ok = db.DeleteActiveBaseline() }));

app.MapPost("/api/baseline/reanalyze", (GraphDb db) =>
{
    var baseline = db.GetActiveBaseline();
    if (baseline is null) return Results.BadRequest(new { error = "기준 프로파일 없음" });
    var count = db.ReanalyzeAll(baseline);
    return Results.Ok(new { ok = true, updated = count });
});

/* ---- Parameter JSON persistence ---- */
app.MapGet("/api/params", () =>
{
    var path = DataPath("params.json");
    if (!File.Exists(path)) return Results.Ok(new { });
    var json = File.ReadAllText(path);
    return Results.Content(json, "application/json");
});

app.MapPost("/api/params", (JsonElement body) =>
{
    File.WriteAllText(DataPath("params.json"), body.GetRawText());
    return Results.Ok(new { ok = true });
});

app.Run("http://localhost:5100");
