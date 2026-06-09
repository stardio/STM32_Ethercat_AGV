using System.Globalization;
using System.IO.Ports;
using System.Text;
using System.Text.Json;

namespace UartStudio;

public sealed class MainForm : Form
{
    private readonly ComboBox _portCombo = new();
    private readonly ComboBox _baudCombo = new();
    private readonly Button _refreshButton = new();
    private readonly Button _connectButton = new();
    private readonly Button _lcdCloneButton = new();
    private readonly Label _statusLabel = new();

    private readonly Label _positionValue = new();
    private readonly Label _velocityValue = new();
    private readonly Label _torqueValue = new();
    private readonly Label _stateValue = new();

    private readonly TextBox _commandText = new();
    private readonly Button _sendButton = new();
    private readonly Button _enableButton = new();
    private readonly Button _disableButton = new();
    private readonly Button _saveCsvButton = new();

    private readonly TelemetryPlotControl _plot = new();
    private readonly RichTextBox _rawLog = new();

    private readonly SerialPort _serial = new();
    private readonly object _rxLock = new();
    private readonly StringBuilder _rxBuffer = new();

    private readonly List<TelemetrySample> _history = new();
    private LcdCloneForm? _lcdCloneForm;

    private const int MaxChartPoints = 400;
    private const int MaxLogChars = 300000;
    private static readonly TimeSpan GraphStopDelay = TimeSpan.FromSeconds(2);
    private const double MotionVelocityThreshold = 0.5;
    private const double MotionPositionDeltaThreshold = 0.5;

    private int _sampleIndex;
    private double _lastPosition;
    private double _lastVelocity;
    private double _lastTorque;
    private string _lastStatus = "-";
    private bool? _lastRunEnabled;
    private bool _graphCaptureActive;
    private DateTime _lastMotionDetectedAt = DateTime.MinValue;
    private bool _hasPrevPosition;
    private double _prevPosition;

    public MainForm()
    {
        Text = "STM32 UART Control Studio";
        Width = 1400;
        Height = 900;
        MinimumSize = new Size(1200, 760);
        Font = new Font("Segoe UI", 10F, FontStyle.Regular, GraphicsUnit.Point);
        BackColor = Color.FromArgb(23, 26, 34);
        ForeColor = Color.WhiteSmoke;
        StartPosition = FormStartPosition.CenterScreen;

        BuildUi();
        ConfigurePlot();
        WireEvents();
        RefreshPorts();
        SetConnectedState(false);

        Shown += (_, _) => ApplyInitialSplitLayout();
    }

