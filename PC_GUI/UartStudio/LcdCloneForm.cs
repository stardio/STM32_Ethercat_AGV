using System.Globalization;

namespace UartStudio;

internal sealed class LcdCloneForm : Form
{
    private readonly Action<string> _sendCommand;

    private readonly Label _connectionLabel = new();
    private readonly Label _positionValue = new();
    private readonly Label _velocityValue = new();
    private readonly Label _torqueValue = new();
    private readonly Label _statusValue = new();

    private readonly CheckBox _runCheck = new();
    private bool _suppressRunChange;

    private readonly NumericUpDown _jogStepInput = CreateIntInput(1000);
    private readonly NumericUpDown _manualPosInput = CreateIntInput(0);
    private readonly NumericUpDown _manualSpeedInput = CreateIntInput(100, 1, int.MaxValue);
    private readonly NumericUpDown _manualTorqueInput = CreateIntInput(20, 1, 100);
    private readonly CheckBox _manualAbsCheck = new();

    private readonly NumericUpDown _paramJogSpeed = CreateIntInput(1000, 1, int.MaxValue);
    private readonly NumericUpDown _paramAcc = CreateIntInput(100, 0, int.MaxValue);
    private readonly NumericUpDown _paramDec = CreateIntInput(100, 0, int.MaxValue);
    private readonly NumericUpDown _paramLimitPlus = CreateIntInput(100000);
    private readonly NumericUpDown _paramLimitMinus = CreateIntInput(-100000);
    private readonly NumericUpDown _paramGearRatio = CreateIntInput(1, 1, int.MaxValue);
    private readonly NumericUpDown _paramBallLead  = CreateIntInput(1, 1, int.MaxValue);
    private readonly NumericUpDown _paramEncRes    = CreateIntInput(131072, 1, int.MaxValue);
    private readonly NumericUpDown _paramHomeOffset = CreateIntInput(0);
    private readonly NumericUpDown _paramPositionGain = CreateIntInput(0, 0, int.MaxValue);

    private readonly NumericUpDown _progPos1 = CreateIntInput(0);
    private readonly NumericUpDown _progPos2 = CreateIntInput(0);
    private readonly NumericUpDown _progPos3 = CreateIntInput(0);
    private readonly NumericUpDown _progSpeed1 = CreateIntInput(100, 1, int.MaxValue);
    private readonly NumericUpDown _progSpeed2 = CreateIntInput(100, 1, int.MaxValue);
    private readonly NumericUpDown _progSpeed3 = CreateIntInput(100, 1, int.MaxValue);
    private readonly NumericUpDown _progTorque1 = CreateIntInput(20, 1, 100);
    private readonly NumericUpDown _progTorque2 = CreateIntInput(20, 1, 100);
    private readonly NumericUpDown _progTorque3 = CreateIntInput(20, 1, 100);
    private readonly NumericUpDown _progReturnSpeed = CreateIntInput(100, 1, int.MaxValue);
    private readonly NumericUpDown _progDelayMs = CreateIntInput(0, 0, 1000000);

    private readonly System.Windows.Forms.Timer _jogTimer = new();
    private int _jogDirection;

    public LcdCloneForm(Action<string> sendCommand, bool initialConnected)
    {
        _sendCommand = sendCommand;

        Text = "LCD UI Clone";
        Width = 1080;
        Height = 820;
        MinimumSize = new Size(980, 720);
        StartPosition = FormStartPosition.CenterParent;
        Font = new Font("Segoe UI", 10F, FontStyle.Regular, GraphicsUnit.Point);
        BackColor = Color.FromArgb(24, 28, 36);

        BuildUi();
        WireEvents();
        SetConnectionState(initialConnected);
    }

    public void SetConnectionState(bool connected)
    {
        _connectionLabel.Text = connected ? "UART: Connected" : "UART: Disconnected";
        _connectionLabel.ForeColor = connected ? Color.FromArgb(130, 236, 130) : Color.FromArgb(255, 155, 155);
    }

