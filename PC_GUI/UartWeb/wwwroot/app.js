const els = {
  portSelect: document.getElementById('portSelect'),
  baudSelect: document.getElementById('baudSelect'),
  refreshPortsBtn: document.getElementById('refreshPortsBtn'),
  connectBtn: document.getElementById('connectBtn'),
  readAllBtn: document.getElementById('readAllBtn'),
  demoBtn: document.getElementById('demoBtn'),
  connState: document.getElementById('connState'),

  mPos: document.getElementById('mPos'),
  mVel: document.getElementById('mVel'),
  mTor: document.getElementById('mTor'),
  mStatus: document.getElementById('mStatus'),
  mRun: document.getElementById('mRun'),
  logBox: document.getElementById('logBox'),
  logPauseBtn: document.getElementById('logPauseBtn'),
  logClearBtn: document.getElementById('logClearBtn'),
  logCopyBtn: document.getElementById('logCopyBtn'),
  logPauseLabel: document.getElementById('logPauseLabel'),

  /* Status strip */
  psMode: document.getElementById('psMode'),
  psStep: document.getElementById('psStep'),
  ilkEstop: document.getElementById('ilkEstop'),
  ilkDoor: document.getElementById('ilkDoor'),
  ilkDrive: document.getElementById('ilkDrive'),
  ilkHome: document.getElementById('ilkHome'),
  cntTotal: document.getElementById('cntTotal'),
  cntOk: document.getElementById('cntOk'),
  cntNg: document.getElementById('cntNg'),
  cntRate: document.getElementById('cntRate'),
  cntCng: document.getElementById('cntCng'),
  activeAlarm: document.getElementById('activeAlarm'),

  tabs: document.querySelectorAll('.tab'),
  panels: {
    home: document.getElementById('panel-home'),
    manual: document.getElementById('panel-manual'),
    parameter: document.getElementById('panel-parameter'),
    program: document.getElementById('panel-program'),
    press: document.getElementById('panel-press'),
    quality: document.getElementById('panel-quality'),
    alarm: document.getElementById('panel-alarm'),
    maintenance: document.getElementById('panel-maintenance'),
    db: document.getElementById('panel-db'),
    log: document.getElementById('panel-log'),
  },
  bottomSections: {
    home: document.getElementById('bottom-home'),
    manual: document.getElementById('bottom-manual'),
    parameter: document.getElementById('bottom-parameter'),
    program: document.getElementById('bottom-program'),
    press: document.getElementById('bottom-press'),
    quality: document.getElementById('bottom-quality'),
    alarm: document.getElementById('bottom-alarm'),
    maintenance: document.getElementById('bottom-maintenance'),
    db: document.getElementById('bottom-db'),
    log: document.getElementById('bottom-log'),
  },

  setHomeBtn: document.getElementById('setHomeBtn'),
  runToggle: document.getElementById('runToggle'),
  runOnBtn: document.getElementById('runOnBtn'),
  runOffBtn: document.getElementById('runOffBtn'),

  manualAbs: document.getElementById('manualAbs'),
  manualPos: document.getElementById('manualPos'),
  manualSpeed: document.getElementById('manualSpeed'),
  manualTorque: document.getElementById('manualTorque'),
  jogRevBtn: document.getElementById('jogRevBtn'),
  jogFwdBtn: document.getElementById('jogFwdBtn'),
  manualApplyBtn: document.getElementById('manualApplyBtn'),
  manualStartBtn: document.getElementById('manualStartBtn'),
  manualStopBtn: document.getElementById('manualStopBtn'),
  manualSaveBtn: document.getElementById('manualSaveBtn'),
  manualLoadBtn: document.getElementById('manualLoadBtn'),

  pJogSpeed: document.getElementById('pJogSpeed'),
  pAcc: document.getElementById('pAcc'),
  pDec: document.getElementById('pDec'),
  pLimitPlus:          document.getElementById('pLimitPlus'),
  pLimitMinus:         document.getElementById('pLimitMinus'),
  captureLimitPlusBtn: document.getElementById('captureLimitPlusBtn'),
  captureLimitMinusBtn:document.getElementById('captureLimitMinusBtn'),
  pGearRatio: document.getElementById('pGearRatio'),
  pBallLead:  document.getElementById('pBallLead'),
  pEncRes:    document.getElementById('pEncRes'),
  pUnitScale: document.getElementById('pUnitScale'),
  pHomeOffset: document.getElementById('pHomeOffset'),
  pPosGain: document.getElementById('pPosGain'),
  paramSaveBtn: document.getElementById('paramSaveBtn'),
  paramLoadBtn: document.getElementById('paramLoadBtn'),

  gPos1: document.getElementById('gPos1'),
  gPos2: document.getElementById('gPos2'),
  gPos3: document.getElementById('gPos3'),
  gSpeed1: document.getElementById('gSpeed1'),
  gSpeed2: document.getElementById('gSpeed2'),
  gSpeed3: document.getElementById('gSpeed3'),
  gTorque1: document.getElementById('gTorque1'),
  gTorque2: document.getElementById('gTorque2'),
  gTorque3: document.getElementById('gTorque3'),
  gReturnSpeed: document.getElementById('gReturnSpeed'),
  gCycleRepeat: document.getElementById('gCycleRepeat'),
  gInterCycleDelay: document.getElementById('gInterCycleDelay'),
  cycleRepeatProgress: document.getElementById('cycleRepeatProgress'),
  gDelay1Ms: document.getElementById('gDelay1Ms'),
  gDelay2Ms: document.getElementById('gDelay2Ms'),
  gDelay3Ms: document.getElementById('gDelay3Ms'),
  programApplyBtn: document.getElementById('programApplyBtn'),
  programStartBtn: document.getElementById('programStartBtn'),
  programStopBtn: document.getElementById('programStopBtn'),
  programSaveBtn: document.getElementById('programSaveBtn'),
  programLoadBtn: document.getElementById('programLoadBtn'),

  /* Maintenance */
  loginPin: document.getElementById('loginPin'),
  loginBtn: document.getElementById('loginBtn'),
  logoutBtn: document.getElementById('logoutBtn'),
  userLevelName: document.getElementById('userLevelName'),
  userLevelBadge: document.getElementById('userLevelBadge'),
  maintTotal: document.getElementById('maintTotal'),
  maintSince: document.getElementById('maintSince'),
  maintThresh: document.getElementById('maintThresh'),
  maintAlert: document.getElementById('maintAlert'),
  maintThreshInput: document.getElementById('maintThreshInput'),
  maintRefreshBtn: document.getElementById('maintRefreshBtn'),
  maintResetBtn: document.getElementById('maintResetBtn'),
  maintSetThreshBtn: document.getElementById('maintSetThreshBtn'),
  sysMode: document.getElementById('sysMode'),
  sysStep: document.getElementById('sysStep'),
  sysDrive: document.getElementById('sysDrive'),
  sysHome: document.getElementById('sysHome'),
  sysEstop: document.getElementById('sysEstop'),
  sysDoor: document.getElementById('sysDoor'),

  /* Cycle result panel */
  cycleResultPanel: document.getElementById('cycleResultPanel'),
  crIcon:           document.getElementById('crIcon'),
  crLabel:          document.getElementById('crLabel'),
  crDetail:         document.getElementById('crDetail'),
  ngResetBtn:       document.getElementById('ngResetBtn'),

  /* AUTO ready lamp */
  autoReadyLamp: document.getElementById('autoReadyLamp'),
  readyIcon:     document.getElementById('readyIcon'),
  readyLabel:    document.getElementById('readyLabel'),
  rcEstop:       document.getElementById('rcEstop'),
  rcDoor:        document.getElementById('rcDoor'),
  rcDrive:       document.getElementById('rcDrive'),
  rcHome:        document.getElementById('rcHome'),

  /* Press control */
  modeManuBtn: document.getElementById('modeManuBtn'),
  modeSetupBtn: document.getElementById('modeSetupBtn'),
  modeAutoBtn: document.getElementById('modeAutoBtn'),
  cycleStartBtn: document.getElementById('cycleStartBtn'),
  cycleStopBtn: document.getElementById('cycleStopBtn'),
  cycleAbortBtn: document.getElementById('cycleAbortBtn'),
  pressApSpd: document.getElementById('pressApSpd'),
  pressApPos: document.getElementById('pressApPos'),
  pressCntSpd: document.getElementById('pressCntSpd'),
  pressCntTh: document.getElementById('pressCntTh'),
  pressPrSpd: document.getElementById('pressPrSpd'),
  pressPrPos: document.getElementById('pressPrPos'),
  pressPrForce: document.getElementById('pressPrForce'),
  pressDwell: document.getElementById('pressDwell'),
  pressRetSpd: document.getElementById('pressRetSpd'),
  pressRetPos: document.getElementById('pressRetPos'),
  pressTimeout: document.getElementById('pressTimeout'),
  pressSaveBtn:  document.getElementById('pressSaveBtn'),
  pressLoadBtn:     document.getElementById('pressLoadBtn'),
  pressGraphCanvas: document.getElementById('pressGraphCanvas'),
  judgeForceMax: document.getElementById('judgeForceMax'),
  judgeForceMin: document.getElementById('judgeForceMin'),
  judgePosMax: document.getElementById('judgePosMax'),
  judgePosMin: document.getElementById('judgePosMin'),
  judgeApplyBtn: document.getElementById('judgeApplyBtn'),
  simEstop: document.getElementById('simEstop'),
  simDoor: document.getElementById('simDoor'),
  simHome: document.getElementById('simHome'),
  simApplyBtn: document.getElementById('simApplyBtn'),
  counterResetBtn: document.getElementById('counterResetBtn'),
  rstCycle: document.getElementById('rstCycle'),
  rstResult: document.getElementById('rstResult'),
  rstForce: document.getElementById('rstForce'),
  rstPos: document.getElementById('rstPos'),
  rstMs: document.getElementById('rstMs'),

  /* DB Viewer */
  dbBody:        document.getElementById('dbBody'),
  dbTotal:       document.getElementById('dbTotal'),
  dbGraphCanvas: document.getElementById('dbGraphCanvas'),
  dbGraphTitle:  document.getElementById('dbGraphTitle'),
  dbGraphInfo:   document.getElementById('dbGraphInfo'),
  dbCsvBtn:           document.getElementById('dbCsvBtn'),
  dbRefreshBtn:       document.getElementById('dbRefreshBtn'),
  dbDeleteBtn:        document.getElementById('dbDeleteBtn'),
  baselineStatus:     document.getElementById('baselineStatus'),
  baselineBuildBtn:     document.getElementById('baselineBuildBtn'),
  baselineDeleteBtn:    document.getElementById('baselineDeleteBtn'),
  baselineReanalyzeBtn: document.getElementById('baselineReanalyzeBtn'),
  baselineCycleCount:   document.getElementById('baselineCycleCount'),

  /* Quality */
  qualityRefreshBtn: document.getElementById('qualityRefreshBtn'),
  qualityClearBtn: document.getElementById('qualityClearBtn'),
  historyBody: document.getElementById('historyBody'),
  qualityChart: document.getElementById('qualityChart'),

  /* Alarm */
  alarmAckBtn: document.getElementById('alarmAckBtn'),
  alarmResetBtn: document.getElementById('alarmResetBtn'),
  alarmFetchBtn: document.getElementById('alarmFetchBtn'),
  activeAlarmDetail: document.getElementById('activeAlarmDetail'),
  activeAlarmAck: document.getElementById('activeAlarmAck'),
  alarmBody: document.getElementById('alarmBody'),

  chart: document.getElementById('trendChart'),
};