    private void BuildUi()
    {
        var root = new TableLayoutPanel
        {
            Dock = DockStyle.Fill,
            ColumnCount = 1,
            RowCount = 2,
            BackColor = BackColor,
            Padding = new Padding(12)
        };
        root.RowStyles.Add(new RowStyle(SizeType.Absolute, 72F));
        root.RowStyles.Add(new RowStyle(SizeType.Percent, 100F));
        Controls.Add(root);

        var topBar = new Panel
        {
            Dock = DockStyle.Fill,
            BackColor = Color.FromArgb(32, 36, 48),
            Padding = new Padding(12, 12, 12, 8)
        };
        root.Controls.Add(topBar, 0, 0);

        var title = new Label
        {
            Text = "UART Control Studio",
            AutoSize = true,
            Font = new Font("Segoe UI Semibold", 16F, FontStyle.Bold),
            ForeColor = Color.FromArgb(114, 219, 255),
            Location = new Point(10, 12)
        };
        topBar.Controls.Add(title);

        _statusLabel.AutoSize = true;
        _statusLabel.Font = new Font("Segoe UI", 10F, FontStyle.Bold);
        _statusLabel.Location = new Point(14, 42);
        topBar.Controls.Add(_statusLabel);

        var toolStrip = new FlowLayoutPanel
        {
            Dock = DockStyle.Right,
            AutoSize = true,
            WrapContents = false,
            FlowDirection = FlowDirection.LeftToRight,
            BackColor = Color.Transparent,
            Padding = new Padding(0, 8, 0, 0)
        };
        topBar.Controls.Add(toolStrip);

        _portCombo.Width = 120;
        _baudCombo.Width = 120;
        _baudCombo.Items.AddRange(new object[] { "115200", "230400", "460800", "921600" });
        _baudCombo.SelectedIndex = 2;

        _refreshButton.Text = "Refresh";
        _refreshButton.Width = 90;
        _refreshButton.ForeColor = Color.Black;

        _connectButton.Text = "Connect";
        _connectButton.Width = 90;
        _connectButton.ForeColor = Color.Black;

        _lcdCloneButton.Text = "LCD Clone";
        _lcdCloneButton.Width = 100;
        _lcdCloneButton.ForeColor = Color.Black;

        toolStrip.Controls.Add(new Label { Text = "Port", AutoSize = true, Margin = new Padding(12, 9, 4, 0) });
        toolStrip.Controls.Add(_portCombo);
        toolStrip.Controls.Add(new Label { Text = "Baud", AutoSize = true, Margin = new Padding(12, 9, 4, 0) });
        toolStrip.Controls.Add(_baudCombo);
        toolStrip.Controls.Add(_lcdCloneButton);
        toolStrip.Controls.Add(_refreshButton);
        toolStrip.Controls.Add(_connectButton);

        var bodySplit = new SplitContainer
        {
            Dock = DockStyle.Fill,
            Orientation = Orientation.Vertical,
            BackColor = BackColor,
            Panel1MinSize = 0,
            Panel2MinSize = 0
        };
        root.Controls.Add(bodySplit, 0, 1);

        var leftSplit = new SplitContainer
        {
            Dock = DockStyle.Fill,
            Orientation = Orientation.Horizontal,
            BackColor = BackColor,
            Panel1MinSize = 0,
            Panel2MinSize = 0
        };
        bodySplit.Panel1.Controls.Add(leftSplit);

        var chartPanel = CreateCardPanel();
        chartPanel.Padding = new Padding(10);
        _plot.Dock = DockStyle.Fill;
        chartPanel.Controls.Add(_plot);
        leftSplit.Panel1.Controls.Add(chartPanel);

        var logPanel = CreateCardPanel();
        logPanel.Padding = new Padding(10);
        _rawLog.Dock = DockStyle.Fill;
        _rawLog.ReadOnly = true;
        _rawLog.BackColor = Color.FromArgb(16, 18, 24);
        _rawLog.ForeColor = Color.FromArgb(205, 215, 230);
        _rawLog.BorderStyle = BorderStyle.None;
        _rawLog.Font = new Font("Consolas", 10F, FontStyle.Regular);
        logPanel.Controls.Add(_rawLog);
        leftSplit.Panel2.Controls.Add(logPanel);

        var rightPanel = CreateCardPanel();
        rightPanel.Dock = DockStyle.Fill;
        rightPanel.Padding = new Padding(12);
        bodySplit.Panel2.Controls.Add(rightPanel);

        var rightLayout = new TableLayoutPanel
        {
            Dock = DockStyle.Fill,
            ColumnCount = 1,
            RowCount = 4,
            BackColor = Color.Transparent
        };
        rightLayout.RowStyles.Add(new RowStyle(SizeType.Absolute, 180F));
        rightLayout.RowStyles.Add(new RowStyle(SizeType.Absolute, 220F));
        rightLayout.RowStyles.Add(new RowStyle(SizeType.Percent, 100F));
        rightLayout.RowStyles.Add(new RowStyle(SizeType.Absolute, 48F));
        rightPanel.Controls.Add(rightLayout);

        rightLayout.Controls.Add(CreateStatusBlock(), 0, 0);
        rightLayout.Controls.Add(CreateCommandBlock(), 0, 1);

        var hint = new Label
        {
            Dock = DockStyle.Fill,
            Text = "Recommended telemetry line format:\nTEL,pos=123.4,vel=5.6,torque=0.7,status=RUN,run=1\n\nAlso supports JSON:\n{\"pos\":123.4,\"vel\":5.6,\"torque\":0.7,\"status\":\"RUN\",\"run\":true}\n\nGraph auto mode:\nstart on motor motion, stop 2s after motion ends",
            ForeColor = Color.FromArgb(174, 188, 210),
            Font = new Font("Segoe UI", 9.5F),
            AutoSize = false
        };
        rightLayout.Controls.Add(hint, 0, 2);

        _saveCsvButton.Text = "Save CSV";
        _saveCsvButton.Dock = DockStyle.Fill;
        _saveCsvButton.FlatStyle = FlatStyle.Flat;
        _saveCsvButton.FlatAppearance.BorderColor = Color.FromArgb(76, 96, 130);
        _saveCsvButton.BackColor = Color.FromArgb(35, 52, 76);
        _saveCsvButton.ForeColor = Color.Black;
        rightLayout.Controls.Add(_saveCsvButton, 0, 3);

        void ApplyInitialSplitLayout()
        {
            bodySplit.Panel1MinSize = 760;
            bodySplit.Panel2MinSize = 280;
            leftSplit.Panel1MinSize = 320;
            leftSplit.Panel2MinSize = 180;

            var bodyMax = Math.Max(bodySplit.Panel1MinSize,
                bodySplit.Width - bodySplit.Panel2MinSize - bodySplit.SplitterWidth);
            var bodyTarget = (int)(bodySplit.Width * 0.72);
            bodySplit.SplitterDistance = Math.Max(bodySplit.Panel1MinSize, Math.Min(bodyMax, bodyTarget));

            var leftMax = Math.Max(leftSplit.Panel1MinSize,
                leftSplit.Height - leftSplit.Panel2MinSize - leftSplit.SplitterWidth);
            var leftTarget = (int)(leftSplit.Height * 0.65);
            leftSplit.SplitterDistance = Math.Max(leftSplit.Panel1MinSize, Math.Min(leftMax, leftTarget));
        }

        _applyInitialSplitLayout = ApplyInitialSplitLayout;
    }