    public void UpdateTelemetry(double position, double velocity, double torque, string status, bool? runEnabled)
    {
        if (InvokeRequired)
        {
            BeginInvoke((Action)(() => UpdateTelemetry(position, velocity, torque, status, runEnabled)));
            return;
        }

        _positionValue.Text = position.ToString("0.###", CultureInfo.InvariantCulture);
        _velocityValue.Text = velocity.ToString("0.###", CultureInfo.InvariantCulture);
        _torqueValue.Text = torque.ToString("0.###", CultureInfo.InvariantCulture);
        _statusValue.Text = status;

        if (runEnabled.HasValue)
        {
            _suppressRunChange = true;
            _runCheck.Checked = runEnabled.Value;
            _suppressRunChange = false;
        }
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
        root.RowStyles.Add(new RowStyle(SizeType.Absolute, 96F));
        root.RowStyles.Add(new RowStyle(SizeType.Percent, 100F));
        Controls.Add(root);

        var topCard = new Panel
        {
            Dock = DockStyle.Fill,
            BackColor = Color.FromArgb(34, 39, 50),
            Padding = new Padding(12)
        };
        root.Controls.Add(topCard, 0, 0);

        var topFlow = new FlowLayoutPanel
        {
            Dock = DockStyle.Fill,
            WrapContents = false,
            AutoScroll = true,
            BackColor = Color.Transparent,
            FlowDirection = FlowDirection.LeftToRight
        };
        topCard.Controls.Add(topFlow);

        _connectionLabel.AutoSize = true;
        _connectionLabel.Margin = new Padding(0, 8, 24, 0);
        _connectionLabel.Font = new Font("Segoe UI", 10F, FontStyle.Bold);

        topFlow.Controls.Add(_connectionLabel);
        topFlow.Controls.Add(CreateLiveMetric("Position", _positionValue));
        topFlow.Controls.Add(CreateLiveMetric("Velocity", _velocityValue));
        topFlow.Controls.Add(CreateLiveMetric("Torque", _torqueValue));
        topFlow.Controls.Add(CreateLiveMetric("State", _statusValue, 180));

        var tabs = new TabControl
        {
            Dock = DockStyle.Fill,
            Font = new Font("Segoe UI", 10F, FontStyle.Regular, GraphicsUnit.Point)
        };
        tabs.TabPages.Add(BuildHomeTab());
        tabs.TabPages.Add(BuildMainTab());
        tabs.TabPages.Add(BuildManualTab());
        tabs.TabPages.Add(BuildParameterTab());
        tabs.TabPages.Add(BuildProgramTab());
        root.Controls.Add(tabs, 0, 1);
    }

    private static Panel CreateLiveMetric(string title, Label valueLabel, int width = 120)
    {
        var panel = new Panel { Width = width, Height = 56, Margin = new Padding(0, 0, 18, 0) };

        var titleLabel = new Label
        {
            Text = title,
            ForeColor = Color.FromArgb(176, 190, 214),
            AutoSize = true,
            Location = new Point(0, 2)
        };

        valueLabel.ForeColor = Color.WhiteSmoke;
        valueLabel.Font = new Font("Segoe UI", 11F, FontStyle.Bold, GraphicsUnit.Point);
        valueLabel.Text = "-";
        valueLabel.AutoSize = true;
        valueLabel.Location = new Point(0, 24);

        panel.Controls.Add(titleLabel);
        panel.Controls.Add(valueLabel);
        return panel;
    }

    private TabPage BuildHomeTab()
    {
        var tab = NewTab("Home");
        var layout = NewContentLayout();
        tab.Controls.Add(layout);

        layout.Controls.Add(CreateHint("HomePage: CP ORG Reset = 현재 위치를 Home으로 저장"));

        var setHomeButton = NewButton("CP ORG Reset (Set Home)", (s, e) => Send("CMD,set_home=1"));
        setHomeButton.Width = 240;
        layout.Controls.Add(setHomeButton);

        return tab;
    }

    private TabPage BuildMainTab()
    {
        var tab = NewTab("Main");
        var layout = NewContentLayout();
        tab.Controls.Add(layout);

        layout.Controls.Add(CreateHint("MainPage: Servo ON 토글은 TouchGFX와 동일하게 Run Enable 제어"));

        _runCheck.Text = "Servo ON (RUN)";
        _runCheck.AutoSize = true;
        _runCheck.ForeColor = Color.WhiteSmoke;
        _runCheck.Margin = new Padding(4, 8, 4, 8);
        layout.Controls.Add(_runCheck);

        var row = NewButtonRow();
        row.Controls.Add(NewButton("Run ON", (s, e) => Send("CMD,run=1")));
        row.Controls.Add(NewButton("Run OFF", (s, e) => Send("CMD,run=0")));
        layout.Controls.Add(row);

        return tab;
    }