let pollingTimer = null;
let jogTimer = null;
let logScrollPaused = false;
let jogDir = 0;
let suppressRunChange = false;
let lastTelemetryStamp = '';
let lastSettingsStamp = '';
let lastResultStamp = '';
const history = [];
const historyMax = 400;
let demoUserLevel = { level: 0, name: 'OPERATOR' };
let demoMode = false;

let qualityChartInstance = null;

const numericInputs = [
  els.manualPos,
  els.manualSpeed,
  els.manualTorque,
  els.pJogSpeed,
  els.pAcc,
  els.pDec,
  els.pLimitPlus,
  els.pLimitMinus,
  els.pGearRatio,
  els.pBallLead,
  els.pEncRes,
  els.pHomeOffset,
  els.pPosGain,
  els.gPos1,
  els.gPos2,
  els.gPos3,
  els.gSpeed1,
  els.gSpeed2,
  els.gSpeed3,
  els.gTorque1,
  els.gTorque2,
  els.gTorque3,
  els.gReturnSpeed,
  els.gCycleRepeat,
  els.gInterCycleDelay,
  els.gDelay1Ms,
  els.gDelay2Ms,
  els.gDelay3Ms,
  els.pressApSpd,
  els.pressApPos,
  els.pressCntSpd,
  els.pressCntTh,
  els.pressPrSpd,
  els.pressPrPos,
  els.pressPrForce,
  els.pressDwell,
  els.pressRetSpd,
  els.pressRetPos,
  els.pressTimeout,
  els.judgeForceMax,
  els.judgeForceMin,
  els.judgePosMax,
  els.judgePosMin,
  els.maintThreshInput,
].filter(Boolean);

/* ---- Golden Signature Baseline ---- */
let g_baseline = null;

async function loadBaseline() {
  try {
    const r = await fetch('/api/baseline');
    if (r.status === 204) { g_baseline = null; updateBaselineUI(); return; }
    if (!r.ok) return;
    g_baseline = await r.json();
    updateBaselineUI();
  } catch (_) {}
}

function updateBaselineUI() {
  if (!els.baselineStatus) return;
  if (g_baseline) {
    const dt = new Date(g_baseline.createdAt).toLocaleString('ko-KR', { hour12: false });
    els.baselineStatus.textContent = `${dt}  |  ${g_baseline.sampleCount}사이클  |  0~${g_baseline.posMax}mm`;
    els.baselineStatus.style.color = '#6be';
    if (els.baselineDeleteBtn)    els.baselineDeleteBtn.style.display = '';
    if (els.baselineReanalyzeBtn) els.baselineReanalyzeBtn.style.display = '';
  } else {
    els.baselineStatus.textContent = '미설정';
    els.baselineStatus.style.color = '#556';
    if (els.baselineDeleteBtn)    els.baselineDeleteBtn.style.display = 'none';
    if (els.baselineReanalyzeBtn) els.baselineReanalyzeBtn.style.display = 'none';
  }
}

/* ---- Press Graph state ---- */
let pgLastCycle   = [];   /* 마지막 완료 사이클 포인트 */
let pgLastCycleNum = 0;
let pgPollTimer   = null;

const PG_PHASE_COLOR = {
  approach: '#4a9eff',
  contact:  '#ffaa00',
  press:    '#ff4455',
  dwell:    '#aa44ff',
  return:   '#888888',
};
/* step 번호 → 이름 매핑 (PressState_t: 1=APPROACH 2=CONTACT 3=PRESS 4=DWELL 5=RETURN) */
const PG_STEP_NAME = ['','approach','contact','press','dwell','return','cycle_end','cycle_ng','abort'];

function normalizeNumberText(value) {
  return String(value ?? '').replace(/,/g, '').trim();
}

function formatNumber(value, fractionDigits = 0) {
  const num = Number(value);
  if (!Number.isFinite(num)) {
    return '-';
  }

  return num.toLocaleString('en-US', {
    minimumFractionDigits: fractionDigits,
    maximumFractionDigits: fractionDigits,
  });
}

function formatInputText(value) {
  const raw = normalizeNumberText(value);
  if (raw === '') {
    return '';
  }

  const num = Number(raw);
  if (!Number.isFinite(num)) {
    return String(value ?? '');
  }

  const fraction = raw.includes('.') ? raw.split('.')[1] : '';
  const fractionDigits = fraction ? Math.min(fraction.length, 6) : 0;
  return formatNumber(num, fractionDigits);
}

function toInt(el, fallback = 0) {
  if (!el) {
    return fallback;
  }

  const parsed = Number.parseInt(normalizeNumberText(el.value), 10);
  return Number.isFinite(parsed) ? parsed : fallback;
}

/* 소수점 % 입력 → 0.1% 정수 (STM32 내부 단위). 예: "80.5" → 805 */
function toTenthPct(el, fallback = 0) {
  if (!el) { return fallback; }
  const parsed = parseFloat(normalizeNumberText(el.value));
  return Number.isFinite(parsed) ? Math.round(parsed * 10) : fallback;
}

/* 0.1% 정수 → 소수점 % 문자열. 예: 805 → "80.5" */
function fromTenthPct(v) {
  return (Number(v) / 10).toFixed(1);
}

async function api(path, method = 'GET', body = null) {
  const opts = { method, headers: {} };
  if (body !== null) {
    opts.headers['Content-Type'] = 'application/json';
    opts.body = JSON.stringify(body);
  }

  const res = await fetch(path, opts);
  if (!res.ok) {
    const txt = await res.text();
    throw new Error(txt || `${res.status} ${res.statusText}`);
  }

  if (res.status === 204) {
    return null;
  }

  const ct = res.headers.get('content-type') || '';
  if (ct.includes('text/csv')) {
    return await res.text();
  }

  return await res.json();
}

function setConnectionUi(connected) {
  els.connState.textContent = connected ? 'Connected' : 'Disconnected';
  els.connState.classList.toggle('online', connected);
  els.connState.classList.toggle('offline', !connected);
  els.connectBtn.textContent = connected ? 'Disconnect' : 'Connect';
}

async function refreshPorts() {
  const ports = await api('/api/ports');
  const current = els.portSelect.value;
  els.portSelect.innerHTML = '';
  for (const p of ports) {
    const opt = document.createElement('option');
    opt.value = p;
    opt.textContent = p;
    els.portSelect.appendChild(opt);
  }
  if (ports.length > 0) {
    els.portSelect.value = ports.includes(current) ? current : ports[0];
  }
}

async function toggleConnect() {
  if (els.connState.textContent === 'Connected') {
    await api('/api/disconnect', 'POST');
    return;
  }

  if (!els.portSelect.value) {
    alert('COM 포트를 선택하세요.');
    return;
  }

  await api('/api/connect', 'POST', {
    portName: els.portSelect.value,
    baudRate: Number.parseInt(els.baudSelect.value, 10),
  });
}

async function sendCmd(command) {
  if (demoMode) {
    console.log('[DEMO] CMD:', command);
    return;
  }
  if (!els.connState.classList.contains('online')) {
    console.warn('[CMD] Not connected — skipped:', command);
    return;
  }
  await api('/api/cmd', 'POST', { command });
}

async function requestSettingsSync() {
  await sendCmd('CMD,cfg_read=1');
}

function drawChart() {
  const canvas = els.chart;
  /* sync to .chart-area container size */
  const area = canvas.parentElement;
  if (area && area.clientWidth > 0) canvas.width = area.clientWidth - 20;
  if (area && area.clientHeight > 0) canvas.height = area.clientHeight - 12;
  const ctx = canvas.getContext('2d');
  const w = canvas.width;
  const h = canvas.height;

  ctx.clearRect(0, 0, w, h);
  ctx.fillStyle = '#0f1a29';
  ctx.fillRect(0, 0, w, h);

  const left = 44;
  const top = 12;
  const right = w - 12;
  const bottom = h - 28;

  ctx.strokeStyle = '#2f3f56';
  ctx.lineWidth = 1;
  for (let i = 0; i <= 6; i++) {
    const y = top + (bottom - top) * i / 6;
    ctx.beginPath();
    ctx.moveTo(left, y);
    ctx.lineTo(right, y);
    ctx.stroke();
  }

  if (history.length < 2) {
    return;
  }

  let minY = Number.POSITIVE_INFINITY;
  let maxY = Number.NEGATIVE_INFINITY;
  for (const p of history) {
    minY = Math.min(minY, p.pos, p.vel, p.tq);
    maxY = Math.max(maxY, p.pos, p.vel, p.tq);
  }

  if (!Number.isFinite(minY) || !Number.isFinite(maxY)) {
    return;
  }
  if (Math.abs(maxY - minY) < 1e-9) {
    maxY = minY + 1;
  }

  const plot = (value) => bottom - ((value - minY) / (maxY - minY)) * (bottom - top);
  const xAt = (idx) => left + (idx / (history.length - 1)) * (right - left);

  function drawSeries(key, color) {
    ctx.strokeStyle = color;
    ctx.lineWidth = 2;
    ctx.beginPath();
    for (let i = 0; i < history.length; i++) {
      const x = xAt(i);
      const y = plot(history[i][key]);
      if (i === 0) {
        ctx.moveTo(x, y);
      } else {
        ctx.lineTo(x, y);
      }
    }
    ctx.stroke();
  }

  drawSeries('pos', '#57c7ff');
  drawSeries('vel', '#80e96a');
  drawSeries('tq', '#ffb44c');

  ctx.fillStyle = '#c6d8ef';
  ctx.font = '12px Segoe UI';
  ctx.fillText(formatNumber(maxY, 0), 6, top + 4);
  ctx.fillText(formatNumber(minY, 0), 6, bottom);

  const legendY = h - 10;
  const drawLegend = (x, color, label) => {
    ctx.fillStyle = color;
    ctx.fillRect(x, legendY - 8, 12, 4);
    ctx.fillStyle = '#c6d8ef';
    ctx.fillText(label, x + 16, legendY - 4);
  };
  drawLegend(50, '#57c7ff', 'Position');
  drawLegend(180, '#80e96a', 'Velocity');
  drawLegend(310, '#ffb44c', 'Torque');
}

function setIlock(el, ok) {
  if (!el) return;
  el.classList.toggle('ok', !!ok);
}

