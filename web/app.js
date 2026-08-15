import createIrChip8Module from './irchip8.js';

const wasmStatus = document.querySelector('#wasmStatus');
const dropZone = document.querySelector('#dropZone');
const romInput = document.querySelector('#romInput');
const romName = document.querySelector('#romName');
const dialect = document.querySelector('#dialect');
const detectedProfile = document.querySelector('#detectedProfile');
const confidence = document.querySelector('#confidence');
const runButton = document.querySelector('#runButton');
const resetButton = document.querySelector('#resetButton');
const fullscreenButton = document.querySelector('#fullscreenButton');
const profileSelect = document.querySelector('#profileSelect');
const speed = document.querySelector('#speed');
const speedValue = document.querySelector('#speedValue');
const runtimeMessage = document.querySelector('#runtimeMessage');
const screen = document.querySelector('#screen');
const keypad = document.querySelector('#keypad');
const ctx = screen.getContext('2d', { alpha: false });

const PROFILE_NAMES = ['Modern', 'COSMAC VIP', 'CHIP-48'];
const DIALECT_NAMES = ['CHIP-8', 'CHIP-48', 'Super-CHIP'];
const FRAMEBUFFER_STRIDE = 128;

const KEYBOARD_MAP = new Map([
  ['1', 0x1], ['2', 0x2], ['3', 0x3], ['4', 0xC],
  ['q', 0x4], ['w', 0x5], ['e', 0x6], ['r', 0xD],
  ['a', 0x7], ['s', 0x8], ['d', 0x9], ['f', 0xE],
  ['z', 0xA], ['x', 0x0], ['c', 0xB], ['v', 0xF],
]);

let Module = null;
let romBytes = null;
let romFileName = '';
let running = false;
let cpuHz = Number(speed.value);
let lastFrameTime = performance.now();
let cpuAccumulator = 0;
let timerAccumulator = 0;
let audioContext = null;
let oscillator = null;
let gain = null;

const keyboardKeys = new Set();
const pointerKeys = new Set();
const gamepadKeys = new Set();
const sentKeyState = new Uint8Array(16);

function setStatus(text, kind = '') {
  wasmStatus.textContent = text;
  wasmStatus.className = `status ${kind}`.trim();
}

function setRuntimeMessage(text) {
  runtimeMessage.textContent = text;
}

function cString(pointer) {
  if (!pointer) return '';
  let end = pointer;
  while (Module.HEAPU8[end] !== 0) end += 1;
  return new TextDecoder().decode(Module.HEAPU8.subarray(pointer, end));
}

function profileOverride() {
  return Number(profileSelect.value);
}

function probeAndDisplay(pointer, length) {
  const dialectCode = Module._irchip8_probe_rom(pointer, length);
  const profileCode = Module._irchip8_probe_profile();
  const confidenceValue = Module._irchip8_probe_confidence();

  dialect.textContent = DIALECT_NAMES[dialectCode] ?? 'Unknown';
  detectedProfile.textContent = PROFILE_NAMES[profileCode] ?? 'Unknown';
  confidence.textContent = `${confidenceValue}%`;
}

function withRomBuffer(callback) {
  const pointer = Module._malloc(romBytes.length);
  try {
    Module.HEAPU8.set(romBytes, pointer);
    return callback(pointer, romBytes.length);
  } finally {
    Module._free(pointer);
  }
}

function loadIntoMachine() {
  if (!romBytes || !Module) return false;

  const ok = withRomBuffer((pointer, length) => {
    probeAndDisplay(pointer, length);
    return Module._irchip8_load_rom(pointer, length, profileOverride());
  });

  if (!ok) {
    running = false;
    runButton.textContent = 'Run';
    setRuntimeMessage(cString(Module._irchip8_error()) || 'Could not load this ROM.');
    return false;
  }

  running = true;
  runButton.textContent = 'Pause';
  runButton.disabled = false;
  resetButton.disabled = false;
  sentKeyState.fill(0);
  cpuAccumulator = 0;
  timerAccumulator = 0;
  setRuntimeMessage(`Running ${romFileName} at ${cpuHz} Hz.`);
  renderFrame(true);
  ensureAudio();
  return true;
}