    private TabPage BuildManualTab()
    {
        var tab = NewTab("Manual");
        var layout = NewContentLayout();
        tab.Controls.Add(layout);

        layout.Controls.Add(CreateHint("ManualPage: Jog(FWD/REV), One-Cycle(ABS/INC), Save/Load"));

        layout.Controls.Add(NewFieldRow("Jog Step", _jogStepInput));

        var jogRow = NewButtonRow();
        var jogFwd = NewButton("Jog FWD", null);
        var jogRev = NewButton("Jog REV", null);
        jogFwd.MouseDown += (_, _) => StartJog(+1);
        jogFwd.MouseUp += (_, _) => StopJog();
        jogFwd.MouseLeave += (_, _) => StopJog();
        jogRev.MouseDown += (_, _) => StartJog(-1);
        jogRev.MouseUp += (_, _) => StopJog();
        jogRev.MouseLeave += (_, _) => StopJog();
        jogRow.Controls.Add(jogFwd);
        jogRow.Controls.Add(jogRev);
        layout.Controls.Add(jogRow);

        layout.Controls.Add(NewFieldRow("Cycle Position", _manualPosInput));
        layout.Controls.Add(NewFieldRow("Cycle Speed", _manualSpeedInput));
        layout.Controls.Add(NewFieldRow("Cycle Torque(%)", _manualTorqueInput));

        _manualAbsCheck.Text = "ABS Mode (off = INC)";
        _manualAbsCheck.AutoSize = true;
        _manualAbsCheck.ForeColor = Color.WhiteSmoke;
        _manualAbsCheck.Checked = true;
        _manualAbsCheck.Margin = new Padding(4, 8, 4, 8);
        layout.Controls.Add(_manualAbsCheck);

        var startStopRow = NewButtonRow();
        startStopRow.Controls.Add(NewButton("Manual Start", (s, e) => SendManualStart()));
        startStopRow.Controls.Add(NewButton("Manual Stop", (s, e) => Send("CMD,manual_stop=1")));
        layout.Controls.Add(startStopRow);

        var persistRow = NewButtonRow();
        persistRow.Controls.Add(NewButton("Save Manual", (s, e) => Send("CMD,manual_save=1")));
        persistRow.Controls.Add(NewButton("Load Manual", (s, e) => Send("CMD,manual_load=1")));
        layout.Controls.Add(persistRow);

        return tab;
    }

    private TabPage BuildParameterTab()
    {
        var tab = NewTab("Parameter");
        var layout = NewContentLayout();
        tab.Controls.Add(layout);

        layout.Controls.Add(CreateHint("ParameterPage: 8개 파라미터 편집 + Write All / Read All / Save / Load"));

        layout.Controls.Add(NewFieldRow("Jog Speed", _paramJogSpeed));
        layout.Controls.Add(NewFieldRow("Acc Time", _paramAcc));
        layout.Controls.Add(NewFieldRow("Dec Time", _paramDec));
        layout.Controls.Add(NewFieldRow("Limit Plus", _paramLimitPlus));
        layout.Controls.Add(NewFieldRow("Limit Minus", _paramLimitMinus));
        layout.Controls.Add(NewFieldRow("감속비 (Gear Ratio)", _paramGearRatio));
        layout.Controls.Add(NewFieldRow("볼스크류 리드 (mm)", _paramBallLead));
        layout.Controls.Add(NewFieldRow("엔코더 분해능 (cts/rev)", _paramEncRes));
        layout.Controls.Add(NewFieldRow("Home Offset", _paramHomeOffset));
        layout.Controls.Add(NewFieldRow("Position Gain", _paramPositionGain));

        var row1 = NewButtonRow();
        row1.Controls.Add(NewButton("Write All", (s, e) => SendParameterWriteAll()));
        row1.Controls.Add(NewButton("Read All", (s, e) => Send("CMD,param_read_all=1")));
        layout.Controls.Add(row1);

        var row2 = NewButtonRow();
        row2.Controls.Add(NewButton("Save Parameter", (s, e) => Send("CMD,param_save=1")));
        row2.Controls.Add(NewButton("Load Parameter", (s, e) => Send("CMD,param_load=1")));
        layout.Controls.Add(row2);

        return tab;
    }