function updatePressStatusBar(status) {
  const ps = status.pressStatus || {};
  const cnt = status.counter || {};
  const alm = status.activeAlarm || {};
  const rst = status.lastResult || {};

  if (els.psMode) els.psMode.textContent = ps.mode || '-';
  if (els.psStep) els.psStep.textContent = ps.step || '-';

  setIlock(els.ilkEstop, ps.estopOk);
  setIlock(els.ilkDoor, ps.doorOk);
  setIlock(els.ilkDrive, ps.driveReady);
  setIlock(els.ilkHome, ps.homeComplete);

  if (els.cntTotal) els.cntTotal.textContent = (cnt.total ?? 0).toLocaleString();
  if (els.cntOk) els.cntOk.textContent = (cnt.ok ?? 0).toLocaleString();
  if (els.cntNg) els.cntNg.textContent = (cnt.ng ?? 0).toLocaleString();
  if (els.cntRate) els.cntRate.textContent = ((cnt.ngRatePct ?? 0).toFixed(1)) + '%';
  if (els.cntCng) els.cntCng.textContent = cnt.consecutiveNg ?? 0;

  const almCode = alm.code || 0;
  const almMsg = alm.message || 'NONE';
  if (els.activeAlarm) {
    els.activeAlarm.textContent = almCode === 0 ? 'NONE' : `[${almCode}] ${almMsg}`;
    els.activeAlarm.className = almCode === 0 ? 'alarm-none' : 'alarm-active';
  }

  /* Last result box in Press Control tab */
  const rstStamp = rst.timestampUtc || '';
  if (rstStamp !== lastResultStamp) {
    lastResultStamp = rstStamp;
    if (els.rstCycle) els.rstCycle.textContent = rst.cycleNumber ?? '-';
    if (els.rstResult) {
      els.rstResult.textContent = rst.result || '-';
      els.rstResult.style.color = (rst.result === 'OK') ? 'var(--ok)' : 'var(--bad)';
    }
    if (els.rstForce) els.rstForce.textContent = `${rst.peakForcePct ?? '-'}%`;
    if (els.rstPos) els.rstPos.textContent = rst.endPosition ?? '-';
    if (els.rstMs) els.rstMs.textContent = rst.cycleTimeMs ? `${rst.cycleTimeMs} ms` : '-';
  }
}

function updateStatusView(status) {
  setConnectionUi(status.connected);

  const t = status.telemetry;
  els.mPos.textContent    = formatNumber(t.position, 2);
  els.mVel.textContent    = formatNumber(t.velocity, 0);
  els.mTor.textContent    = formatNumber(t.torque / 10, 1);
  els.mStatus.textContent = t.status || '-';
  els.mRun.textContent    = t.runEnabled === null ? '-' : (t.runEnabled ? 'ON' : 'OFF');

  suppressRunChange = true;
  if (t.runEnabled !== null) {
    els.runToggle.checked = !!t.runEnabled;
  }
  suppressRunChange = false;

  const stamp = t.timestampUtc || '';
  if (stamp !== lastTelemetryStamp) {
    lastTelemetryStamp = stamp;
    history.push({ pos: Number(t.position), vel: Number(t.velocity), tq: Number(t.torque) / 10 }); /* 0.1%→% */
    while (history.length > historyMax) {
      history.shift();
    }
    drawChart();
  }

  const settingsStamp = status.settings?.timestampUtc || '';
  if (settingsStamp !== '' && settingsStamp !== lastSettingsStamp) {
    lastSettingsStamp = settingsStamp;
    applySettingsToForm(status.settings);
  }

  updatePressStatusBar(status);
  updateAutoReadyLamp(status);
  updateCycleResultPanel(status);
  collectPressGraphPoint(status);
  updateRepeatProgress(status);
  /* 클라이언트 로그인 상태 항상 우선 적용 */
  status.userLevel = demoUserLevel;
  updateMaintenanceView(status);

  if (!logScrollPaused) {
    els.logBox.textContent = (status.logs || []).join('\n');
    els.logBox.scrollTop = els.logBox.scrollHeight;
  }
}

function updateRepeatProgress(status) {
  const el = els.cycleRepeatProgress;
  if (!el) { return; }
  const p = status.programProgress;
  if (!p || p.total === 0) {
    el.textContent = '';
  } else {
    /* 남은 횟수 다운카운트: cur=1,total=5 → 5, cur=2,total=5 → 4 … */
    const remaining = p.total - p.current + 1;
    el.textContent = `${remaining} / ${p.total}`;
  }
}

function setInputValue(el, value) {
  if (!el) {
    return;
  }
  if (document.activeElement === el) {
    return;
  }
  el.value = formatInputText(value);
}

function bindNumericInputFormatting() {
  numericInputs.forEach((el) => {
    el.addEventListener('focus', () => {
      el.value = normalizeNumberText(el.value);
    });

    el.addEventListener('blur', () => {
      el.value = formatInputText(el.value);
    });

    el.value = formatInputText(el.value);
  });
}

function setCheckboxValue(el, value) {
  if (!el) {
    return;
  }
  if (document.activeElement === el) {
    return;
  }
  el.checked = !!value;
}

function applySettingsToForm(settings) {
  if (!settings) {
    return;
  }

  if (settings.manual !== undefined) {
    const manual = settings.manual || {};
    setInputValue(els.manualPos, manual.position ?? 0);
    setInputValue(els.manualSpeed, manual.speed ?? 100);
    setInputValue(els.manualTorque, manual.torque ?? 20);
    setCheckboxValue(els.manualAbs, manual.absMode ?? true);
  }

  if (settings.parameter !== undefined) {
    const param = settings.parameter || {};
    setInputValue(els.pJogSpeed, param.jogSpeed ?? 10);
    setInputValue(els.pAcc, param.acc ?? 100);
    setInputValue(els.pDec, param.dec ?? 100);
    setInputValue(els.pLimitPlus, param.limitPlus ?? 100000);
    setInputValue(els.pLimitMinus, param.limitMinus ?? -100000);
    setInputValue(els.pGearRatio, param.gearRatio ?? 1);
    setInputValue(els.pBallLead,  param.ballLeadMm ?? 1);
    setInputValue(els.pEncRes,    param.encRes ?? 131072);
    /* Computed unit scale display (read-only) */
    {
      const g = Math.max(1, parseInt(els.pGearRatio.value) || 1);
      const l = Math.max(1, parseInt(els.pBallLead.value)  || 1);
      const e = Math.max(1, parseInt(els.pEncRes.value)    || 1);
      els.pUnitScale.value = Math.round(e * g / l);
    }
    setInputValue(els.pHomeOffset, param.homeOffset ?? 0);
    setInputValue(els.pPosGain, param.positionGain ?? 0);
  }

  if (settings.program !== undefined) {
    const prog = settings.program || {};
    setInputValue(els.gPos1, prog.pos1 ?? 0);
    setInputValue(els.gPos2, prog.pos2 ?? 0);
    setInputValue(els.gPos3, prog.pos3 ?? 0);
    setInputValue(els.gSpeed1, prog.speed1 ?? 100);
    setInputValue(els.gSpeed2, prog.speed2 ?? 100);
    setInputValue(els.gSpeed3, prog.speed3 ?? 100);
    setInputValue(els.gTorque1, prog.torque1 ?? 20);
    setInputValue(els.gTorque2, prog.torque2 ?? 20);
    setInputValue(els.gTorque3, prog.torque3 ?? 20);
    setInputValue(els.gReturnSpeed, prog.returnSpeed ?? 100);
    setInputValue(els.gDelay1Ms, prog.delay1Ms ?? 0);
    setInputValue(els.gDelay2Ms, prog.delay2Ms ?? 0);
    setInputValue(els.gDelay3Ms, prog.delay3Ms ?? 0);
    /* repeatCount가 없는 구버전 JSON/CFGR이면 현재 폼값 유지 */
    if (prog.repeatCount != null && prog.repeatCount > 0) {
      setInputValue(els.gCycleRepeat, prog.repeatCount);
    }
    if (prog.interCycleDelayMs != null) {
      setInputValue(els.gInterCycleDelay, prog.interCycleDelayMs);
    }
  }
}

async function pollStatus() {
  try {
    const url = demoMode ? '/api/demo/status' : '/api/status';
    const status = await api(url);
    /* Force fresh telemetry stamp in demo mode so chart updates */
    if (demoMode) {
      status.telemetry.timestampUtc = new Date().toISOString();
      status.lastResult.timestampUtc = new Date().toISOString();
    }
    updateStatusView(status);
  } catch (err) {
    console.error(err);
  }
}

function setActiveTab(tabName) {
  els.tabs.forEach((btn) => {
    btn.classList.toggle('active', btn.dataset.tab === tabName);
  });

  for (const [name, panel] of Object.entries(els.panels)) {
    panel?.classList.toggle('active', name === tabName);
  }

  for (const [name, section] of Object.entries(els.bottomSections)) {
    section?.classList.toggle('active', name === tabName);
  }

  if (tabName === 'quality') {
    loadQualityHistory();
  } else if (tabName === 'alarm') {
    loadAlarmHistory();
  } else if (tabName === 'maintenance') {
    refreshMaintenance();
  } else if (tabName === 'db') {
    loadDbCycles().catch(console.warn);
  }
}

function bindTabs() {
  els.tabs.forEach((btn) => {
    btn.addEventListener('click', () => setActiveTab(btn.dataset.tab));
  });
}

function bindHomeMain() {
  els.setHomeBtn.addEventListener('click', () => sendCmd('CMD,set_home=1').catch(alertError));
  els.runOnBtn.addEventListener('click', () => sendCmd('CMD,run=1').catch(alertError));
  els.runOffBtn.addEventListener('click', () => sendCmd('CMD,run=0').catch(alertError));

  els.runToggle.addEventListener('change', () => {
    if (suppressRunChange) {
      return;
    }
    sendCmd(els.runToggle.checked ? 'CMD,run=1' : 'CMD,run=0').catch(alertError);
  });
}

function bindJogHold(btn, dir) {
  let jogActive = false;

  const start = () => {
    if (jogActive) return;
    jogActive = true;
    jogDir = dir;
    sendCmd(`CMD,jog_velocity=${dir}`).catch(alertError);
    jogTimer = setInterval(() => {
      sendCmd(`CMD,jog_velocity=${dir}`).catch(alertError);
    }, 50);
  };

  const stop = () => {
    if (!jogActive) {
      return;
    }

    jogActive = false;
    jogDir = 0;
    clearInterval(jogTimer);
    jogTimer = null;

    sendCmd('CMD,manual_stop=1').catch(alertError);
  };

  btn.addEventListener('mousedown', start);
  btn.addEventListener('mouseup', stop);
  btn.addEventListener('mouseleave', stop);
  btn.addEventListener('touchstart', (e) => { e.preventDefault(); start(); }, { passive: false });
  btn.addEventListener('touchend', stop);
  btn.addEventListener('touchcancel', stop);
}

function bindManual() {
  bindJogHold(els.jogRevBtn, -1);
  bindJogHold(els.jogFwdBtn, +1);

  const sendManualRuntimeValues = async () => {
    const speed = Math.max(1, toInt(els.manualSpeed));
    const torque = Math.min(100, Math.max(1, toInt(els.manualTorque)));
    const absMode = els.manualAbs.checked ? 1 : 0;
    const pos = toInt(els.manualPos);

    await sendCmd(`CMD,manual_pos=${pos}`);
    await sendCmd(`CMD,manual_speed=${speed}`);
    await sendCmd(`CMD,manual_torque=${torque}`);
    await sendCmd(`CMD,manual_abs=${absMode}`);
  };

  const applyManualValues = async (options = {}) => {
    const syncAfterApply = options.syncAfterApply !== false;

    await sendManualRuntimeValues();
    await sendCmd('CMD,manual_apply=1');
      if (syncAfterApply) {
        await requestSettingsSync();
      }
  };

  els.manualApplyBtn.addEventListener('click', () => applyManualValues().catch(alertError));

  els.manualStartBtn.addEventListener('click', async () => {
    try {
      await sendManualRuntimeValues();
      await sendCmd('CMD,manual_start=1');
    } catch (err) {
      alertError(err);
    }
  });

  els.manualStopBtn.addEventListener('click', () => sendCmd('CMD,manual_stop=1').catch(alertError));
  els.manualSaveBtn.addEventListener('click', () => sendCmd('CMD,manual_save=1').catch(alertError));
  els.manualLoadBtn.addEventListener('click', () => sendCmd('CMD,manual_load=1').catch(alertError));
}