    private Action? _applyInitialSplitLayout;

    private void ApplyInitialSplitLayout()
    {
        _applyInitialSplitLayout?.Invoke();
        _applyInitialSplitLayout = null;
    }

    private Panel CreateCardPanel()
    {
        return new Panel
        {
            BackColor = Color.FromArgb(30, 34, 44),
            Dock = DockStyle.Fill,
            Margin = new Padding(8)
        };
    }

    private Control CreateStatusBlock()
    {
        var panel = new Panel { Dock = DockStyle.Fill, BackColor = Color.Transparent };

        var title = new Label
        {
            Text = "Live Status",
            AutoSize = true,
            Font = new Font("Segoe UI Semibold", 12F, FontStyle.Bold),
            ForeColor = Color.FromArgb(144, 225, 255),
            Location = new Point(0, 0)
        };
        panel.Controls.Add(title);

        AddMetric(panel, "Position", _positionValue, 36);
        AddMetric(panel, "Velocity", _velocityValue, 68);
        AddMetric(panel, "Torque", _torqueValue, 100);
        AddMetric(panel, "State", _stateValue, 132);

        return panel;
    }

    private static void AddMetric(Control parent, string name, Label valueLabel, int y)
    {
        var nameLabel = new Label
        {
            Text = name,
            AutoSize = true,
            Location = new Point(0, y),
            ForeColor = Color.FromArgb(182, 194, 214)
        };

        valueLabel.AutoSize = true;
        valueLabel.Location = new Point(110, y);
        valueLabel.Font = new Font("Segoe UI", 10F, FontStyle.Bold);
        valueLabel.ForeColor = Color.WhiteSmoke;
        valueLabel.Text = "-";

        parent.Controls.Add(nameLabel);
        parent.Controls.Add(valueLabel);
    }