    private TabPage BuildProgramTab()
    {
        var tab = NewTab("Program");
        var layout = NewContentLayout();
        tab.Controls.Add(layout);

        layout.Controls.Add(CreateHint("ProgramPage: Pos1~3, Speed1~3, Torque1~3, ReturnSpeed, Delay(ms)"));

        layout.Controls.Add(NewFieldRow("Target Pos1", _progPos1));
        layout.Controls.Add(NewFieldRow("Target Pos2", _progPos2));
        layout.Controls.Add(NewFieldRow("Target Pos3", _progPos3));
        layout.Controls.Add(NewFieldRow("Target Speed1", _progSpeed1));
        layout.Controls.Add(NewFieldRow("Target Speed2", _progSpeed2));
        layout.Controls.Add(NewFieldRow("Target Speed3", _progSpeed3));
        layout.Controls.Add(NewFieldRow("Target Torque1", _progTorque1));
        layout.Controls.Add(NewFieldRow("Target Torque2", _progTorque2));
        layout.Controls.Add(NewFieldRow("Target Torque3", _progTorque3));
        layout.Controls.Add(NewFieldRow("Return Speed", _progReturnSpeed));
        layout.Controls.Add(NewFieldRow("Delay (ms)", _progDelayMs));

        var row1 = NewButtonRow();
        row1.Controls.Add(NewButton("Apply Values", (s, e) => SendProgramValues()));
        row1.Controls.Add(NewButton("Program Start", (s, e) =>
        {
            SendProgramValues();
            Send("CMD,program_start=1");
        }));
        row1.Controls.Add(NewButton("Program Stop", (s, e) => Send("CMD,program_stop=1")));
        layout.Controls.Add(row1);

        var row2 = NewButtonRow();
        row2.Controls.Add(NewButton("Save Program", (s, e) => Send("CMD,program_save=1")));
        row2.Controls.Add(NewButton("Load Program", (s, e) => Send("CMD,program_load=1")));
        layout.Controls.Add(row2);

        return tab;
    }

    private static TabPage NewTab(string title)
    {
        return new TabPage(title)
        {
            BackColor = Color.FromArgb(30, 34, 44),
            ForeColor = Color.WhiteSmoke,
            Padding = new Padding(10)
        };
    }

    private static FlowLayoutPanel NewContentLayout()
    {
        return new FlowLayoutPanel
        {
            Dock = DockStyle.Fill,
            AutoScroll = true,
            WrapContents = true,
            FlowDirection = FlowDirection.LeftToRight,
            BackColor = Color.Transparent
        };
    }

    private static Label CreateHint(string text)
    {
        return new Label
        {
            Text = text,
            ForeColor = Color.FromArgb(176, 190, 214),
            Font = new Font("Segoe UI", 9.5F, FontStyle.Regular, GraphicsUnit.Point),
            AutoSize = true,
            Margin = new Padding(4, 4, 4, 12)
        };
    }

    private static Panel NewFieldRow(string label, NumericUpDown input)
    {
        var panel = new Panel
        {
            Width = 460,
            Height = 42,
            Margin = new Padding(4, 4, 12, 4)
        };

        var lbl = new Label
        {
            Text = label,
            Width = 180,
            ForeColor = Color.WhiteSmoke,
            Location = new Point(2, 10)
        };

        input.Location = new Point(190, 6);
        input.Width = 240;

        panel.Controls.Add(lbl);
        panel.Controls.Add(input);
        return panel;
    }

    private static FlowLayoutPanel NewButtonRow()
    {
        return new FlowLayoutPanel
        {
            Width = 930,
            Height = 46,
            Margin = new Padding(4, 10, 4, 8),
            FlowDirection = FlowDirection.LeftToRight,
            WrapContents = false,
            BackColor = Color.Transparent
        };
    }

    private static Button NewButton(string text, EventHandler? onClick)
    {
        var button = new Button
        {
            Text = text,
            Width = 170,
            Height = 36,
            FlatStyle = FlatStyle.Flat,
            BackColor = Color.FromArgb(64, 194, 255),
            ForeColor = Color.Black,
            Margin = new Padding(0, 0, 10, 0)
        };
        button.FlatAppearance.BorderColor = Color.FromArgb(49, 137, 176);

        if (onClick is not null)
        {
            button.Click += onClick;
        }

        return button;
    }