function updateUnitScaleDisplay() {
  const g = Math.max(1, parseInt(els.pGearRatio.value) || 1);
  const l = Math.max(1, parseInt(els.pBallLead.value)  || 1);
  const e = Math.max(1, parseInt(els.pEncRes.value)    || 1);
  els.pUnitScale.value = Math.round(e * g / l);
}

function bindParameter() {
  [els.pGearRatio, els.pBallLead, els.pEncRes].forEach(el => {
    if (el) el.addEventListener('input', updateUnitScaleDisplay);
  });

  /* 현재위치 → Limit+ 설정 (HW 인코더 절대값 기준) */
  if (els.captureLimitPlusBtn) {
    els.captureLimitPlusBtn.addEventListener('click', async () => {
      els.captureLimitPlusBtn.disabled = true;
      try {
        const st = await api('/api/status');
        const posHw = st?.telemetry?.positionHw;
        if (posHw !== undefined) setInputValue(els.pLimitPlus, Math.round(posHw));
        await sendCmd('CMD,capture_limit_plus=1');
        /* STM32가 parameterValues를 HW값으로 동기화하므로 JSON도 동기화 */
        await saveParamsToFile();
      } catch (e) { console.warn(e); }
      finally { els.captureLimitPlusBtn.disabled = false; }
    });
  }

  /* 현재위치 → Limit- 설정 (HW 인코더 절대값 기준) */
  if (els.captureLimitMinusBtn) {
    els.captureLimitMinusBtn.addEventListener('click', async () => {
      els.captureLimitMinusBtn.disabled = true;
      try {
        const st = await api('/api/status');
        const posHw = st?.telemetry?.positionHw;
        if (posHw !== undefined) setInputValue(els.pLimitMinus, Math.round(posHw));
        await sendCmd('CMD,capture_limit_minus=1');
        await saveParamsToFile();
      } catch (e) { console.warn(e); }
      finally { els.captureLimitMinusBtn.disabled = false; }
    });
  }

  els.paramSaveBtn.addEventListener('click', async () => {
    try {
      await sendParameterValues();            /* UI → STM32 RAM */
      await sendCmd('CMD,param_write_all=1'); /* RAM → 드라이브 SDO + Flash */
      await saveParamsToFile();               /* JSON 파일 저장 */
    } catch (err) {
      alertError(err);
    }
  });

  els.paramLoadBtn.addEventListener('click', async () => {
    try {
      await sendCmd('CMD,param_load=1'); /* Flash → STM32 RAM, CFGP 자동 응답 */
    } catch (err) {
      alertError(err);
    }
  });
}

function collectProgramSnapshot() {
  return {
    pos1:        toInt(els.gPos1),
    pos2:        toInt(els.gPos2),
    pos3:        toInt(els.gPos3),
    speed1:      Math.max(1, toInt(els.gSpeed1)),
    speed2:      Math.max(1, toInt(els.gSpeed2)),
    speed3:      Math.max(1, toInt(els.gSpeed3)),
    torque1:     Math.min(100, Math.max(1, toInt(els.gTorque1))),
    torque2:     Math.min(100, Math.max(1, toInt(els.gTorque2))),
    torque3:     Math.min(100, Math.max(1, toInt(els.gTorque3))),
    returnSpeed: Math.max(1, toInt(els.gReturnSpeed)),
    delay1Ms:    Math.max(0, toInt(els.gDelay1Ms)),
    delay2Ms:    Math.max(0, toInt(els.gDelay2Ms)),
    delay3Ms:    Math.max(0, toInt(els.gDelay3Ms)),
    repeatCount:       Math.min(99, Math.max(1, toInt(els.gCycleRepeat) || 1)),
    interCycleDelayMs: Math.max(0, toInt(els.gInterCycleDelay)),
  };
}

async function sendProgramValuesFromSnapshot(snap) {
  await sendCmd(`CMD,prog_pos1=${snap.pos1}`);
  await sendCmd(`CMD,prog_pos2=${snap.pos2}`);
  await sendCmd(`CMD,prog_pos3=${snap.pos3}`);
  await sendCmd(`CMD,prog_speed1=${snap.speed1}`);
  await sendCmd(`CMD,prog_speed2=${snap.speed2}`);
  await sendCmd(`CMD,prog_speed3=${snap.speed3}`);
  await sendCmd(`CMD,prog_torque1=${snap.torque1}`);
  await sendCmd(`CMD,prog_torque2=${snap.torque2}`);
  await sendCmd(`CMD,prog_torque3=${snap.torque3}`);
  await sendCmd(`CMD,prog_return_speed=${snap.returnSpeed}`);
  await sendCmd(`CMD,prog_delay1_ms=${snap.delay1Ms}`);
  await sendCmd(`CMD,prog_delay2_ms=${snap.delay2Ms}`);
  await sendCmd(`CMD,prog_delay3_ms=${snap.delay3Ms}`);
  await sendCmd(`CMD,prog_repeat_count=${snap.repeatCount}`);
  await sendCmd(`CMD,prog_inter_cycle_ms=${snap.interCycleDelayMs}`);
}

async function sendProgramValues() {
  await sendProgramValuesFromSnapshot(collectProgramSnapshot());
}

async function saveProgramToFile(snap) {
  try {
    await api('/api/program', 'POST', snap || collectProgramSnapshot());
  } catch (e) {
    console.warn('[program] save failed:', e);
  }
}

async function loadAndApplyProgramFromFile() {
  try {
    const saved = await api('/api/program');
    if (!saved || Object.keys(saved).length === 0) return;
    applySettingsToForm({ program: saved });
    /* repeatCount가 없는 구버전 JSON이면 현재 폼값(기본 1)을 유지한다.
     * 업데이트된 JSON에는 항상 repeatCount가 있으므로 applySettingsToForm에서 반영됨. */
    const snap = collectProgramSnapshot();
    await sendProgramValuesFromSnapshot(snap);
    await sendCmd('CMD,program_save=1');
    await saveProgramToFile(snap); /* repeatCount 포함하여 JSON 갱신 */
    console.log('[program] loaded from program.json and applied to STM32');
  } catch (e) {
    console.warn('[program] load failed:', e);
  }
}

async function applyProgramValues() {
  const snap = collectProgramSnapshot(); /* pollStatus 인터럽트 전에 DOM 값 확보 */
  await sendProgramValuesFromSnapshot(snap);
  await sendCmd('CMD,program_apply=1');
  await saveProgramToFile(snap);
  await requestSettingsSync();
}

function bindProgram() {
  els.programApplyBtn.addEventListener('click', () => applyProgramValues().catch(alertError));
  els.programStartBtn.addEventListener('click', async () => {
    try {
      const snap = collectProgramSnapshot();
      await sendProgramValuesFromSnapshot(snap);
      await sendCmd('CMD,program_apply=1');
      await saveProgramToFile(snap);
      await sendCmd('CMD,program_start=1');
    } catch (err) {
      alertError(err);
    }
  });
  els.programStopBtn.addEventListener('click', () => sendCmd('CMD,program_stop=1').catch(alertError));
  els.programSaveBtn.addEventListener('click', async () => {
    try {
      const snap = collectProgramSnapshot();
      await sendProgramValuesFromSnapshot(snap);
      await sendCmd('CMD,program_save=1');
      await saveProgramToFile(snap);
      await requestSettingsSync();
    } catch (err) { alertError(err); }
  });
  els.programLoadBtn.addEventListener('click', async () => {
    try {
      await sendCmd('CMD,program_load=1');
      await requestSettingsSync();
    } catch (err) { alertError(err); }
  });
}

/* ---- Phase 3: Press Control ---- */
function bindPress() {
  if (els.modeManuBtn) els.modeManuBtn.addEventListener('click', () => sendCmd('CMD,mode_set=manual').catch(alertError));
  if (els.modeSetupBtn) els.modeSetupBtn.addEventListener('click', () => sendCmd('CMD,mode_set=setup').catch(alertError));
  if (els.modeAutoBtn) els.modeAutoBtn.addEventListener('click', () => sendCmd('CMD,mode_set=auto').catch(alertError));

  if (els.cycleStartBtn) els.cycleStartBtn.addEventListener('click', () => sendCmd('CMD,cycle_start=1').catch(alertError));
  if (els.cycleStopBtn) els.cycleStopBtn.addEventListener('click', () => sendCmd('CMD,cycle_stop=1').catch(alertError));
  if (els.cycleAbortBtn) els.cycleAbortBtn.addEventListener('click', () => sendCmd('CMD,cycle_abort=1').catch(alertError));
  if (els.ngResetBtn) {
    els.ngResetBtn.addEventListener('click', async () => {
      try {
        await sendCmd('CMD,ng_reset=1');
      } catch (err) {
        alertError(err);
      }
    });
  }

  if (els.pressSaveBtn) {
    els.pressSaveBtn.addEventListener('click', async () => {
      try {
        await sendPressValues();
        await sendJudgeValues();
        await savePressParamsToFile();
      } catch (err) { alertError(err); }
    });
  }

  if (els.pressLoadBtn) {
    els.pressLoadBtn.addEventListener('click', async () => {
      try {
        await loadAndApplyPressParamsFromFile();
      } catch (err) { alertError(err); }
    });
  }

  if (els.simApplyBtn) {
    els.simApplyBtn.addEventListener('click', async () => {
      try {
        await sendCmd(`CMD,ilock_estop=${els.simEstop?.checked ? 1 : 0}`);
        await sendCmd(`CMD,ilock_door=${els.simDoor?.checked ? 1 : 0}`);
        await sendCmd(`CMD,ilock_home=${els.simHome?.checked ? 1 : 0}`);
      } catch (err) {
        alertError(err);
      }
    });
  }

  if (els.counterResetBtn) {
    els.counterResetBtn.addEventListener('click', () => sendCmd('CMD,counter_reset=1').catch(alertError));
  }
}