    private Control CreateCommandBlock()
    {
        var panel = new Panel { Dock = DockStyle.Fill, BackColor = Color.Transparent };

        var title = new Label
        {
            Text = "Device Commands",
            AutoSize = true,
            Font = new Font("Segoe UI Semibold", 12F, FontStyle.Bold),
            ForeColor = Color.FromArgb(144, 225, 255),
            Location = new Point(0, 0)
        };
        panel.Controls.Add(title);

        _commandText.Width = 230;
        _commandText.Location = new Point(0, 36);
        _commandText.BackColor = Color.FromArgb(17, 20, 28);
        _commandText.ForeColor = Color.WhiteSmoke;
        _commandText.BorderStyle = BorderStyle.FixedSingle;
        _commandText.Text = "CMD,run=1";
        panel.Controls.Add(_commandText);

        _sendButton.Text = "Send";
        _sendButton.Width = 70;
        _sendButton.Location = new Point(236, 34);
        _sendButton.ForeColor = Color.Black;
        panel.Controls.Add(_sendButton);

        _enableButton.Text = "Enable";
        _enableButton.Width = 130;
        _enableButton.Location = new Point(0, 80);
        _enableButton.ForeColor = Color.Black;
        panel.Controls.Add(_enableButton);

        _disableButton.Text = "Disable";
        _disableButton.Width = 130;
        _disableButton.Location = new Point(140, 80);
        _disableButton.ForeColor = Color.Black;
        panel.Controls.Add(_disableButton);

        var info = new Label
        {
            Text = "Tip: If your firmware prints plain logs only, this app still works as serial console.\nTelemetry values update when TEL/JSON lines are detected.",
            AutoSize = false,
            Width = 300,
            Height = 64,
            Location = new Point(0, 122),
            ForeColor = Color.FromArgb(170, 183, 205),
            Font = new Font("Segoe UI", 9F)
        };
        panel.Controls.Add(info);

        return panel;
    }

    private void ConfigurePlot()
    {
        _plot.SetSeriesColors(
            position: Color.FromArgb(62, 199, 255),
            velocity: Color.FromArgb(129, 237, 113),
            torque: Color.FromArgb(255, 157, 64));
    }

    private void WireEvents()
    {
        _refreshButton.Click += (_, _) => RefreshPorts();
        _connectButton.Click += (_, _) => ToggleConnection();
        _lcdCloneButton.Click += (_, _) => OpenLcdCloneWindow();
        _sendButton.Click += (_, _) => SendCommand(_commandText.Text);
        _enableButton.Click += (_, _) => SendCommand("CMD,run=1");
        _disableButton.Click += (_, _) => SendCommand("CMD,run=0");
        _saveCsvButton.Click += (_, _) => SaveCsv();
        _commandText.KeyDown += (_, e) =>
        {
            if (e.KeyCode == Keys.Enter)
            {
                e.SuppressKeyPress = true;
                SendCommand(_commandText.Text);
            }
        };

        _serial.DataReceived += SerialOnDataReceived;
        FormClosing += (_, _) =>
        {
            Disconnect();
            if (_lcdCloneForm is not null && !_lcdCloneForm.IsDisposed)
            {
                _lcdCloneForm.Close();
            }
        };
    }

    private void RefreshPorts()
    {
        var ports = SerialPort.GetPortNames().OrderBy(p => p).ToArray();
        _portCombo.Items.Clear();
        _portCombo.Items.AddRange(ports);

        if (_portCombo.Items.Count > 0)
        {
            _portCombo.SelectedIndex = 0;
        }

        AppendLog($"[APP] Ports refreshed: {(ports.Length == 0 ? "none" : string.Join(", ", ports))}");
    }

    private void ToggleConnection()
    {
        if (_serial.IsOpen)
        {
            Disconnect();
            return;
        }

        if (_portCombo.SelectedItem is not string portName)
        {
            MessageBox.Show(this, "No COM port selected.", "UART", MessageBoxButtons.OK, MessageBoxIcon.Warning);
            return;
        }

        if (!int.TryParse(_baudCombo.SelectedItem?.ToString(), out var baudRate))
        {
            baudRate = 460800;
        }

        try
        {
            _serial.PortName = portName;
            _serial.BaudRate = baudRate;
            _serial.DataBits = 8;
            _serial.StopBits = StopBits.One;
            _serial.Parity = Parity.None;
            _serial.Handshake = Handshake.None;
            _serial.Encoding = Encoding.ASCII;
            _serial.NewLine = "\r\n";
            _serial.ReadTimeout = 200;
            _serial.WriteTimeout = 200;
            _serial.DtrEnable = true;
            _serial.RtsEnable = false;
            _serial.Open();

            SetConnectedState(true);
            AppendLog($"[APP] Connected to {portName} @ {baudRate}");
        }
        catch (Exception ex)
        {
            SetConnectedState(false);
            MessageBox.Show(this, ex.Message, "Open COM failed", MessageBoxButtons.OK, MessageBoxIcon.Error);
        }
    }

