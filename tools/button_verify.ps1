param(
    [string]$BaseUrl = "http://127.0.0.1:5088",
    [string]$PortName = "COM15",
    [int]$BaudRate = 115200
)

$ErrorActionPreference = "Stop"
$reportPath = Join-Path $PSScriptRoot "button_verify_report.txt"
$results = New-Object System.Collections.Generic.List[object]

function Invoke-Api {
    param(
        [string]$Method,
        [string]$Path,
        [object]$Body
    )

    if ($null -ne $Body) {
        return Invoke-RestMethod -Uri ("$BaseUrl$Path") -Method $Method -ContentType "application/json" -Body ($Body | ConvertTo-Json -Compress)
    }

    return Invoke-RestMethod -Uri ("$BaseUrl$Path") -Method $Method
}

function Add-Result {
    param(
        [string]$Step,
        [string]$Result,
        [string]$Error = ""
    )

    $results.Add([pscustomobject]@{
        Step = $Step
        Result = $Result
        Error = $Error
    }) | Out-Null
}

function Post-Cmd {
    param([string]$Cmd)

    try {
        Invoke-Api -Method "POST" -Path "/api/cmd" -Body @{ command = $Cmd } | Out-Null
        Add-Result -Step $Cmd -Result "OK"
    }
    catch {
        Add-Result -Step $Cmd -Result "FAIL" -Error $_.Exception.Message
    }

    Start-Sleep -Milliseconds 120
}

try {
    Invoke-Api -Method "POST" -Path "/api/disconnect" -Body $null | Out-Null
}
catch {
}

try {
    Invoke-Api -Method "POST" -Path "/api/connect" -Body @{ portName = $PortName; baudRate = $BaudRate } | Out-Null
    Add-Result -Step "CONNECT $PortName@$BaudRate" -Result "OK"
}
catch {
    Add-Result -Step "CONNECT $PortName@$BaudRate" -Result "FAIL" -Error $_.Exception.Message
}

Start-Sleep -Milliseconds 250

$cmds = @(
    "CMD,set_home=1",
    "CMD,run=1",
    "CMD,jog_delta=100",
    "CMD,jog_delta=-100",
    "CMD,manual_pos=1234",
    "CMD,manual_speed=150",
    "CMD,manual_torque=15",
    "CMD,manual_abs=1",
    "CMD,manual_apply=1",
    "CMD,target_abs=1234",
    "CMD,manual_stop=1",
    "CMD,manual_save=1",
    "CMD,manual_load=1",
    "CMD,param_jog_speed=4321",
    "CMD,param_acc=222",
    "CMD,param_dec=333",
    "CMD,param_limit_plus=44444",
    "CMD,param_limit_minus=-55555",
    "CMD,param_unit_scale=1",
    "CMD,param_home_offset=77",
    "CMD,param_position_gain=9",
    "CMD,param_apply=1",
    "CMD,param_write_all=1",
    "CMD,param_read_all=1",
    "CMD,param_save=1",
    "CMD,param_load=1",
    "CMD,prog_pos1=10",
    "CMD,prog_pos2=20",
    "CMD,prog_pos3=30",
    "CMD,prog_speed1=120",
    "CMD,prog_speed2=130",
    "CMD,prog_speed3=140",
    "CMD,prog_torque1=11",
    "CMD,prog_torque2=12",
    "CMD,prog_torque3=13",
    "CMD,prog_return_speed=115",
    "CMD,prog_delay_ms=50",
    "CMD,program_apply=1",
    "CMD,program_save=1",
    "CMD,program_load=1",
    "CMD,program_start=1",
    "CMD,program_stop=1",
    "CMD,cfg_read=1",
    "CMD,run=0"
)

foreach ($cmd in $cmds) {
    Post-Cmd -Cmd $cmd
}

Start-Sleep -Milliseconds 700
$status = Invoke-Api -Method "GET" -Path "/api/status" -Body $null

$fail = $results | Where-Object { $_.Result -eq "FAIL" }
$pass = $results | Where-Object { $_.Result -eq "OK" }

$lines = New-Object System.Collections.Generic.List[string]
$lines.Add(("TOTAL={0} PASS={1} FAIL={2}" -f $results.Count, $pass.Count, $fail.Count)) | Out-Null

if ($fail.Count -gt 0) {
    $lines.Add("FAIL_LIST:") | Out-Null
    foreach ($f in $fail) {
        $lines.Add((" - {0} :: {1}" -f $f.Step, $f.Error)) | Out-Null
    }
}

$lines.Add(("TELEMETRY run={0} sw={1} pos={2} vel={3} tq={4}" -f $status.telemetry.runEnabled, $status.telemetry.status, $status.telemetry.position, $status.telemetry.velocity, $status.telemetry.torque)) | Out-Null
$lines.Add(("MANUAL pos={0} speed={1} tq={2} abs={3}" -f $status.settings.manual.position, $status.settings.manual.speed, $status.settings.manual.torque, $status.settings.manual.absMode)) | Out-Null
$lines.Add(("PARAM jog={0} acc={1} dec={2} limPlus={3} limMinus={4} unit={5} home={6} gain={7}" -f $status.settings.parameter.jogSpeed, $status.settings.parameter.acc, $status.settings.parameter.dec, $status.settings.parameter.limitPlus, $status.settings.parameter.limitMinus, $status.settings.parameter.unitScale, $status.settings.parameter.homeOffset, $status.settings.parameter.positionGain)) | Out-Null
$lines.Add(("PROGRAM p1={0} p2={1} p3={2} s1={3} s2={4} s3={5} t1={6} t2={7} t3={8} ret={9} dly={10}" -f $status.settings.program.pos1, $status.settings.program.pos2, $status.settings.program.pos3, $status.settings.program.speed1, $status.settings.program.speed2, $status.settings.program.speed3, $status.settings.program.torque1, $status.settings.program.torque2, $status.settings.program.torque3, $status.settings.program.returnSpeed, $status.settings.program.delayMs)) | Out-Null

$lines.Add("LOG_HINTS_BEGIN") | Out-Null
$pattern = "CMD,set_home=1|CMD,run=1|CMD,manual_apply=1|CMD,param_apply=1|param_read_all|CMD,program_apply=1|CMD,program_start=1|CMD,program_stop=1|CMD,run=0|\\[CMD\\]|CIA402"
$hits = $status.logs | Select-String -Pattern $pattern | Select-Object -Last 60
foreach ($h in $hits) {
    $lines.Add($h.Line) | Out-Null
}
$lines.Add("LOG_HINTS_END") | Out-Null

[System.IO.File]::WriteAllLines($reportPath, $lines, [System.Text.Encoding]::UTF8)
Write-Output ("REPORT_WRITTEN {0}" -f $reportPath)