/* ---- Phase 3: Quality Trend ---- */
async function loadQualityHistory() {
  try {
    const url = demoMode ? '/api/demo/history' : '/api/history?n=100';
    const resp = await api(url);
    const records = resp.records || [];

    /* Update history table */
    if (els.historyBody) {
      els.historyBody.innerHTML = '';
      records.forEach((r, i) => {
        const tr = document.createElement('tr');
        const isOk = r.result === 'OK';
        const ts = r.timestampUtc ? new Date(r.timestampUtc).toLocaleTimeString() : '-';
        tr.innerHTML = `
          <td>${i + 1}</td>
          <td>${r.cycleNumber ?? '-'}</td>
          <td class="${isOk ? 'td-ok' : 'td-ng'}">${r.result ?? '-'}</td>
          <td>${r.peakForcePct ?? '-'}%</td>
          <td>${r.endPosition ?? '-'}</td>
          <td>${r.cycleTimeMs ?? '-'}</td>
          <td>${ts}</td>
        `;
        els.historyBody.appendChild(tr);
      });
    }

    /* Update Chart.js bar chart */
    if (els.qualityChart && typeof Chart !== 'undefined') {
      const okCounts = [];
      const ngCounts = [];
      const labels = [];
      const bucketSize = Math.max(1, Math.ceil(records.length / 20));

      for (let i = 0; i < records.length; i += bucketSize) {
        const bucket = records.slice(i, i + bucketSize);
        const ok = bucket.filter(r => r.result === 'OK').length;
        const ng = bucket.length - ok;
        labels.push(`${i + 1}-${Math.min(i + bucketSize, records.length)}`);
        okCounts.push(ok);
        ngCounts.push(ng);
      }

      if (qualityChartInstance) {
        qualityChartInstance.data.labels = labels;
        qualityChartInstance.data.datasets[0].data = okCounts;
        qualityChartInstance.data.datasets[1].data = ngCounts;
        qualityChartInstance.update();
      } else {
        qualityChartInstance = new Chart(els.qualityChart, {
          type: 'bar',
          data: {
            labels,
            datasets: [
              { label: 'OK', data: okCounts, backgroundColor: '#4cae74' },
              { label: 'NG', data: ngCounts, backgroundColor: '#e05050' },
            ],
          },
          options: {
            responsive: true,
            plugins: { legend: { labels: { color: '#c6d8ef' } } },
            scales: {
              x: { stacked: true, ticks: { color: '#8fa8c8' }, grid: { color: '#1e2d3f' } },
              y: { stacked: true, ticks: { color: '#8fa8c8' }, grid: { color: '#1e2d3f' } },
            },
          },
        });
      }
    }
  } catch (err) {
    console.error(err);
  }
}

function bindQuality() {
  if (els.qualityRefreshBtn) {
    els.qualityRefreshBtn.addEventListener('click', () => loadQualityHistory().catch(alertError));
  }

  if (els.qualityClearBtn) {
    els.qualityClearBtn.addEventListener('click', async () => {
      try {
        await api('/api/history/clear', 'POST');
        await loadQualityHistory();
      } catch (err) {
        alertError(err);
      }
    });
  }
}

/* ---- Phase 3: Alarm History ---- */
async function loadAlarmHistory() {
  try {
    const url = demoMode ? '/api/demo/alarms' : '/api/alarm/list';
    const entries = await api(url);

    /* Update active alarm detail from status */
    const status = await api(demoMode ? '/api/demo/status' : '/api/status');
    const alm = status.activeAlarm || {};
    if (els.activeAlarmDetail) {
      els.activeAlarmDetail.textContent = alm.code
        ? `[${alm.code}] ${alm.message}`
        : 'NONE (code=0)';
    }
    if (els.activeAlarmAck) {
      els.activeAlarmAck.textContent = alm.acked ? '(ACKed)' : '';
    }

    if (els.alarmBody) {
      els.alarmBody.innerHTML = '';
      (entries || []).forEach((e) => {
        const tr = document.createElement('tr');
        const ts = e.timestampUtc ? new Date(e.timestampUtc).toLocaleTimeString() : '-';
        tr.innerHTML = `
          <td>${e.code}</td>
          <td>${e.message}</td>
          <td>${e.acked ? '✓' : '-'}</td>
          <td>${ts}</td>
        `;
        els.alarmBody.appendChild(tr);
      });
    }
  } catch (err) {
    console.error(err);
  }
}

function bindAlarm() {
  if (els.alarmAckBtn) {
    els.alarmAckBtn.addEventListener('click', async () => {
      try {
        await api('/api/alarm/ack', 'POST');
        await loadAlarmHistory();
      } catch (err) {
        alertError(err);
      }
    });
  }

  if (els.alarmResetBtn) {
    els.alarmResetBtn.addEventListener('click', async () => {
      try {
        await api('/api/alarm/reset', 'POST');
        await loadAlarmHistory();
      } catch (err) {
        alertError(err);
      }
    });
  }

  if (els.alarmFetchBtn) {
    els.alarmFetchBtn.addEventListener('click', async () => {
      try {
        await api('/api/alarm/fetch', 'POST');
        await new Promise(r => setTimeout(r, 400));
        await loadAlarmHistory();
      } catch (err) {
        alertError(err);
      }
    });
  }
}

/* ---- Phase 4: Maintenance ---- */
function updateMaintenanceView(status) {
  const mc = status.maintCounter || {};
  const ul = status.userLevel || {};
  const ps = status.pressStatus || {};

  if (els.maintTotal) els.maintTotal.textContent = (mc.totalCycles ?? 0).toLocaleString();
  if (els.maintSince) els.maintSince.textContent = (mc.sinceLastMaint ?? 0).toLocaleString();
  if (els.maintThresh) els.maintThresh.textContent = (mc.pmThreshold ?? 100000).toLocaleString();
  if (els.maintAlert) {
    const alert = !!mc.pmAlert;
    els.maintAlert.textContent = alert ? 'YES ⚠' : 'NO';
    els.maintAlert.className = alert ? 'alarm-active' : 'alarm-none';
  }

  const lvl = ul.level ?? 0;
  if (els.userLevelName) els.userLevelName.textContent = ul.name || 'OPERATOR';
  if (els.userLevelBadge) {
    els.userLevelBadge.textContent = lvl === 2 ? 'ADMIN' : lvl === 1 ? 'ENG' : 'OP';
    els.userLevelBadge.className = `ulevel-badge ${lvl === 2 ? 'adm' : lvl === 1 ? 'eng' : 'op'}`;
  }
  applyAccessControl(lvl);

  if (els.sysMode) els.sysMode.textContent = ps.mode || '-';
  if (els.sysStep) els.sysStep.textContent = ps.step || '-';
  if (els.sysDrive) els.sysDrive.textContent = ps.driveReady ? 'YES' : 'NO';
  if (els.sysHome) els.sysHome.textContent = ps.homeComplete ? 'YES' : 'NO';
  if (els.sysEstop) els.sysEstop.textContent = ps.estopOk ? 'OK' : 'TRIPPED';
  if (els.sysDoor) els.sysDoor.textContent = ps.doorOk ? 'CLOSED' : 'OPEN';
}

async function refreshMaintenance() {
  try {
    await api('/api/maintenance/refresh', 'POST');
  } catch (err) {
    console.error(err);
  }
}

/* PIN 테이블 (클라이언트 검증 — 펌웨어와 동일한 값) */
const PIN_TABLE = { 1234: { level: 1, name: 'ENGINEER' }, 9999: { level: 2, name: 'ADMIN' } };

function setUserLevel(level, name) {
  demoUserLevel = { level, name };
  /* 배지 즉시 갱신 */
  if (els.userLevelName) els.userLevelName.textContent = name;
  if (els.userLevelBadge) {
    els.userLevelBadge.textContent = level === 2 ? 'ADMIN' : level === 1 ? 'ENG' : 'OP';
    els.userLevelBadge.className = `ulevel-badge ${level === 2 ? 'adm' : level === 1 ? 'eng' : 'op'}`;
  }
  applyAccessControl(level);
}

/* 레벨별 버튼 활성/비활성 */
function applyAccessControl(level) {
  const engRequired = [
    els.paramSaveBtn,
    els.programSaveBtn,
    els.maintResetBtn, els.maintSetThreshBtn,
    els.alarmFetchBtn,
  ];
  const isEng = level >= 1;
  engRequired.forEach(btn => {
    if (!btn) return;
    btn.disabled = !isEng;
    btn.style.opacity = isEng ? '' : '0.35';
    btn.title = isEng ? '' : 'Engineer 이상 권한 필요 (PIN: 1234)';
  });
}

function bindMaintenance() {
  if (els.loginBtn) {
    els.loginBtn.addEventListener('click', async () => {
      const pin = els.loginPin?.value?.trim() || '';
      if (!pin) { alert('PIN을 입력하세요.'); return; }
      const pinNum = parseInt(pin, 10);
      const entry = PIN_TABLE[pinNum];

      if (!entry) {
        alert('PIN이 올바르지 않습니다.\nEngineer: 1234 / Admin: 9999');
        return;
      }

      /* 클라이언트 즉시 반영 */
      setUserLevel(entry.level, entry.name);
      els.loginPin.value = '';

      /* 연결된 경우 펌웨어에도 전송 */
      if (!demoMode && els.connState.classList.contains('online')) {
        api('/api/user/login', 'POST', { pin: pinNum }).catch(() => {});
      }
    });
  }

  if (els.logoutBtn) {
    els.logoutBtn.addEventListener('click', async () => {
      setUserLevel(0, 'OPERATOR');
      if (!demoMode && els.connState.classList.contains('online')) {
        api('/api/user/logout', 'POST').catch(() => {});
      }
    });
  }

  if (els.maintRefreshBtn) {
    els.maintRefreshBtn.addEventListener('click', async () => {
      if (!demoMode) await refreshMaintenance();
      await pollStatus();
    });
  }

  if (els.maintResetBtn) {
    els.maintResetBtn.addEventListener('click', async () => {
      try {
        await api('/api/maintenance/reset', 'POST');
        await pollStatus();
      } catch (err) {
        alertError(err);
      }
    });
  }

  if (els.maintSetThreshBtn) {
    els.maintSetThreshBtn.addEventListener('click', async () => {
      const thr = toInt(els.maintThreshInput, 0);
      if (thr <= 0) { alert('임계값을 입력하세요.'); return; }
      try {
        await api('/api/maintenance/threshold', 'POST', { threshold: thr });
        await pollStatus();
      } catch (err) {
        alertError(err);
      }
    });
  }
}

/* ---- Press Parameter Persistence ---- */
function collectPressFormValues() {
  return {
    approachSpeed: toInt(els.pressApSpd),
    approachPos:   toInt(els.pressApPos),
    contactSpeed:  toInt(els.pressCntSpd),
    contactTh:     parseFloat(els.pressCntTh?.value) || 0,   /* % 소수점 저장 */
    pressSpeed:    toInt(els.pressPrSpd),
    pressTargetPos:toInt(els.pressPrPos),
    pressMaxForce: parseFloat(els.pressPrForce?.value) || 0, /* % 소수점 저장 */
    dwellMs:       toInt(els.pressDwell),
    returnSpeed:   toInt(els.pressRetSpd),
    returnPos:     toInt(els.pressRetPos),
    timeoutMs:     toInt(els.pressTimeout),
    judgeForceMax: parseFloat(els.judgeForceMax?.value) || 0, /* % 소수점 저장 */
    judgeForceMin: parseFloat(els.judgeForceMin?.value) || 0, /* % 소수점 저장 */
    judgePosMax:   toInt(els.judgePosMax),
    judgePosMin:   toInt(els.judgePosMin),
  };
}

async function savePressParamsToFile() {
  try {
    await api('/api/press-params', 'POST', collectPressFormValues());
  } catch (e) {
    console.warn('[press-params] save failed:', e);
  }
}

