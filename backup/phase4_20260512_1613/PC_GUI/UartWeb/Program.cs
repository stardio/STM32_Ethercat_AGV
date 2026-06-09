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

app.Run();