    private void Disconnect()
    {
        try
        {
            if (_serial.IsOpen)
            {
                _serial.Close();
                AppendLog("[APP] Disconnected");
            }
        }
        catch (Exception ex)
        {
            AppendLog($"[APP] Disconnect error: {ex.Message}");
        }
        finally
        {
            SetConnectedState(false);
        }
    }

    private void SetConnectedState(bool connected)
    {
        _connectButton.Text = connected ? "Disconnect" : "Connect";
        _statusLabel.Text = connected ? "Status: Connected" : "Status: Disconnected";
        _statusLabel.ForeColor = connected ? Color.FromArgb(125, 232, 125) : Color.FromArgb(255, 145, 145);
        _lcdCloneForm?.SetConnectionState(connected);
    }

    private void SendCommand(string text)
    {
        var command = text.Trim();
        if (command.Length == 0)
        {
            return;
        }

        if (!_serial.IsOpen)
        {
            AppendLog("[APP] Not connected");
            return;
        }

        try
        {
            _serial.WriteLine(command);
            AppendLog($"> {command}");
        }
        catch (Exception ex)
        {
            AppendLog($"[APP] Send failed: {ex.Message}");
        }
    }

    private void OpenLcdCloneWindow()
    {
        if (_lcdCloneForm is not null && !_lcdCloneForm.IsDisposed)
        {
            _lcdCloneForm.Focus();
            return;
        }

        _lcdCloneForm = new LcdCloneForm(
            sendCommand: cmd => SendCommand(cmd),
            initialConnected: _serial.IsOpen);

        _lcdCloneForm.FormClosed += (_, _) => _lcdCloneForm = null;
        _lcdCloneForm.Show(this);
    }

    private void SerialOnDataReceived(object? sender, SerialDataReceivedEventArgs e)
    {
        try
        {
            var incoming = _serial.ReadExisting();
            if (incoming.Length == 0)
            {
                return;
            }

            lock (_rxLock)
            {
                _rxBuffer.Append(incoming);
            }

            BeginInvoke((Action)DrainReceivedLines);
        }
        catch (Exception ex)
        {
            BeginInvoke((Action)(() => AppendLog($"[APP] RX error: {ex.Message}")));
        }
    }

    private void DrainReceivedLines()
    {
        string chunk;

        lock (_rxLock)
        {
            var text = _rxBuffer.ToString();
            var lastLf = text.LastIndexOf('\n');
            if (lastLf < 0)
            {
                return;
            }

            chunk = text[..(lastLf + 1)];
            _rxBuffer.Remove(0, lastLf + 1);
        }

        var lines = chunk.Replace("\r", string.Empty).Split('\n', StringSplitOptions.RemoveEmptyEntries);
        foreach (var rawLine in lines)
        {
            var line = rawLine.Trim();
            if (line.Length == 0)
            {
                continue;
            }

            AppendLog(line);

            if (TryParseTelemetry(line, out var sample))
            {
                AddSample(sample);
            }
        }
    }