function applyPressParamsToForm(saved) {
  if (!saved || Object.keys(saved).length === 0) return;
  setInputValue(els.pressApSpd,    saved.approachSpeed  ?? '');
  setInputValue(els.pressApPos,    saved.approachPos    ?? '');
  setInputValue(els.pressCntSpd,   saved.contactSpeed   ?? '');
  setInputValue(els.pressCntTh,    saved.contactTh      != null ? Number(saved.contactTh).toFixed(1)    : '');
  setInputValue(els.pressPrSpd,    saved.pressSpeed     ?? '');
  setInputValue(els.pressPrPos,    saved.pressTargetPos ?? '');
  setInputValue(els.pressPrForce,  saved.pressMaxForce  != null ? Number(saved.pressMaxForce).toFixed(1) : '');
  setInputValue(els.pressDwell,    saved.dwellMs        ?? '');
  setInputValue(els.pressRetSpd,   saved.returnSpeed    ?? '');
  setInputValue(els.pressRetPos,   saved.returnPos      ?? '');
  setInputValue(els.pressTimeout,  saved.timeoutMs      ?? '');
  setInputValue(els.judgeForceMax, saved.judgeForceMax != null ? Number(saved.judgeForceMax).toFixed(1) : '');
  setInputValue(els.judgeForceMin, saved.judgeForceMin != null ? Number(saved.judgeForceMin).toFixed(1) : '');
  setInputValue(els.judgePosMax,   saved.judgePosMax    ?? '');
  setInputValue(els.judgePosMin,   saved.judgePosMin    ?? '');
}

async function sendPressValues() {
  await sendCmd(`CMD,press_approach_speed=${toInt(els.pressApSpd)}`);
  await sendCmd(`CMD,press_approach_pos=${toInt(els.pressApPos)}`);
  await sendCmd(`CMD,press_contact_speed=${toInt(els.pressCntSpd)}`);
  await sendCmd(`CMD,press_contact_th=${toTenthPct(els.pressCntTh)}`);  /* 0.1% 단위 */
  await sendCmd(`CMD,press_speed=${toInt(els.pressPrSpd)}`);
  await sendCmd(`CMD,press_target_pos=${toInt(els.pressPrPos)}`);
  await sendCmd(`CMD,press_max_force=${toTenthPct(els.pressPrForce)}`);  /* 0.1% 단위 */
  await sendCmd(`CMD,press_dwell_ms=${toInt(els.pressDwell)}`);
  await sendCmd(`CMD,press_return_speed=${toInt(els.pressRetSpd)}`);
  await sendCmd(`CMD,press_return_pos=${toInt(els.pressRetPos)}`);
  await sendCmd(`CMD,press_timeout_ms=${toInt(els.pressTimeout)}`);
}

async function sendJudgeValues() {
  await sendCmd(`CMD,judge_force_max=${toTenthPct(els.judgeForceMax)}`);  /* 0.1% 단위 */
  await sendCmd(`CMD,judge_force_min=${toTenthPct(els.judgeForceMin)}`);  /* 0.1% 단위 */
  await sendCmd(`CMD,judge_pos_max=${toInt(els.judgePosMax)}`);
  await sendCmd(`CMD,judge_pos_min=${toInt(els.judgePosMin)}`);
}

async function loadAndApplyPressParamsFromFile() {
  try {
    const saved = await api('/api/press-params');
    if (!saved || Object.keys(saved).length === 0) return;
    applyPressParamsToForm(saved);
    await sendPressValues();
    await sendJudgeValues();
    console.log('[press-params] loaded from press-params.json and applied to STM32');
  } catch (e) {
    console.warn('[press-params] load failed:', e);
  }
}

/* ---- Press Graph: STM32 고속 샘플링 데이터 수신 후 표시 ---- */
function collectPressGraphPoint(status) {
  const lr = status.lastResult || {};
  const cycleNum = Number(lr.cycleNumber) || 0;
  if (cycleNum > 0 && cycleNum !== pgLastCycleNum) {
    pgLastCycleNum = cycleNum;
    /* RST 프레임 감지 → STM32가 GRFS 스트리밍 시작. 최대 10초 폴링 */
    if (pgPollTimer) clearTimeout(pgPollTimer);
    pgPollTimer = setTimeout(() => pollGraphData(lr, 0), 500);
  }
}

async function pollGraphData(lastResult, attempt) {
  if (attempt > 20) return; /* 10초 타임아웃 */
  try {
    const r = await fetch('/api/graph');
    if (!r.ok) { return; }
    const data = await r.json();
    if (data.ready && Number(data.cycleNumber) === pgLastCycleNum && data.count > 1) {
      /* STM32 고해상도 데이터로 그래프 표시 */
      pgLastCycle = data.points.map(p => ({
        pos:   Math.abs(p.pos),              /* displacement from home (mm) */
        force: Math.abs(p.torque) / 10,      /* 0.1% → % (absolute value) */
        step:  PG_STEP_NAME[p.step] || 'unknown',
      }));
      drawPressGraph(lastResult);
      /* DB 탭이 열려 있으면 즉시 갱신, 아니면 1.5초 후 백그라운드 갱신 */
      const activeTab = document.querySelector('.tab.active')?.dataset?.tab;
      if (activeTab === 'db') {
        loadDbCycles().catch(console.warn);
      } else {
        setTimeout(() => loadDbCycles().catch(console.warn), 1500);
      }
    } else {
      /* 아직 수신 중 — 500ms 후 재시도 */
      pgPollTimer = setTimeout(() => pollGraphData(lastResult, attempt + 1), 500);
    }
  } catch(_) {
    pgPollTimer = setTimeout(() => pollGraphData(lastResult, attempt + 1), 500);
  }
}

function drawGraphOnCanvas(canvas, points, lastResult) {
  if (!canvas) { return; }

  canvas.width  = canvas.offsetWidth  || 600;
  canvas.height = canvas.offsetHeight || 220;

  const ctx   = canvas.getContext('2d');
  const W     = canvas.width;
  const H     = canvas.height;
  const ML = 52, MR = 16, MT = 16, MB = 36;
  const pw = W - ML - MR;
  const ph = H - MT - MB;

  ctx.clearRect(0, 0, W, H);

  if (!points || points.length < 2) {
    ctx.fillStyle = '#446';
    ctx.font = '13px monospace';
    ctx.textAlign = 'center';
    ctx.fillText('사이클 완료 후 그래프가 표시됩니다', W / 2, H / 2);
    return;
  }

  const pgLastCycle = points; /* local alias */

  /* --- 데이터 범위 (position은 STM32 TEL 프레임에서 이미 mm 단위로 전송됨) --- */
  const posVals   = pgLastCycle.map(p => p.pos);
  const forceVals = pgLastCycle.map(p => p.force);
  let xMin = Math.min(...posVals), xMax = Math.max(...posVals);
  const yMin = 0;
  const blMax = g_baseline ? Math.max(...g_baseline.torqueAvg.map((v, i) => (v + g_baseline.torqueStd[i]) / 10)) : 0;
  const rawMax = forceVals.length > 0 ? Math.max(...forceVals) : 10;
  const yMax = Math.max(10, rawMax * 1.15, blMax * 1.15); /* 기준선 포함 */

  /* 범위가 너무 좁으면 확장 */
  if (Math.abs(xMax - xMin) < 0.1) { xMin -= 1; xMax += 1; }
  const xPad = (xMax - xMin) * 0.05;
  xMin -= xPad; xMax += xPad;

  const toX = (mm)  => ML + (mm - xMin) / (xMax - xMin) * pw;
  const toY = (pct) => MT + ph - (pct - yMin) / (yMax - yMin) * ph;

  /* --- 배경 그리드 (Y축 자동 스케일) --- */
  ctx.strokeStyle = '#1a3040';
  ctx.lineWidth   = 1;
  const yStep = yMax <= 20 ? 5 : yMax <= 50 ? 10 : 20;
  for (let f = 0; f <= yMax; f += yStep) {
    const y = toY(f);
    ctx.beginPath(); ctx.moveTo(ML, y); ctx.lineTo(ML + pw, y); ctx.stroke();
  }
  const xSteps = 5;
  for (let i = 0; i <= xSteps; i++) {
    const x = ML + (pw / xSteps) * i;
    ctx.beginPath(); ctx.moveTo(x, MT); ctx.lineTo(x, MT + ph); ctx.stroke();
  }

  /* --- Golden Signature Baseline 오버레이 --- */
  if (g_baseline && g_baseline.torqueAvg.length > 1) {
    const bl = g_baseline;
    const blPts = bl.torqueAvg.length;

    /* ±1σ 밴드 */
    ctx.fillStyle = 'rgba(160,160,180,0.10)';
    ctx.beginPath();
    for (let i = 0; i < blPts; i++) {
      const x = toX(bl.posMin + i * bl.resolution);
      const y = toY((bl.torqueAvg[i] + bl.torqueStd[i]) / 10);
      i === 0 ? ctx.moveTo(x, y) : ctx.lineTo(x, y);
    }
    for (let i = blPts - 1; i >= 0; i--) {
      const x = toX(bl.posMin + i * bl.resolution);
      const y = toY(Math.max(0, (bl.torqueAvg[i] - bl.torqueStd[i]) / 10));
      ctx.lineTo(x, y);
    }
    ctx.closePath();
    ctx.fill();

    /* 현재 사이클이 ±2σ 벗어나는 구간: 빨간 하이라이트 */
    const interpCycle = (posVal) => {
      const sorted = [...points].sort((a, b) => a.pos - b.pos);
      if (sorted.length === 0) return NaN;
      if (posVal <= sorted[0].pos) return sorted[0].force;
      if (posVal >= sorted[sorted.length - 1].pos) return sorted[sorted.length - 1].force;
      let lo = 0, hi = sorted.length - 1;
      while (lo < hi - 1) { const m = (lo + hi) >> 1; if (sorted[m].pos <= posVal) lo = m; else hi = m; }
      const t = (posVal - sorted[lo].pos) / (sorted[hi].pos - sorted[lo].pos);
      return sorted[lo].force + t * (sorted[hi].force - sorted[lo].force);
    };
    for (let i = 0; i < blPts; i++) {
      const g    = bl.posMin + i * bl.resolution;
      const cyF  = interpCycle(g);
      const blF  = bl.torqueAvg[i] / 10;
      const sig2 = (bl.torqueStd[i] / 10) * 2;
      if (!isNaN(cyF) && Math.abs(cyF - blF) > sig2 && sig2 > 0) {
        const x1 = toX(g - bl.resolution * 0.5);
        const x2 = toX(g + bl.resolution * 0.5);
        ctx.fillStyle = 'rgba(255,60,60,0.18)';
        ctx.fillRect(x1, MT, x2 - x1, ph);
      }
    }

    /* 평균선 (회색 점선) */
    ctx.strokeStyle = 'rgba(180,190,200,0.75)';
    ctx.lineWidth   = 1.5;
    ctx.setLineDash([6, 4]);
    ctx.beginPath();
    for (let i = 0; i < blPts; i++) {
      const x = toX(bl.posMin + i * bl.resolution);
      const y = toY(bl.torqueAvg[i] / 10);
      i === 0 ? ctx.moveTo(x, y) : ctx.lineTo(x, y);
    }
    ctx.stroke();
    ctx.setLineDash([]);
  }

  /* --- Judge 윈도우 (판정 영역) --- */
  const jfMax = toInt(els.judgeForceMax, 100);
  const jfMin = toInt(els.judgeForceMin, 0);
  const jpMax = toInt(els.judgePosMax, 0); /* mm */
  const jpMin = toInt(els.judgePosMin, 0); /* mm */
  const jx1 = toX(Math.min(jpMin, jpMax));
  const jx2 = toX(Math.max(jpMin, jpMax));
  const jy1 = toY(Math.max(jfMin, jfMax));
  const jy2 = toY(Math.min(jfMin, jfMax));
  ctx.fillStyle   = 'rgba(0,200,80,0.08)';
  ctx.strokeStyle = 'rgba(0,220,80,0.55)';
  ctx.lineWidth   = 1.5;
  ctx.setLineDash([4, 3]);
  ctx.fillRect(jx1, jy1, jx2 - jx1, jy2 - jy1);
  ctx.strokeRect(jx1, jy1, jx2 - jx1, jy2 - jy1);
  ctx.setLineDash([]);

  /* --- 데이터 트레이스 (단계별 색상) --- */
  ctx.lineWidth = 2;
  let prevStep = null;
  let segStart = 0;
  const drawSegment = (from, to, color) => {
    if (from >= to) { return; }
    ctx.strokeStyle = color;
    ctx.beginPath();
    ctx.moveTo(toX(pgLastCycle[from].pos), toY(pgLastCycle[from].force));
    for (let i = from + 1; i <= to; i++) {
      ctx.lineTo(toX(pgLastCycle[i].pos), toY(pgLastCycle[i].force));
    }
    ctx.stroke();
  };

  pgLastCycle.forEach((pt, i) => {
    if (pt.step !== prevStep) {
      if (prevStep !== null) {
        drawSegment(segStart, i, PG_PHASE_COLOR[prevStep] || '#888');
      }
      prevStep = pt.step;
      segStart = i;
    }
  });
  drawSegment(segStart, pgLastCycle.length - 1, PG_PHASE_COLOR[prevStep] || '#888');

  /* --- 결과 OK/NG 마커 (마지막 점) --- */
  const lastPt = pgLastCycle[pgLastCycle.length - 1];
  const isOk = (lastResult || {}).result === 'OK';
  ctx.fillStyle = isOk ? '#00e060' : '#ff3344';
  ctx.beginPath();
  ctx.arc(toX(lastPt.pos), toY(lastPt.force), 5, 0, Math.PI * 2);
  ctx.fill();

  /* --- 축 --- */
  ctx.strokeStyle = '#3a6080';
  ctx.lineWidth   = 1;
  ctx.beginPath();
  ctx.moveTo(ML, MT); ctx.lineTo(ML, MT + ph); ctx.lineTo(ML + pw, MT + ph);
  ctx.stroke();

  /* Y 레이블 */
  ctx.fillStyle  = '#6a9ab0';
  ctx.font       = '10px monospace';
  ctx.textAlign  = 'right';
  for (let f = 0; f <= yMax; f += yStep) {
    ctx.fillText(`${f}%`, ML - 4, toY(f) + 3);
  }

  /* X 레이블 */
  ctx.textAlign = 'center';
  for (let i = 0; i <= xSteps; i++) {
    const mm = xMin + (xMax - xMin) * (i / xSteps);
    ctx.fillText(mm.toFixed(1), ML + (pw / xSteps) * i, MT + ph + 14);
  }

  /* 축 제목 */
  ctx.fillStyle = '#8aaac0';
  ctx.font = '11px monospace';
  ctx.textAlign = 'center';
  ctx.fillText('Position (mm)', ML + pw / 2, H - 4);
  ctx.save();
  ctx.translate(12, MT + ph / 2);
  ctx.rotate(-Math.PI / 2);
  ctx.fillText('Force (%)', 0, 0);
  ctx.restore();
}