    private static NumericUpDown CreateIntInput(int initialValue, int minValue = int.MinValue, int maxValue = int.MaxValue)
    {
        var input = new NumericUpDown
        {
            DecimalPlaces = 0,
            ThousandsSeparator = true,
            Minimum = minValue,
            Maximum = maxValue,
            Value = Math.Max(minValue, Math.Min(maxValue, initialValue)),
            BackColor = Color.FromArgb(14, 18, 25),
            ForeColor = Color.WhiteSmoke,
            BorderStyle = BorderStyle.FixedSingle
        };
        return input;
    }

    private void WireEvents()
    {
        _runCheck.CheckedChanged += (_, _) =>
        {
            if (_suppressRunChange)
            {
                return;
            }

            Send(_runCheck.Checked ? "CMD,run=1" : "CMD,run=0");
        };

        _jogTimer.Interval = 120;
        _jogTimer.Tick += (_, _) =>
        {
            if (_jogDirection == 0)
            {
                return;
            }

            SendJogDelta(_jogDirection);
        };

        FormClosing += (_, _) =>
        {
            _jogTimer.Stop();
            _jogDirection = 0;
        };
    }

    private void StartJog(int direction)
    {
        _jogDirection = direction;
        SendJogDelta(direction);
        _jogTimer.Start();
    }

    private void StopJog()
    {
        _jogDirection = 0;
        _jogTimer.Stop();
    }

    private void SendJogDelta(int direction)
    {
        var step = ToInt32(_jogStepInput.Value);
        var delta = step * direction;
        Send($"CMD,jog_delta={delta}");
    }

    private void SendManualStart()
    {
        var position = ToInt32(_manualPosInput.Value);
        var speed = Math.Max(1, ToInt32(_manualSpeedInput.Value));
        var torque = Math.Clamp(ToInt32(_manualTorqueInput.Value), 1, 100);

        Send($"CMD,manual_speed={speed}");
        Send($"CMD,manual_torque={torque}");
        Send($"CMD,manual_abs={( _manualAbsCheck.Checked ? 1 : 0 )}");

        if (_manualAbsCheck.Checked)
        {
            Send($"CMD,target_abs={position}");
        }
        else
        {
            Send($"CMD,target_delta={position}");
        }
    }

    private void SendParameterWriteAll()
    {
        Send($"CMD,param_jog_speed={ToInt32(_paramJogSpeed.Value)}");
        Send($"CMD,param_acc={ToInt32(_paramAcc.Value)}");
        Send($"CMD,param_dec={ToInt32(_paramDec.Value)}");
        Send($"CMD,param_limit_plus={ToInt32(_paramLimitPlus.Value)}");
        Send($"CMD,param_limit_minus={ToInt32(_paramLimitMinus.Value)}");
        Send($"CMD,param_gear_ratio={ToInt32(_paramGearRatio.Value)}");
        Send($"CMD,param_lead={ToInt32(_paramBallLead.Value)}");
        Send($"CMD,param_enc_res={ToInt32(_paramEncRes.Value)}");
        Send($"CMD,param_home_offset={ToInt32(_paramHomeOffset.Value)}");
        Send($"CMD,param_position_gain={ToInt32(_paramPositionGain.Value)}");
        Send("CMD,param_write_all=1");
    }

    private void SendProgramValues()
    {
        Send($"CMD,prog_pos1={ToInt32(_progPos1.Value)}");
        Send($"CMD,prog_pos2={ToInt32(_progPos2.Value)}");
        Send($"CMD,prog_pos3={ToInt32(_progPos3.Value)}");
        Send($"CMD,prog_speed1={ToInt32(_progSpeed1.Value)}");
        Send($"CMD,prog_speed2={ToInt32(_progSpeed2.Value)}");
        Send($"CMD,prog_speed3={ToInt32(_progSpeed3.Value)}");
        Send($"CMD,prog_torque1={ToInt32(_progTorque1.Value)}");
        Send($"CMD,prog_torque2={ToInt32(_progTorque2.Value)}");
        Send($"CMD,prog_torque3={ToInt32(_progTorque3.Value)}");
        Send($"CMD,prog_return_speed={ToInt32(_progReturnSpeed.Value)}");
        Send($"CMD,prog_delay_ms={ToInt32(_progDelayMs.Value)}");
    }

    private void Send(string command)
    {
        _sendCommand(command);
    }

    private static int ToInt32(decimal value)
    {
        return decimal.ToInt32(decimal.Truncate(value));
    }
}