    private void AddSample(TelemetrySample sample)
    {
        _history.Add(sample);

        if (sample.HasKinematics)
        {
            _lastPosition = sample.Position;
            _lastVelocity = sample.Velocity;
            _lastTorque = sample.Torque;
        }
        _lastStatus = sample.Status;
        if (sample.RunEnabled.HasValue)
        {
            _lastRunEnabled = sample.RunEnabled.Value;
        }

        _positionValue.Text = _lastPosition.ToString("0.###", CultureInfo.InvariantCulture);
        _velocityValue.Text = _lastVelocity.ToString("0.###", CultureInfo.InvariantCulture);
        _torqueValue.Text = _lastTorque.ToString("0.###", CultureInfo.InvariantCulture);
        _stateValue.Text = sample.Status;
        _lcdCloneForm?.UpdateTelemetry(_lastPosition, _lastVelocity, _lastTorque, sample.Status, _lastRunEnabled);

        var movingNow = DetectMotorMotion(sample);
        if (movingNow)
        {
            _graphCaptureActive = true;
            _lastMotionDetectedAt = sample.Timestamp;
        }
        else if (_graphCaptureActive && (sample.Timestamp - _lastMotionDetectedAt) > GraphStopDelay)
        {
            _graphCaptureActive = false;
        }

        if (!_graphCaptureActive)
        {
            return;
        }

        if (!sample.HasKinematics)
        {
            return;
        }

        _plot.AddPoint(_sampleIndex, sample.Position, sample.Velocity, sample.Torque, MaxChartPoints);
        _sampleIndex++;
    }

    private bool DetectMotorMotion(TelemetrySample sample)
    {
        if (sample.RunEnabled == false)
        {
            return false;
        }

        if (!sample.HasKinematics)
        {
            return false;
        }

        var hasRunStart = (sample.RunEnabled == true) && !_graphCaptureActive;
        var velAbs = Math.Abs(sample.Velocity);

        var posDelta = 0.0;
        if (_hasPrevPosition)
        {
            posDelta = Math.Abs(sample.Position - _prevPosition);
        }
        _prevPosition = sample.Position;
        _hasPrevPosition = true;

        if (hasRunStart)
        {
            return true;
        }

        if (velAbs >= MotionVelocityThreshold)
        {
            return true;
        }

        return posDelta >= MotionPositionDeltaThreshold;
    }