function drawPressGraph(lastResult) {
  drawGraphOnCanvas(els.pressGraphCanvas, pgLastCycle, lastResult);
}

/* ---- DB Viewer ---- */
let dbSelectedId = null;

async function loadDbCycles() {
  try {
    const data = await api('/api/db/cycles?limit=200');
    if (!data) return;
    if (els.dbTotal) els.dbTotal.textContent = `${data.total} 건`;
    const tbody = els.dbBody;
    if (!tbody) return;
    tbody.innerHTML = '';
    (data.records || []).forEach(r => {
      const isOk = r.result === 'OK';
      const tr = document.createElement('tr');
      tr.className = `db-row-${isOk ? 'ok' : 'ng'}`;
      if (r.id === dbSelectedId) tr.classList.add('db-row-selected');
      const ts = new Date(r.timestamp).toLocaleString('ko-KR', { hour12: false });
      const anomType  = r.anomalyType  || 'none';
      const anomScore = r.anomalyScore || 0;
      const anomCell  = anomType === 'none'
        ? '<span style="color:#446">-</span>'
        : anomType === 'ok'
          ? `<span style="color:#3b8">ok</span>`
          : `<span style="color:#f84;font-weight:600">${anomType}<br><small>${anomScore.toFixed(1)}%</small></span>`;
      const avgPct  = r.torqueAvg  ? (r.torqueAvg  / 10).toFixed(1) : '-';
      const peakPct = r.peakForce ? (r.peakForce / 10).toFixed(1) : '-';
      tr.innerHTML = `
        <td>${r.id}</td>
        <td>${r.cycleNo}</td>
        <td style="font-size:0.78rem">${ts}</td>
        <td>${r.result}</td>
        <td>${avgPct}%</td>
        <td>${peakPct}%</td>
        <td>${r.endPos}</td>
        <td>${r.cycleMs}</td>
        <td>${r.pointCount}</td>
        <td style="text-align:center;line-height:1.2">${anomCell}</td>`;
      tr.style.cursor = 'pointer';
      tr.addEventListener('click', () => selectDbRow(r, tr));
      tbody.appendChild(tr);
    });
  } catch (e) {
    console.warn('[DB] loadDbCycles failed:', e);
  }
}

async function selectDbRow(record, trEl) {
  dbSelectedId = record.id;
  /* 선택 강조 */
  document.querySelectorAll('#dbBody tr').forEach(r => r.classList.remove('db-row-selected'));
  trEl.classList.add('db-row-selected');

  /* CSV 다운로드 링크 설정 */
  if (els.dbCsvBtn) {
    els.dbCsvBtn.href = `/api/db/cycles/${record.id}/csv`;
    els.dbCsvBtn.download = `cycle_${record.cycleNo}_id${record.id}.csv`;
    els.dbCsvBtn.style.display = '';
  }

  /* 그래프 로드 */
  if (els.dbGraphTitle) els.dbGraphTitle.textContent = `Cycle #${record.cycleNo}  (ID ${record.id})`;
  if (els.dbGraphInfo) {
    const ts = new Date(record.timestamp).toLocaleString('ko-KR', { hour12: false });
    const anomStr = (record.anomalyType && record.anomalyType !== 'none' && record.anomalyType !== 'ok')
      ? `  |  이상: ${record.anomalyType} (${(record.anomalyScore || 0).toFixed(1)}%)`
      : '';
    const avgStr = record.torqueAvg ? ` Avg: ${(record.torqueAvg/10).toFixed(1)}%  |` : '';
    els.dbGraphInfo.textContent =
      `${ts}  |  결과: ${record.result}  |${avgStr}  Peak: ${(record.peakForce/10).toFixed(1)}%  |  Pos: ${record.endPos}mm  |  ${record.cycleMs}ms  |  ${record.pointCount}pts${anomStr}`;
  }

  try {
    const data = await api(`/api/db/cycles/${record.id}`);
    if (!data || !data.points) return;
    const points = data.points.map(p => ({
      pos:   Math.abs(p.pos),
      force: Math.abs(p.torque) / 10,
      step:  PG_STEP_NAME[p.step] || 'unknown',
    }));
    drawGraphOnCanvas(els.dbGraphCanvas, points, { result: record.result });
  } catch (e) {
    console.warn('[DB] loadDbGraph failed:', e);
  }
}

function bindDb() {
  if (els.dbRefreshBtn) {
    els.dbRefreshBtn.addEventListener('click', () => loadDbCycles().catch(console.warn));
  }

  /* Golden Signature 수립 */
  if (els.baselineBuildBtn) {
    els.baselineBuildBtn.addEventListener('click', async () => {
      const count = parseInt(els.baselineCycleCount?.value || '10', 10);
      els.baselineBuildBtn.disabled = true;
      els.baselineBuildBtn.textContent = '수립 중...';
      try {
        const r = await fetch(`/api/baseline/build?count=${count}`, { method: 'POST' });
        if (!r.ok) { const e = await r.json(); alert(e.error || '수립 실패'); return; }
        g_baseline = await r.json();
        updateBaselineUI();
        /* 현재 열린 그래프 갱신 */
        if (pgLastCycle.length > 0) drawPressGraph(null);
      } catch (e) { console.warn(e); } finally {
        els.baselineBuildBtn.disabled = false;
        els.baselineBuildBtn.textContent = '수립';
      }
    });
  }

  /* Golden Signature 삭제 */
  if (els.baselineDeleteBtn) {
    els.baselineDeleteBtn.addEventListener('click', async () => {
      if (!confirm('기준 프로파일을 삭제하시겠습니까?')) return;
      await fetch('/api/baseline', { method: 'DELETE' });
      g_baseline = null;
      updateBaselineUI();
      if (pgLastCycle.length > 0) drawPressGraph(null);
    });
  }

  /* 전체 DB 재분석 */
  if (els.baselineReanalyzeBtn) {
    els.baselineReanalyzeBtn.addEventListener('click', async () => {
      if (!confirm('현재 기준 프로파일로 전체 DB를 재분석하시겠습니까?')) return;
      els.baselineReanalyzeBtn.disabled = true;
      els.baselineReanalyzeBtn.textContent = '분석 중...';
      try {
        const r = await fetch('/api/baseline/reanalyze', { method: 'POST' });
        const j = await r.json();
        if (r.ok) {
          await loadDbCycles();
          alert(`재분석 완료: ${j.updated}건 업데이트`);
        } else {
          alert(j.error || '재분석 실패');
        }
      } catch (e) { console.warn(e); } finally {
        els.baselineReanalyzeBtn.disabled = false;
        els.baselineReanalyzeBtn.textContent = '재분석';
      }
    });
  }

  const dbTestInsertBtn = document.getElementById('dbTestInsertBtn');
  if (dbTestInsertBtn) {
    dbTestInsertBtn.addEventListener('click', async () => {
      try {
        dbTestInsertBtn.disabled = true;
        const res = await fetch('/api/db/test-insert', { method: 'POST' });
        const json = await res.json();
        if (res.ok) {
          await loadDbCycles();
          console.log('[DB] Test insert OK:', json);
        } else {
          alert('Test insert 실패: ' + JSON.stringify(json));
        }
      } catch (e) {
        alertError(e);
      } finally {
        dbTestInsertBtn.disabled = false;
      }
    });
  }

  /* UART Test: send CMD,test_graph=1 to STM32, wait for GRFE, then refresh */
  const dbUartTestBtn = document.getElementById('dbUartTestBtn');
  if (dbUartTestBtn) {
    dbUartTestBtn.addEventListener('click', async () => {
      try {
        dbUartTestBtn.disabled = true;
        dbUartTestBtn.textContent = '전송 중...';
        await sendCmd('CMD,test_graph=1');
        /* Graph streaming takes ~2ms × 32 lines ≈ 64ms, wait 500ms to be safe */
        await new Promise(r => setTimeout(r, 500));
        await loadDbCycles();
        dbUartTestBtn.textContent = 'UART Test';
      } catch (e) {
        alertError(e);
        dbUartTestBtn.textContent = 'UART Test';
      } finally {
        dbUartTestBtn.disabled = false;
      }
    });
  }

  if (els.dbDeleteBtn) {
    els.dbDeleteBtn.addEventListener('click', async () => {
      if (!dbSelectedId) { alert('삭제할 행을 선택하세요.'); return; }
      if (!confirm(`ID ${dbSelectedId} 사이클 데이터를 삭제하시겠습니까?`)) return;
      try {
        await fetch(`/api/db/cycles/${dbSelectedId}`, { method: 'DELETE' });
        dbSelectedId = null;
        if (els.dbCsvBtn) els.dbCsvBtn.style.display = 'none';
        if (els.dbGraphTitle) els.dbGraphTitle.textContent = '← 행을 클릭하면 그래프 표시';
        if (els.dbGraphInfo) els.dbGraphInfo.textContent = '';
        if (els.dbGraphCanvas) {
          const c = els.dbGraphCanvas;
          c.width = c.offsetWidth || 400;
          c.height = c.offsetHeight || 220;
          c.getContext('2d').clearRect(0, 0, c.width, c.height);
        }
        await loadDbCycles();
      } catch (e) {
        alertError(e);
      }
    });
  }
}

