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
    log: document.getElementById('bottom-log'),
  },

  setHomeBtn: document.getElementById('setHomeBtn'),
  runToggle: document.getElementById('runToggle'),
  runOnBtn: document.getElementById('runOnBtn'),
  runOffBtn: document.getElementById('runOffBtn'),

  jogStep: document.getElementById('jogStep'),
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
  pLimitPlus: document.getElementById('pLimitPlus'),
  pLimitMinus: document.getElementById('pLimitMinus'),
  pGearRatio: document.getElementById('pGearRatio'),
  pBallLead:  document.getElementById('pBallLead'),
  pEncRes:    document.getElementById('pEncRes'),
  pUnitScale: document.getElementById('pUnitScale'),
  pHomeOffset: document.getElementById('pHomeOffset'),
  pPosGain: document.getElementById('pPosGain'),
  paramWriteAllBtn: document.getElementById('paramWriteAllBtn'),
  paramReadAllBtn: document.getElementById('paramReadAllBtn'),
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
  gDelayMs: document.getElementById('gDelayMs'),
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
  pressApplyBtn: document.getElementById('pressApplyBtn'),
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
let jogDir = 0;
let suppressRunChange = false;
let lastTelemetryStamp = '';
let lastSettingsStamp = '';
let lastResultStamp = '';
const history = [];
const historyMax = 400;
const defaultJogStepPuls = 1000;
let demoUserLevel = { level: 0, name: 'OPERATOR' };
let demoMode = false;

let qualityChartInstance = null;