    private bool TryParseTelemetry(string line, out TelemetrySample sample)
    {
        sample = default;
        var normalized = NormalizeTelemetryLine(line);

        if (TryParseJsonTelemetry(normalized, out sample))
        {
            return true;
        }

        if (TryParseTelTelemetry(normalized, out sample))
        {
            return true;
        }

        if (TryParseStatuswordLine(normalized, out var statusText))
        {
            sample = new TelemetrySample(
                Timestamp: DateTime.Now,
                Position: _lastPosition,
                Velocity: _lastVelocity,
                Torque: _lastTorque,
                Status: statusText,
                RunEnabled: _lastRunEnabled,
                HasKinematics: false);
            return true;
        }

        return false;
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

    private static bool TryParseJsonTelemetry(string line, out TelemetrySample sample)
    {
        sample = default;

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

            sample = new TelemetrySample(
                Timestamp: DateTime.Now,
                Position: double.IsNaN(pos) ? 0.0 : pos,
                Velocity: double.IsNaN(vel) ? 0.0 : vel,
                Torque: double.IsNaN(tq) ? 0.0 : tq,
                Status: ReadJsonString(root, "status", "state") ?? "JSON",
                RunEnabled: ReadJsonBool(root, "run", "run_enable", "enabled"),
                HasKinematics: true);
            return true;
        }
        catch
        {
            return false;
        }
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

    private static bool TryParseTelTelemetry(string line, out TelemetrySample sample)
    {
        sample = default;
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

        sample = new TelemetrySample(
            Timestamp: DateTime.Now,
            Position: double.IsNaN(pos) ? 0.0 : pos,
            Velocity: double.IsNaN(vel) ? 0.0 : vel,
            Torque: double.IsNaN(tq) ? 0.0 : tq,
            Status: map.TryGetValue("status", out var st) ? st : "TEL",
            RunEnabled: ReadMapBool(map, "run", "run_enable", "enabled"),
            HasKinematics: true);

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

    private void AppendLog(string line)
    {
        var time = DateTime.Now.ToString("HH:mm:ss.fff", CultureInfo.InvariantCulture);
        var text = $"[{time}] {line}{Environment.NewLine}";

        _rawLog.AppendText(text);
        _rawLog.SelectionStart = _rawLog.TextLength;
        _rawLog.ScrollToCaret();

        if (_rawLog.TextLength > MaxLogChars)
        {
            _rawLog.Select(0, 60000);
            _rawLog.SelectedText = string.Empty;
        }
    }

    private void SaveCsv()
    {
        if (_history.Count == 0)
        {
            MessageBox.Show(this, "No telemetry samples yet.", "Save CSV", MessageBoxButtons.OK, MessageBoxIcon.Information);
            return;
        }

        using var saveDialog = new SaveFileDialog
        {
            Filter = "CSV files (*.csv)|*.csv|All files (*.*)|*.*",
            FileName = $"uart_telemetry_{DateTime.Now:yyyyMMdd_HHmmss}.csv"
        };

        if (saveDialog.ShowDialog(this) != DialogResult.OK)
        {
            return;
        }

        try
        {
            using var writer = new StreamWriter(saveDialog.FileName, false, Encoding.UTF8);
            writer.WriteLine("timestamp,position,velocity,torque,status");

            foreach (var sample in _history)
            {
                writer.WriteLine(
                    $"{sample.Timestamp:O}," +
                    $"{sample.Position.ToString(CultureInfo.InvariantCulture)}," +
                    $"{sample.Velocity.ToString(CultureInfo.InvariantCulture)}," +
                    $"{sample.Torque.ToString(CultureInfo.InvariantCulture)}," +
                    $"{sample.Status}");
            }

            AppendLog($"[APP] Saved CSV: {saveDialog.FileName}");
        }
        catch (Exception ex)
        {
            MessageBox.Show(this, ex.Message, "Save CSV failed", MessageBoxButtons.OK, MessageBoxIcon.Error);
        }
    }

    private readonly record struct TelemetrySample(
        DateTime Timestamp,
        double Position,
        double Velocity,
        double Torque,
        string Status,
        bool? RunEnabled,
        bool HasKinematics);
}

internal sealed class TelemetryPlotControl : Control
{
    private readonly List<PointF> _positionPoints = new();
    private readonly List<PointF> _velocityPoints = new();
    private readonly List<PointF> _torquePoints = new();

    private Color _positionColor = Color.FromArgb(62, 199, 255);
    private Color _velocityColor = Color.FromArgb(129, 237, 113);
    private Color _torqueColor = Color.FromArgb(255, 157, 64);

    public TelemetryPlotControl()
    {
        SetStyle(ControlStyles.AllPaintingInWmPaint |
                 ControlStyles.OptimizedDoubleBuffer |
                 ControlStyles.UserPaint |
                 ControlStyles.ResizeRedraw, true);
        BackColor = Color.FromArgb(20, 22, 30);
        ForeColor = Color.Gainsboro;
    }

    public void SetSeriesColors(Color position, Color velocity, Color torque)
    {
        _positionColor = position;
        _velocityColor = velocity;
        _torqueColor = torque;
        Invalidate();
    }

    public void AddPoint(int x, double position, double velocity, double torque, int maxPoints)
    {
        _positionPoints.Add(new PointF(x, (float)position));
        _velocityPoints.Add(new PointF(x, (float)velocity));
        _torquePoints.Add(new PointF(x, (float)torque));

        TrimTo(_positionPoints, maxPoints);
        TrimTo(_velocityPoints, maxPoints);
        TrimTo(_torquePoints, maxPoints);

        Invalidate();
    }

    private static void TrimTo(List<PointF> points, int maxPoints)
    {
        while (points.Count > maxPoints)
        {
            points.RemoveAt(0);
        }
    }

    protected override void OnPaint(PaintEventArgs e)
    {
        base.OnPaint(e);

        var g = e.Graphics;
        g.Clear(BackColor);
        g.SmoothingMode = System.Drawing.Drawing2D.SmoothingMode.AntiAlias;

        var plotRect = new RectangleF(52, 16, Width - 72, Height - 44);
        if (plotRect.Width <= 10 || plotRect.Height <= 10)
        {
            return;
        }

        using (var gridPen = new Pen(Color.FromArgb(46, 50, 62), 1f))
        {
            for (int i = 0; i <= 5; i++)
            {
                var y = plotRect.Top + i * plotRect.Height / 5f;
                g.DrawLine(gridPen, plotRect.Left, y, plotRect.Right, y);
            }

            for (int i = 0; i <= 8; i++)
            {
                var x = plotRect.Left + i * plotRect.Width / 8f;
                g.DrawLine(gridPen, x, plotRect.Top, x, plotRect.Bottom);
            }
        }

        var bounds = GetBounds();
        if (double.IsNaN(bounds.minY) || double.IsNaN(bounds.maxY) || Math.Abs(bounds.maxY - bounds.minY) < 1e-9)
        {
            DrawLegend(g);
            return;
        }

        using var axisPen = new Pen(Color.FromArgb(96, 102, 120), 1.2f);
        g.DrawRectangle(axisPen, plotRect.X, plotRect.Y, plotRect.Width, plotRect.Height);

        DrawSeries(g, plotRect, _positionPoints, _positionColor, bounds.minX, bounds.maxX, bounds.minY, bounds.maxY);
        DrawSeries(g, plotRect, _velocityPoints, _velocityColor, bounds.minX, bounds.maxX, bounds.minY, bounds.maxY);
        DrawSeries(g, plotRect, _torquePoints, _torqueColor, bounds.minX, bounds.maxX, bounds.minY, bounds.maxY);

        DrawLegend(g);
        DrawAxisLabels(g, plotRect, bounds.minY, bounds.maxY);
    }

    private (float minX, float maxX, float minY, float maxY) GetBounds()
    {
        var all = _positionPoints.Concat(_velocityPoints).Concat(_torquePoints).ToArray();
        if (all.Length == 0)
        {
            return (0, 1, float.NaN, float.NaN);
        }

        var minX = all.Min(p => p.X);
        var maxX = all.Max(p => p.X);
        if (Math.Abs(maxX - minX) < 1f)
        {
            maxX = minX + 1f;
        }

        var minY = all.Min(p => p.Y);
        var maxY = all.Max(p => p.Y);
        if (Math.Abs(maxY - minY) < 1e-3f)
        {
            maxY = minY + 1f;
        }

        return (minX, maxX, minY, maxY);
    }

    private static void DrawSeries(
        Graphics g,
        RectangleF rect,
        List<PointF> source,
        Color color,
        float minX,
        float maxX,
        float minY,
        float maxY)
    {
        if (source.Count < 2)
        {
            return;
        }

        var mapped = new PointF[source.Count];
        for (int i = 0; i < source.Count; i++)
        {
            var px = rect.Left + (source[i].X - minX) / (maxX - minX) * rect.Width;
            var py = rect.Bottom - (source[i].Y - minY) / (maxY - minY) * rect.Height;
            mapped[i] = new PointF(px, py);
        }

        using var pen = new Pen(color, 2f);
        g.DrawLines(pen, mapped);
    }

    private void DrawLegend(Graphics g)
    {
        DrawLegendItem(g, 60, 8, _positionColor, "Position");
        DrawLegendItem(g, 170, 8, _velocityColor, "Velocity");
        DrawLegendItem(g, 280, 8, _torqueColor, "Torque");
    }

    private static void DrawLegendItem(Graphics g, int x, int y, Color color, string text)
    {
        var font = SystemFonts.MessageBoxFont ?? Control.DefaultFont;
        using var brush = new SolidBrush(color);
        g.FillRectangle(brush, x, y + 4, 16, 4);
        using var txtBrush = new SolidBrush(Color.Gainsboro);
        g.DrawString(text, font, txtBrush, x + 22, y);
    }

    private static void DrawAxisLabels(Graphics g, RectangleF rect, float minY, float maxY)
    {
        var font = SystemFonts.MessageBoxFont ?? Control.DefaultFont;
        using var txtBrush = new SolidBrush(Color.FromArgb(180, 190, 206));
        g.DrawString(maxY.ToString("0.##", CultureInfo.InvariantCulture), font, txtBrush, 6, rect.Top - 8);
        g.DrawString(minY.ToString("0.##", CultureInfo.InvariantCulture), font, txtBrush, 6, rect.Bottom - 8);
        g.DrawString("Samples", font, txtBrush, rect.Right - 52, rect.Bottom + 6);
    }
}