async function loadRomFile(file) {
  if (!Module) return;
  if (!file) return;

  const lowerName = file.name.toLowerCase();
  if (!lowerName.endsWith('.ch8')) {
    setRuntimeMessage('Please select a .ch8 ROM file.');
    return;
  }

  const bytes = new Uint8Array(await file.arrayBuffer());
  if (bytes.length === 0) {
    setRuntimeMessage('The selected ROM is empty.');
    return;
  }
  if (bytes.length > 3584) {
    setRuntimeMessage('This ROM is larger than the classic CHIP-8 memory area (3584 bytes).');
    return;
  }

  romBytes = bytes;
  romFileName = file.name;
  romName.textContent = `${file.name} (${bytes.length} bytes)`;
  loadIntoMachine();
}

function renderFrame(force = false) {
  if (!Module || !romBytes) return;
  if (!force && !Module._irchip8_draw_pending()) return;

  const width = Module._irchip8_display_width();
  const height = Module._irchip8_display_height();
  const pointer = Module._irchip8_framebuffer();
  const fb = Module.HEAPU8;

  if (screen.width !== width || screen.height !== height) {
    screen.width = width;
    screen.height = height;
  }

  ctx.fillStyle = '#020304';
  ctx.fillRect(0, 0, width, height);
  ctx.fillStyle = '#f1f5f9';

  for (let y = 0; y < height; y += 1) {
    const row = pointer + y * FRAMEBUFFER_STRIDE;
    for (let x = 0; x < width; x += 1) {
      if (fb[row + x]) ctx.fillRect(x, y, 1, 1);
    }
  }

  Module._irchip8_clear_draw_pending();
}

async function ensureAudio() {
  if (!audioContext) {
    audioContext = new AudioContext();
    oscillator = audioContext.createOscillator();
    gain = audioContext.createGain();
    oscillator.type = 'square';
    oscillator.frequency.value = 440;
    gain.gain.value = 0;
    oscillator.connect(gain);
    gain.connect(audioContext.destination);
    oscillator.start();
  }
  if (audioContext.state === 'suspended') {
    await audioContext.resume();
  }
}

function updateAudio() {
  if (!audioContext || !gain || !Module) return;
  const target = Module._irchip8_sound_active() ? 0.035 : 0;
  gain.gain.setTargetAtTime(target, audioContext.currentTime, 0.006);
}

function pollGamepad() {
  gamepadKeys.clear();
  const pads = navigator.getGamepads?.() ?? [];
  const pad = [...pads].find(Boolean);
  if (!pad) return;

  const pressed = (index) => Boolean(pad.buttons[index]?.pressed);

  if (pressed(12)) gamepadKeys.add(0x2); // up
  if (pressed(13)) gamepadKeys.add(0x8); // down
  if (pressed(14)) gamepadKeys.add(0x4); // left
  if (pressed(15)) gamepadKeys.add(0x6); // right
  if (pressed(0)) gamepadKeys.add(0x5);  // A
  if (pressed(1)) gamepadKeys.add(0x0);  // B
  if (pressed(2)) gamepadKeys.add(0xA);  // X
  if (pressed(3)) gamepadKeys.add(0xF);  // Y
}

function syncKeys() {
  if (!Module) return;
  for (let key = 0; key < 16; key += 1) {
    const pressed = keyboardKeys.has(key) || pointerKeys.has(key) || gamepadKeys.has(key);
    const value = pressed ? 1 : 0;
    if (sentKeyState[key] !== value) {
      Module._irchip8_set_key(key, value);
      sentKeyState[key] = value;
    }

    const button = keypad.querySelector(`[data-chip-key="${key}"]`);
    button?.classList.toggle('active', pressed);
  }
}