/* ---- AUTO Ready Lamp ---- */
function updateAutoReadyLamp(status) {
  const ps = status.pressStatus || {};
  const estop = !!ps.estopOk;
  const door  = !!ps.doorOk;
  const drive = !!ps.driveReady;
  const home  = !!ps.homeComplete;
  const ready = estop && door && drive && home;

  if (els.rcEstop) els.rcEstop.classList.toggle('ok', estop);
  if (els.rcDoor)  els.rcDoor.classList.toggle('ok', door);
  if (els.rcDrive) els.rcDrive.classList.toggle('ok', drive);
  if (els.rcHome)  els.rcHome.classList.toggle('ok', home);

  if (els.autoReadyLamp) els.autoReadyLamp.classList.toggle('ready', ready);
  if (els.readyLabel) els.readyLabel.textContent = ready ? 'AUTO READY' : 'NOT READY';
}

/* ---- Cycle Result Panel ---- */
const NG_LABELS = {
  'NG_FORCE_HIGH': 'NG — 힘 초과 (Force High)',
  'NG_FORCE_LOW':  'NG — 힘 부족 (Force Low)',
  'NG_POS_HIGH':   'NG — 위치 초과 (Pos High)',
  'NG_POS_LOW':    'NG — 위치 부족 (Pos Low)',
  'NG_TIME_OVER':  'NG — 타임아웃 (Time Over)',
  'NG_INTERLOCK':  'NG — 인터락 (Interlock)',
  'NG_ABORT':      'NG — 중단 (Abort)',
};
const RUNNING_STEPS = new Set(['APPROACH','CONTACT','PRESS','DWELL','RETURN']);

function updateCycleResultPanel(status) {
  const panel = els.cycleResultPanel;
  const step  = (status.pressStatus?.step || '').toUpperCase();
  const rst   = status.lastResult || {};
  const result = (rst.result || '').toUpperCase();

  if (!panel) return;

  panel.classList.remove('cr-ok', 'cr-ng', 'cr-run');

  if (RUNNING_STEPS.has(step)) {
    panel.classList.add('cr-run');
    els.crIcon.textContent  = '↺';
    els.crLabel.textContent = `진행중 — ${step}`;
    els.crDetail.textContent = '';
    if (els.ngResetBtn) els.ngResetBtn.disabled = true;
    return;
  }

  if (step === 'CYCLE_NG' || (result && result !== 'OK' && result !== '')) {
    panel.classList.add('cr-ng');
    els.crIcon.textContent  = '✕';
    els.crLabel.textContent = NG_LABELS[result] || `NG — ${result}`;
    els.crDetail.textContent =
      `Force: ${rst.peakForcePct ?? '-'}%  |  Pos: ${rst.endPosition ?? '-'}  |  Cycle: ${rst.cycleNumber ?? '-'}`;
    if (els.ngResetBtn) els.ngResetBtn.disabled = false;
    return;
  }

  if (result === 'OK') {
    panel.classList.add('cr-ok');
    els.crIcon.textContent  = '✓';
    els.crLabel.textContent = 'OK';
    els.crDetail.textContent =
      `Force: ${rst.peakForcePct ?? '-'}%  |  Pos: ${rst.endPosition ?? '-'}  |  Cycle: ${rst.cycleNumber ?? '-'}`;
    if (els.ngResetBtn) els.ngResetBtn.disabled = true;
    return;
  }

  /* Idle with no result yet */
  els.crIcon.textContent  = '—';
  els.crLabel.textContent = '대기중';
  els.crDetail.textContent = '';
  if (els.ngResetBtn) els.ngResetBtn.disabled = true;
}

/* ---- Parameter JSON persistence ---- */
async function saveParamsToFile() {
  try {
    const params = {
      jogSpeed:     toInt(els.pJogSpeed),
      acc:          toInt(els.pAcc),
      dec:          toInt(els.pDec),
      limitPlus:    toInt(els.pLimitPlus),
      limitMinus:   toInt(els.pLimitMinus),
      gearRatio:    Math.max(1, toInt(els.pGearRatio)),
      ballLeadMm:   Math.max(1, toInt(els.pBallLead)),
      encRes:       Math.max(1, toInt(els.pEncRes)),
      homeOffset:   toInt(els.pHomeOffset),
      positionGain: Math.max(0, toInt(els.pPosGain)),
    };
    await api('/api/params', 'POST', params);
  } catch (e) {
    console.warn('[params] save failed:', e);
  }
}

async function sendParameterValues() {
  await sendCmd(`CMD,param_jog_speed=${toInt(els.pJogSpeed)}`);
  await sendCmd(`CMD,param_acc=${toInt(els.pAcc)}`);
  await sendCmd(`CMD,param_dec=${toInt(els.pDec)}`);
  await sendCmd(`CMD,param_limit_plus=${toInt(els.pLimitPlus)}`);
  await sendCmd(`CMD,param_limit_minus=${toInt(els.pLimitMinus)}`);
  await sendCmd(`CMD,param_gear_ratio=${Math.max(1, toInt(els.pGearRatio))}`);
  await sendCmd(`CMD,param_lead=${Math.max(1, toInt(els.pBallLead))}`);
  await sendCmd(`CMD,param_enc_res=${Math.max(1, toInt(els.pEncRes))}`);
  await sendCmd(`CMD,param_home_offset=${toInt(els.pHomeOffset)}`);
  await sendCmd(`CMD,param_position_gain=${Math.max(0, toInt(els.pPosGain))}`);
  await saveParamsToFile();
}

async function loadAndApplyParamsFromFile() {
  try {
    const saved = await api('/api/params');
    if (!saved || Object.keys(saved).length === 0) return;
    applySettingsToForm({ parameter: saved });
    await sendParameterValues();
    await sendCmd('CMD,param_write_all=1');
    await requestSettingsSync();
    console.log('[params] loaded from params.json and applied to STM32');
  } catch (e) {
    console.warn('[params] load failed:', e);
  }
}

function alertError(err) {
  const msg = String(err);
  console.error(err);
  /* UART 미연결 오류는 팝업 없이 콘솔에만 기록 */
  if (msg.includes('not connected') || msg.includes('UART')) return;
  alert(msg);
}

function bindDemoMode() {
  if (!els.demoBtn) return;
  els.demoBtn.addEventListener('click', () => {
    demoMode = !demoMode;
    els.demoBtn.textContent = demoMode ? '■ Exit Demo' : 'Demo Mode';
    els.demoBtn.style.background = demoMode
      ? 'linear-gradient(180deg,#ff9060,#e05020)'
      : '';
    if (demoMode) {
      setConnectionUi(true);
      lastTelemetryStamp = '';
      lastSettingsStamp = '';
      lastResultStamp = '';
      setUserLevel(0, 'OPERATOR');
      pollStatus();
    } else {
      setConnectionUi(false);
      setUserLevel(0, 'OPERATOR');
    }
  });
}

function bindConnection() {
  els.refreshPortsBtn.addEventListener('click', () => refreshPorts().catch(alertError));
  els.readAllBtn.addEventListener('click', () => requestSettingsSync().catch(alertError));
  els.connectBtn.addEventListener('click', async () => {
    try {
      const wasConnected = (els.connState.textContent === 'Connected');
      await toggleConnect();
      await pollStatus();

      const isConnectedNow = (els.connState.textContent === 'Connected');
      if (!wasConnected && isConnectedNow) {
        await requestSettingsSync();
        await loadAndApplyParamsFromFile();
        await loadAndApplyPressParamsFromFile();
        await loadAndApplyProgramFromFile();
      }
    } catch (err) {
      alertError(err);
    }
  });
}

function bindLog() {
  if (els.logPauseBtn) {
    els.logPauseBtn.addEventListener('click', () => {
      logScrollPaused = !logScrollPaused;
      els.logPauseBtn.textContent = logScrollPaused ? '▶ 스크롤 재개' : '⏸ 스크롤 정지';
      els.logPauseBtn.classList.toggle('active', logScrollPaused);
      if (els.logPauseLabel) els.logPauseLabel.style.display = logScrollPaused ? 'inline' : 'none';
    });
  }
  if (els.logClearBtn) {
    els.logClearBtn.addEventListener('click', () => {
      logScrollPaused = false;
      els.logPauseBtn.textContent = '⏸ 스크롤 정지';
      els.logPauseBtn.classList.remove('active');
      if (els.logPauseLabel) els.logPauseLabel.style.display = 'none';
      els.logBox.textContent = '';
    });
  }
  if (els.logCopyBtn) {
    els.logCopyBtn.addEventListener('click', async () => {
      const text = els.logBox.textContent || '';
      if (!text) return;
      try {
        await navigator.clipboard.writeText(text);
        const orig = els.logCopyBtn.textContent;
        els.logCopyBtn.textContent = '✔ 복사됨';
        setTimeout(() => { els.logCopyBtn.textContent = orig; }, 1500);
      } catch {
        /* fallback: select all text in logBox */
        const sel = window.getSelection();
        const range = document.createRange();
        range.selectNodeContents(els.logBox);
        sel.removeAllRanges();
        sel.addRange(range);
      }
    });
  }
}

async function boot() {
  bindTabs();
  bindDemoMode();
  bindConnection();
  bindHomeMain();
  bindManual();
  bindParameter();
  bindProgram();
  bindPress();
  bindQuality();
  bindAlarm();
  bindMaintenance();
  bindDb();
  bindLog();
  bindNumericInputFormatting();
  applyAccessControl(0); /* 초기 OP 권한 적용 */
  loadBaseline().catch(console.warn); /* Golden Signature 로드 */

  await refreshPorts();
  await pollStatus();

  /* 새로고침/재진입 시 항상 저장된 파라미터를 폼에 복원 (sendCmd는 미연결 시 자동 스킵) */
  if (!demoMode) {
    await loadAndApplyParamsFromFile();
    await loadAndApplyPressParamsFromFile();
    await loadAndApplyProgramFromFile();
  }

  /* 이전 폴링 정리 후 setTimeout 재귀 루프 시작
   * setInterval 대비 장점: 이전 요청 완료 후 다음 요청 시작 → 동시 요청 없음
   * 100ms 간격으로 200ms TEL 위상 불일치 해소 */
  if (pollingTimer !== null) { clearTimeout(pollingTimer); pollingTimer = null; }
  async function pollLoop() {
    await pollStatus();
    pollingTimer = setTimeout(pollLoop, 100);
  }
  pollLoop();
}

boot().catch(alertError);