const numericInputs = [
  els.jogStep,
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
  els.gDelayMs,
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
  els.mPos.textContent = formatNumber(t.position, 0);
  els.mVel.textContent = formatNumber(t.velocity, 0);
  els.mTor.textContent = formatNumber(t.torque, 0);
  els.mStatus.textContent = t.status || '-';
  els.mRun.textContent = t.runEnabled === null ? '-' : (t.runEnabled ? 'ON' : 'OFF');

  suppressRunChange = true;
  if (t.runEnabled !== null) {
    els.runToggle.checked = !!t.runEnabled;
  }
  suppressRunChange = false;

  const stamp = t.timestampUtc || '';
  if (stamp !== lastTelemetryStamp) {
    lastTelemetryStamp = stamp;
    history.push({ pos: Number(t.position), vel: Number(t.velocity), tq: Number(t.torque) });
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
  /* 클라이언트 로그인 상태 항상 우선 적용 */
  status.userLevel = demoUserLevel;
  updateMaintenanceView(status);

  els.logBox.textContent = (status.logs || []).join('\n');
  els.logBox.scrollTop = els.logBox.scrollHeight;
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

  const manual = settings.manual || {};
  setInputValue(els.manualPos, manual.position ?? 0);
  setInputValue(els.manualSpeed, manual.speed ?? 100);
  setInputValue(els.manualTorque, manual.torque ?? 20);
  setCheckboxValue(els.manualAbs, manual.absMode ?? true);

  const param = settings.parameter || {};
  setInputValue(els.pJogSpeed, param.jogSpeed ?? 1000);
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
  setInputValue(els.gDelayMs, prog.delayMs ?? 0);
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

function sendJogDelta(dir) {
  const step = Math.max(1, toInt(els.jogStep, defaultJogStepPuls));
  sendCmd(`CMD,jog_delta=${step * dir}`).catch(alertError);
}

function bindJogHold(btn, dir) {
  let jogActive = false;

  const start = () => {
    if (jogActive) return;
    jogActive = true;
    jogDir = dir;
    sendCmd(`CMD,jog_velocity=${dir}`).catch(alertError);
  };

  const stop = () => {
    if (!jogActive) {
      return;
    }

    jogActive = false;
    jogDir = 0;
    clearInterval(jogTimer);

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

  const sendParameterValues = async () => {
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
  };

  els.paramWriteAllBtn.addEventListener('click', async () => {
    try {
      await sendParameterValues();
      await sendCmd('CMD,param_write_all=1');
      await requestSettingsSync();
    } catch (err) {
      alertError(err);
    }
  });

  els.paramReadAllBtn.addEventListener('click', () => sendCmd('CMD,param_read_all=1').catch(alertError));
  els.paramSaveBtn.addEventListener('click', () => sendCmd('CMD,param_save=1').catch(alertError));
  els.paramLoadBtn.addEventListener('click', () => sendCmd('CMD,param_load=1').catch(alertError));
}

async function sendProgramValues() {
  await sendCmd(`CMD,prog_pos1=${toInt(els.gPos1)}`);
  await sendCmd(`CMD,prog_pos2=${toInt(els.gPos2)}`);
  await sendCmd(`CMD,prog_pos3=${toInt(els.gPos3)}`);
  await sendCmd(`CMD,prog_speed1=${Math.max(1, toInt(els.gSpeed1))}`);
  await sendCmd(`CMD,prog_speed2=${Math.max(1, toInt(els.gSpeed2))}`);
  await sendCmd(`CMD,prog_speed3=${Math.max(1, toInt(els.gSpeed3))}`);
  await sendCmd(`CMD,prog_torque1=${Math.min(100, Math.max(1, toInt(els.gTorque1)))}`);
  await sendCmd(`CMD,prog_torque2=${Math.min(100, Math.max(1, toInt(els.gTorque2)))}`);
  await sendCmd(`CMD,prog_torque3=${Math.min(100, Math.max(1, toInt(els.gTorque3)))}`);
  await sendCmd(`CMD,prog_return_speed=${Math.max(1, toInt(els.gReturnSpeed))}`);
  await sendCmd(`CMD,prog_delay_ms=${Math.max(0, toInt(els.gDelayMs))}`);
}

async function applyProgramValues() {
  await sendProgramValues();
  await sendCmd('CMD,program_apply=1');
  await requestSettingsSync();
}

function bindProgram() {
  els.programApplyBtn.addEventListener('click', () => applyProgramValues().catch(alertError));
  els.programStartBtn.addEventListener('click', async () => {
    try {
      await applyProgramValues();
      await sendCmd('CMD,program_start=1');
    } catch (err) {
      alertError(err);
    }
  });
  els.programStopBtn.addEventListener('click', () => sendCmd('CMD,program_stop=1').catch(alertError));
  els.programSaveBtn.addEventListener('click', () => sendCmd('CMD,program_save=1').catch(alertError));
  els.programLoadBtn.addEventListener('click', () => sendCmd('CMD,program_load=1').catch(alertError));
}

/* ---- Phase 3: Press Control ---- */
function bindPress() {
  if (els.modeManuBtn) els.modeManuBtn.addEventListener('click', () => sendCmd('CMD,mode_set=manual').catch(alertError));
  if (els.modeSetupBtn) els.modeSetupBtn.addEventListener('click', () => sendCmd('CMD,mode_set=setup').catch(alertError));
  if (els.modeAutoBtn) els.modeAutoBtn.addEventListener('click', () => sendCmd('CMD,mode_set=auto').catch(alertError));

  if (els.cycleStartBtn) els.cycleStartBtn.addEventListener('click', () => sendCmd('CMD,cycle_start=1').catch(alertError));
  if (els.cycleStopBtn) els.cycleStopBtn.addEventListener('click', () => sendCmd('CMD,cycle_stop=1').catch(alertError));
  if (els.cycleAbortBtn) els.cycleAbortBtn.addEventListener('click', () => sendCmd('CMD,cycle_abort=1').catch(alertError));

  if (els.pressApplyBtn) {
    els.pressApplyBtn.addEventListener('click', async () => {
      try {
        await sendCmd(`CMD,press_approach_speed=${toInt(els.pressApSpd)}`);
        await sendCmd(`CMD,press_approach_pos=${toInt(els.pressApPos)}`);
        await sendCmd(`CMD,press_contact_speed=${toInt(els.pressCntSpd)}`);
        await sendCmd(`CMD,press_contact_th=${toInt(els.pressCntTh)}`);
        await sendCmd(`CMD,press_speed=${toInt(els.pressPrSpd)}`);
        await sendCmd(`CMD,press_target_pos=${toInt(els.pressPrPos)}`);
        await sendCmd(`CMD,press_max_force=${toInt(els.pressPrForce)}`);
        await sendCmd(`CMD,press_dwell_ms=${toInt(els.pressDwell)}`);
        await sendCmd(`CMD,press_return_speed=${toInt(els.pressRetSpd)}`);
        await sendCmd(`CMD,press_return_pos=${toInt(els.pressRetPos)}`);
        await sendCmd(`CMD,press_timeout_ms=${toInt(els.pressTimeout)}`);
      } catch (err) {
        alertError(err);
      }
    });
  }

  if (els.judgeApplyBtn) {
    els.judgeApplyBtn.addEventListener('click', async () => {
      try {
        await sendCmd(`CMD,judge_force_max=${toInt(els.judgeForceMax)}`);
        await sendCmd(`CMD,judge_force_min=${toInt(els.judgeForceMin)}`);
        await sendCmd(`CMD,judge_pos_max=${toInt(els.judgePosMax)}`);
        await sendCmd(`CMD,judge_pos_min=${toInt(els.judgePosMin)}`);
      } catch (err) {
        alertError(err);
      }
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
    els.paramWriteAllBtn, els.paramSaveBtn,
    els.programSaveBtn,
    els.pressApplyBtn, els.judgeApplyBtn,
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
      }
    } catch (err) {
      alertError(err);
    }
  });
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
  bindNumericInputFormatting();
  applyAccessControl(0); /* 초기 OP 권한 적용 */

  await refreshPorts();
  await pollStatus();

  if (pollingTimer !== null) {
    clearInterval(pollingTimer);
  }
  pollingTimer = setInterval(pollStatus, 220);
}

boot().catch(alertError);