function animationFrame(now) {
  const delta = Math.min(0.1, Math.max(0, (now - lastFrameTime) / 1000));
  lastFrameTime = now;

  pollGamepad();
  syncKeys();

  if (Module && romBytes && running) {
    cpuAccumulator += delta * cpuHz;
    const cycles = Math.min(5000, Math.floor(cpuAccumulator));
    if (cycles > 0) {
      Module._irchip8_cycle_n(cycles);
      cpuAccumulator -= cycles;
    }

    timerAccumulator += delta * 60;
    while (timerAccumulator >= 1) {
      Module._irchip8_tick_timers();
      timerAccumulator -= 1;
    }

    if (Module._irchip8_halted()) {
      running = false;
      runButton.textContent = 'Run';
      const error = cString(Module._irchip8_error());
      setRuntimeMessage(error || 'Program halted.');
    }
  }

  renderFrame();
  updateAudio();
  requestAnimationFrame(animationFrame);
}

dropZone.addEventListener('click', () => romInput.click());
dropZone.addEventListener('keydown', (event) => {
  if (event.key === 'Enter' || event.key === ' ') {
    event.preventDefault();
    romInput.click();
  }
});
romInput.addEventListener('change', () => loadRomFile(romInput.files?.[0]));

for (const type of ['dragenter', 'dragover']) {
  dropZone.addEventListener(type, (event) => {
    event.preventDefault();
    dropZone.classList.add('dragging');
  });
}
for (const type of ['dragleave', 'drop']) {
  dropZone.addEventListener(type, (event) => {
    event.preventDefault();
    dropZone.classList.remove('dragging');
  });
}
dropZone.addEventListener('drop', (event) => loadRomFile(event.dataTransfer?.files?.[0]));

window.addEventListener('keydown', (event) => {
  const key = KEYBOARD_MAP.get(event.key.toLowerCase());
  if (key === undefined) return;
  event.preventDefault();
  keyboardKeys.add(key);
  ensureAudio();
});
window.addEventListener('keyup', (event) => {
  const key = KEYBOARD_MAP.get(event.key.toLowerCase());
  if (key === undefined) return;
  event.preventDefault();
  keyboardKeys.delete(key);
});
window.addEventListener('blur', () => keyboardKeys.clear());

keypad.querySelectorAll('[data-chip-key]').forEach((button) => {
  const key = Number(button.dataset.chipKey);
  const release = (event) => {
    event.preventDefault();
    pointerKeys.delete(key);
    try { button.releasePointerCapture(event.pointerId); } catch { /* no-op */ }
  };
  button.addEventListener('pointerdown', (event) => {
    event.preventDefault();
    pointerKeys.add(key);
    button.setPointerCapture(event.pointerId);
    ensureAudio();
  });
  button.addEventListener('pointerup', release);
  button.addEventListener('pointercancel', release);
  button.addEventListener('pointerleave', (event) => {
    if (event.buttons === 0) pointerKeys.delete(key);
  });
});

runButton.addEventListener('click', async () => {
  if (!romBytes) return;
  await ensureAudio();
  running = !running;
  runButton.textContent = running ? 'Pause' : 'Run';
  setRuntimeMessage(running ? `Running ${romFileName} at ${cpuHz} Hz.` : 'Paused.');
});

resetButton.addEventListener('click', async () => {
  if (!romBytes) return;
  await ensureAudio();
  loadIntoMachine();
});

profileSelect.addEventListener('change', () => {
  if (romBytes) loadIntoMachine();
});

speed.addEventListener('input', () => {
  cpuHz = Number(speed.value);
  speedValue.value = `${cpuHz} Hz`;
  if (running && romBytes) setRuntimeMessage(`Running ${romFileName} at ${cpuHz} Hz.`);
});

fullscreenButton.addEventListener('click', async () => {
  const target = document.querySelector('.screen-wrap');
  if (!document.fullscreenElement) {
    await target.requestFullscreen?.();
  } else {
    await document.exitFullscreen?.();
  }
});

try {
  Module = await createIrChip8Module({
    locateFile: (path) => path.endsWith('.wasm') ? './irchip8.wasm' : path,
  });
  setStatus('WebAssembly ready', 'ready');
  setRuntimeMessage('Choose a .ch8 ROM to start.');
} catch (error) {
  console.error(error);
  setStatus('WebAssembly failed', 'error');
  setRuntimeMessage('Could not initialize the WebAssembly module. Serve this folder over HTTP instead of opening index.html directly.');
}

requestAnimationFrame(animationFrame);
