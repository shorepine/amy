// Gamma9000 — web drum machine on AMY wasm, using the Koblo Tokyo sample set.
"use strict";

const NUM_CH = 8;
const NUM_STEPS = 16;
const NUM_PATTERNS = 16;
const OSC_BASE = 200;          // stay clear of oscs used by AMY's default web synths
const LOOKAHEAD_MS = 150;      // schedule this far into the future
const TICK_MS = 30;
const ACCENT_GAIN = 1.5;
const PCM_PHASE_DENOM = 1 << 23;  // AMY PCM phase is frame_index / 2^PCM_INDEX_BITS

// Baked-in example setups: share-format strings (see decodeShare). Regenerate
// by dialing in a setup and copying the part of the share URL after "#s=".
const EXAMPLE_SONGS = [
  { name: "Four On The Floor", data: "1xVXBbtswDP2V4J2JTE7ntNF5CHZoscOGXQwfFIeeA8iSK8neiiD_PkgxsiGt2wJbEPBA0Xx6j6YpeY-Bnd9ZA5kRNl0LmS2WBN_0da0ZUhBa5QO771YnTKdCYGdSRlvbQQbXM6G1W4Y8pQnemh-QRUmoGmUMaw9Z7OFV20ViBLcSqw8rsdps5z_VAEK3C1WTiDsVBeY5YYiyYn5HsHXtOaR01Qdb15DZUghBcOwj5pbQ9oEha6U9xwK0HYMDvSTsryVcadVdSdrt2ispN82U8PLCynZS-ebi3d7ytWbMKT_53n-083_ULk83wni8IYtiQSJZ9rIvqRD0qv2FyM5yUxzPcCUVGb1qI8dEldG_q9Jz5XNESYhX55IwxOa8XdT_R8TvfP7k9lj6_GNcJ_-uKI9kFynyOaI8vNV81L8g93A8sNvEWc1ygt4NbNin2b3L4__Hut6nAeeqsafFJ9bqCXKRj_E6Mdwcg69PpoI0vdYEfrw_bnp8GP1nSHEgGNXGQ7e2vZt9MbNvDc_W2lqHw28" },
  { name: "Boom Bap", data: "1xVVNj9owEP0r1TtPwIGFDT6uqqqH9lSpF8TBZCebqI4dbCftCvHfKwcUwars0pVgNcrHeF7evIwzky06dr6yBjIlrJsaciEIvmyLQjPk3YxQKx_Y_bS6xzQqBHYGUhC0tQ1kcC0TavvIkEOY4K15glyuCHmpjGHtIZdbeFU3kRnBZSIbB5dkIkvWyvvk0bV1Mh39Vh0ITRXyEjKZxJQx3WhG6KIIMcoItig8h15F3gZbFJDpXAhBcOwj5p5Qt4EhC6U9RznaHpwdHcnQlTELIcTYG-W4l5CeShBXVnBSiLIqVUhybT0_npNxdysZtmHzgbXIrXlSSV2dLcT9VTQ07PLWx6YY-yquJb5Uv9h9ZCW0am6bfmgLV9W-tDbcoitWw3g5zArI5XJC4sjSo3NvK1qexl5iIyI9eGeOI45_WzogzuS4gOM1RHoxx4oQ5_Cc0MXipPSq9a_-FkKMFhR3Me7N_nrsz_-bU4zmF-V9P2JQdQFL_KZipd6_OddATF5EkumNhax2b1Gg-AO5heOO3Tp2bkrQVceGfd_I2Sz-2a1rfd_tnJd2uPnMWj1DTmYH_0tPMN07P55NDmlarQm8-QYZ1zff9w9vvkKKHcGoOo6hB2vrTw-qwe4v" },
  { name: "Linn Pop", data: "11VVRi9swDP4rQ89KZ6fr3c7PY-zhBoPBXkIfnFRZAraVs53cSul_H86FEo72Wu44uiGMI_RZ-izJyg4G8qFlB0oilJ0FJWWOEJq-rg2BEghWh0j-F5sR0-kYybvRYpg7UNH3hGB5Q6AOZoTA7jeoYo1QNdo5MgFUsYOgbZccg2mduxNCfCx1CBvf20wuHvUACF0bq2YM0OkUaLFCGFJ4sfiMwHUdKI7mqo9c16DkjRACwVNImFsE20cCVWsTKBExPCl7PEYgOO3pqgyattExqwwH2mTLq5LgjlyWX4lC1GXvW0enC7F8XwIVP5ZkzNUaIbKNbE_n__adr7-1pTaXlP_T6o3x14dRMs0FUEWRoziInNZM1ljMreIZ-gkhZ2ePrJmP4yInhHwWabaf9fEyQl7oY42QZu4NwpCSI_FFGa9-DpH6JpXx1P4an_8GIvVTytJbCvP_IJaY5Zf10P5cFKj_gNqBp4F8OY7YFYJpB3IUxld8l_7g7PswvnSqGj58fCGjt6Dy1aR_LacZnZSfW1eBcr0xCPRw_3To4fu0fwMl9ghO2zSB7lvnPvzgDvZ_AQ" },
  { name: "Tokyo Electro", data: "13VZRj9MwDP4ryM_uSHrtusszIB7gCcTL1Iesc2lZ2vSarLCb9t9Rumh312PlBhuTkBU5rr_YjmOn2UJHrSl1DYIjLJoKBI8YginWea4IBEOopLHUftGqxzTSWmrrXqO0bkDYdk0IlV4SCDC6_gq4Z2LOkCFPEbJC1jUpA2K-BSOrRvXYTW0LsmVmXlu92ujgG9lgsZx8lx0gNKXNit5NI527SYzQuSDYZIag89yQ7dXZ2uo8B8GnjDGElozDJAjV2hKIXCpDLiKlvbDDXwexWAYsupr3TJXZKmDxsQDCy-9f6SZg4R9nYHZwf3O686K8vw_Y9Grpr8hKpcosYLfHYkgufwK6rFfXLEJjpXU5SC4fQXq4S_y1AGI-D5H1xD0fyCnOGY6SR_CBnUf8YIMP1vIjNp7pT4jjd4iROJGlKYK7dKcInUsOx1FK8T9DsEnk5ZGZS1LjsnOeA3mgyPPE8yB-BHHCEwpuz14bf4tI-yY_1lX8YbzIG3-6ZjgusqdhB4Yv7t3RvBy66qx1E_qaiX3NuILgfXbCf14UQTJQxScYSXfjPiD_AWILLXXULvbvwLKjmkx_5c9i99zT7dr0KsoK7T7fxPv5G1JyA4KzZC-_cxYmkdd-2tQZiAiB7j7sl9999Pw9CLZDqGXlflmf3Xvx1VtFmW017H4C" },
  { name: "Conga Jam", data: "1vVVNb5wwEP0r1ZyHrWF3k5Wvjaqqak6RekEcDB0WFGMT25BGq_3vlVmyYT8gPYQIgRn7-c3TMzPsoCVjS62AhwhpXQEP2QrBFk2eSwLOECphHZnfWnaYWjhHRnUrUusauDMNIVT6DwE_LiNYrbbA4wQhK4RSJC3weAdWVLXskGSyxvrcXzOttiJgLFw8ixYQ6tJlRZehFj7TYrlGaL0Attgg6Dy35Lr1rHE6z4GHN4wxBEPWY24RqsYR8FxIS16K1H2wx-sSUq22OmBsOSbhdm4FTqRy0oTV7Ca4skqFJDul4mZuEbb0c4EtxCOZMRXzW2FKobaSpqyIZlchclNmQgWvBRKNSdnMreS5tG7ajY8zIzl2mb5lAI_jCBkyDAfPt5EhSzA-Xx-iwh4x2HHJ0nOcMQ9nEozDN8Zr94BjhOsK4kzXVcQJLkkQfDv2xejNCXHy6mR_BsKfK1uscGx8n9N_Rx-n7JjTG1Z7pw42BtGJq4PoxPvlhf9-48XxrM9hEwf4mYhg9R8kyf69LJD_Bb4DQy2Z9LXvybIlRbar5c3a_-O1aWxX8JQV-vhyR1K8AI_Wffy9Y1gegocXlQFXjZQI9PTrsOnpvh9_AGd7BCUq34u--Rb45aeoYP8P" },
];

// Default kit: sample slugs from samples/manifest.json, one per channel.
const DEFAULT_KIT = [
  "tr909/909bd.wav", "tr909/909sd.wav", "tr909/909clap.wav", "tr808/tr-808-conga-mid.wav",
  "tr909/909hh.wav", "tr909/909oh.wav", "tr808/tr-808-cowbell.wav", "tr909/909ride.wav",
];

let manifest = [];             // manifest.json entries; index == AMY preset number
let loadedPresets = new Set(); // preset indices already transferred into AMY
let audioOn = false;

function newPattern() {
  return {
    steps: Array.from({ length: NUM_CH }, () => new Array(NUM_STEPS).fill(0)),   // 0 off, 1 on, 2 accent
    vel: Array.from({ length: NUM_CH }, () => new Array(NUM_STEPS).fill(1.0)),   // per-step velocity scale
    pitch: Array.from({ length: NUM_CH }, () => new Array(NUM_STEPS).fill(0)),   // per-step semitones
    length: NUM_STEPS,
  };
}

const state = {
  name: "Empty",
  bpm: 120,
  shuffle: 0,
  masterVol: 1.0,
  playing: false,
  pattern: 0,
  loop: true,
  mode: "pattern",  // or "song"
  song: [],         // list of pattern indices
  lane: "hit",      // step grid edit mode: hit | vel | pitch
  channels: [],     // {sample, pitch, pan, vol, offset, cutoff, res, mute, solo}
  patterns: [],
  fx: { reverb: 0, liveness: 0.85, chorus: 0, echo: 0, echoDelay: 250, echoFb: 0.3, echoSync: null, eqL: 0, eqM: 0, eqH: 0 },
};

const CUTOFF_MAX = 16000;   // knob at max = filter bypassed

for (let c = 0; c < NUM_CH; c++) {
  state.channels.push({ sample: 0, pitch: 0, pan: 0.5, vol: 0.8, offset: 0, cutoff: CUTOFF_MAX, res: 0.7, mute: false, solo: false });
}
for (let p = 0; p < NUM_PATTERNS; p++) state.patterns.push(newPattern());

// ---------------------------------------------------------------- persistence

const SAVE_KEY = "gamma9001-v1";
let stateLoaded = false;   // don't save until we've restored (or decided not to)
let saveTimer = null;

// Compact a pattern for saving: 0 when empty, and drop all-default vel/pitch lanes.
function compactPattern(p) {
  if (!p.steps.some(r => r.some(v => v))) return 0;
  const out = { s: p.steps, l: p.length };
  if (p.vel.some(r => r.some(v => v !== 1))) out.v = p.vel;
  if (p.pitch.some(r => r.some(v => v !== 0))) out.p = p.pitch;
  return out;
}

function buildSavePayload() {
  return {
    version: 1,
    name: state.name,
    bpm: state.bpm, shuffle: state.shuffle, masterVol: state.masterVol,
    pattern: state.pattern, loop: state.loop, mode: state.mode, song: state.song,
    // store samples as file paths so saves survive manifest reordering
    channels: state.channels.map(c => ({ ...c, sample: manifest[c.sample]?.file })),
    patterns: state.patterns.map(compactPattern),
    fx: state.fx,
  };
}

function saveState() {
  if (!stateLoaded) return;
  try { localStorage.setItem(SAVE_KEY, JSON.stringify(buildSavePayload())); } catch (e) { /* quota/private mode */ }
}

function scheduleSave() {
  clearTimeout(saveTimer);
  saveTimer = setTimeout(saveState, 400);
}

// Returns true if a saved state was restored.
function loadState() {
  try {
    return loadStateInner();
  } catch (e) {
    // a malformed save must never brick boot; drop it and start fresh
    console.warn("discarding unusable saved state:", e);
    try { localStorage.removeItem(SAVE_KEY); } catch (e2) { }
    return false;
  }
}

function loadStateInner() {
  const s = JSON.parse(localStorage.getItem(SAVE_KEY));
  return applySavePayload(s);
}

// Restore a save payload (from localStorage or a share URL) into state.
// Returns false if the payload isn't usable.
function applySavePayload(s) {
  if (!s || s.version !== 1) return false;
  state.name = String(s.name ?? "Empty").slice(0, 64);
  state.bpm = s.bpm ?? 120;
  state.shuffle = s.shuffle ?? 0;
  state.masterVol = s.masterVol ?? 1.0;
  state.pattern = Math.min(NUM_PATTERNS - 1, s.pattern ?? 0);
  state.loop = s.loop ?? true;
  state.mode = s.mode === "song" ? "song" : "pattern";
  state.song = (s.song || []).filter(p => p >= 0 && p < NUM_PATTERNS);
  (s.channels || []).slice(0, NUM_CH).forEach((c, i) => {
    const idx = manifest.findIndex(m => m.file === c.sample);
    state.channels[i] = {
      sample: idx >= 0 ? idx : state.channels[i].sample,
      pitch: c.pitch ?? 0, pan: c.pan ?? 0.5, vol: c.vol ?? 0.8, offset: c.offset ?? 0,
      cutoff: c.cutoff ?? CUTOFF_MAX, res: c.res ?? 0.7,
      mute: !!c.mute, solo: !!c.solo,
    };
  });
  (s.patterns || []).slice(0, NUM_PATTERNS).forEach((p, i) => {
    const np = newPattern();
    if (p) {  // 0/null = empty pattern; accept both compact (s/v/p/l) and legacy keys
      const steps = p.s ?? p.steps, vel = p.v ?? p.vel, pitch = p.p ?? p.pitch;
      for (let ch = 0; ch < NUM_CH; ch++) {
        for (let st = 0; st < NUM_STEPS; st++) {
          np.steps[ch][st] = steps?.[ch]?.[st] ?? 0;
          np.vel[ch][st] = vel?.[ch]?.[st] ?? 1.0;
          np.pitch[ch][st] = pitch?.[ch]?.[st] ?? 0;
        }
      }
      np.length = Math.max(1, Math.min(NUM_STEPS, p.l ?? p.length ?? NUM_STEPS));
    }
    state.patterns[i] = np;
  });
  if (s.fx) state.fx = { ...state.fx, ...s.fx };
  return true;
}

// ---------------------------------------------------------------- share URLs

function b64url(bytes) {
  let s = "";
  for (let i = 0; i < bytes.length; i += 0x8000)
    s += String.fromCharCode.apply(null, bytes.subarray(i, i + 0x8000));
  return btoa(s).replace(/\+/g, "-").replace(/\//g, "_").replace(/=+$/, "");
}

function unb64url(str) {
  const b = atob(str.replace(/-/g, "+").replace(/_/g, "/"));
  const u = new Uint8Array(b.length);
  for (let i = 0; i < b.length; i++) u[i] = b.charCodeAt(i);
  return u;
}

async function deflate(str) {
  const cs = new CompressionStream("deflate-raw");
  const ab = await new Response(new Blob([new TextEncoder().encode(str)]).stream().pipeThrough(cs)).arrayBuffer();
  return new Uint8Array(ab);
}

async function inflate(bytes) {
  const ds = new DecompressionStream("deflate-raw");
  const ab = await new Response(new Blob([bytes]).stream().pipeThrough(ds)).arrayBuffer();
  return new TextDecoder().decode(ab);
}

// #s=1<b64url deflate json> (or scheme 0 = uncompressed, for old browsers)
async function makeShareUrl() {
  const json = JSON.stringify(buildSavePayload());
  let scheme, bytes;
  if (typeof CompressionStream !== "undefined") { scheme = "1"; bytes = await deflate(json); }
  else { scheme = "0"; bytes = new TextEncoder().encode(json); }
  return `${location.origin}${location.pathname}#s=${scheme}${b64url(bytes)}`;
}

// Decode a share string: scheme char + b64url data.
async function decodeShare(str) {
  const bytes = unb64url(str.slice(1));
  const json = str[0] === "1" ? await inflate(bytes) : new TextDecoder().decode(bytes);
  return JSON.parse(json);
}

// Returns the decoded payload if the URL carries one, else null.
async function loadFromHash() {
  const m = location.hash.match(/[#&]s=([01][A-Za-z0-9_-]+)/);
  if (!m) return null;
  return decodeShare(m[1]);
}

// Load a full setup at runtime (from the examples dropdown or elsewhere):
// swap the state, rebuild every control from it, and push audio-side settings.
function loadSongState(payload) {
  stopPlayback();
  if (!applySavePayload(payload)) return false;
  rebuildUI();
  if (audioOn) {
    amy_send({ volume: state.masterVol });
    applyAllChannelFilters();
    applyFx();
  }
  saveState();
  return true;
}

// ---------------------------------------------------------------- sample loading

function wavDataChunk(buf) {
  const dv = new DataView(buf);
  let off = 12; // past RIFF....WAVE
  while (off + 8 <= dv.byteLength) {
    const id = String.fromCharCode(dv.getUint8(off), dv.getUint8(off + 1), dv.getUint8(off + 2), dv.getUint8(off + 3));
    const sz = dv.getUint32(off + 4, true);
    if (id === "data") return new Uint8Array(buf, off + 8, sz);
    off += 8 + sz + (sz & 1);
  }
  throw new Error("no data chunk in wav");
}

function b64chunk(bytes) {
  let s = "";
  for (let i = 0; i < bytes.length; i++) s += String.fromCharCode(bytes[i]);
  return btoa(s);
}

// Transfer one sample into an AMY memorypcm preset. The header message plus all
// base64 chunks are sent in one synchronous loop: nothing else may talk to AMY
// mid-transfer, so no awaits in here.
function sendSampleToAmy(presetIdx, wavBuf) {
  const entry = manifest[presetIdx];
  const data = wavDataChunk(wavBuf);
  const frames = data.length >> 1;
  amy_send({ load_sample: `${presetIdx},${frames},${entry.sr},60,0,0` });
  for (let i = 0; i < data.length; i += 188) {
    amy_add_message(b64chunk(data.subarray(i, Math.min(i + 188, data.length))));
  }
  loadedPresets.add(presetIdx);
}

async function loadPreset(presetIdx) {
  if (loadedPresets.has(presetIdx)) return;
  const entry = manifest[presetIdx];
  const buf = await (await fetch("samples/" + entry.file)).arrayBuffer();
  if (loadedPresets.has(presetIdx)) return; // raced with another load
  sendSampleToAmy(presetIdx, buf);
}

function setLoadStatus(text) {
  document.getElementById("loadstatus").textContent = text;
  document.getElementById("startlabel").textContent = text;
}

async function loadAllSamples() {
  // Kit samples first so the machine is playable immediately.
  for (const ch of state.channels) await loadPreset(ch.sample);
  setLoadStatus("kit ready");
  let n = 0;
  for (let i = 0; i < manifest.length; i++) {
    if (!loadedPresets.has(i)) {
      await loadPreset(i);
      if (++n % 8 === 0) {
        setLoadStatus(`loading samples ${loadedPresets.size}/${manifest.length}`);
        await new Promise(r => setTimeout(r, 0));
      }
    }
  }
  setLoadStatus(`${manifest.length} samples loaded`);
}

// ---------------------------------------------------------------- triggering

function triggerChannel(ch, time, stepVel, stepPitch, accent) {
  const c = state.channels[ch];
  const vel = c.vol * (stepVel ?? 1) * (accent ? ACCENT_GAIN : 1.0);
  const ev = {
    osc: OSC_BASE + ch,
    wave: AMY.PCM,
    preset: c.sample,
    note: 60 + c.pitch + (stepPitch ?? 0),
    pan: c.pan,
    vel: vel,
  };
  if (time !== undefined) ev.time = time;
  amy_send(ev);
  if (c.offset > 0) {
    // Start playback partway into the sample. pcm_note_on resets phase to 0, so
    // this must land as a separate delta *after* the note-on; AMY's queue keeps
    // insertion order for equal timestamps, so a second message at the same
    // time does exactly that.
    const startFrame = Math.floor(c.offset * manifest[c.sample].frames);
    const ph = { osc: OSC_BASE + ch, phase: startFrame / PCM_PHASE_DENOM };
    if (time !== undefined) ph.time = time;
    amy_send(ph);
  }
}

// ---------------------------------------------------------------- filter & fx

// Per-channel LPF24. Filter params persist on the osc, so we send them when a
// knob moves (and once at power-on), not on every trigger. Cutoff at max =
// bypass, so unfiltered channels skip the 4-pole filter entirely.
function applyChannelFilter(ch) {
  if (!audioOn) return;
  const c = state.channels[ch];
  amy_send({
    osc: OSC_BASE + ch,
    filter_type: c.cutoff >= CUTOFF_MAX ? AMY.FILTER_NONE : AMY.FILTER_LPF24,
    filter_freq: c.cutoff,
    resonance: c.res,
  });
}

function applyAllChannelFilters() {
  for (let ch = 0; ch < NUM_CH; ch++) applyChannelFilter(ch);
}

function applyFx() {
  if (!audioOn) return;
  const x = state.fx;
  amy_send({ reverb: `${x.reverb},${x.liveness},0.5,3000` });
  amy_send({ chorus: `${x.chorus},,,` });
  amy_send({ echo: `${x.echo},${x.echoDelay},743,${x.echoFb},0` });
  amy_send({ eq: `${x.eqL},${x.eqM},${x.eqH}` });
}

// ---------------------------------------------------------------- sequencer

let schedStep = 0;
let schedTime = 0;      // amy_sysclock ms
let schedSongPos = 0;
let endAt = null;       // sysclock ms to stop at (loop off), or null
let timer = null;
const playheadQueue = [];  // {time, step, pat, songPos}

function stepDurMs() { return 60000 / state.bpm / 4; }
function anySolo() { return state.channels.some(c => c.solo); }
function songActive() { return state.mode === "song" && state.song.length > 0; }
function schedPatternIdx() {
  if (!songActive()) return state.pattern;
  // the song may have been edited shorter while playing
  if (schedSongPos >= state.song.length) schedSongPos = 0;
  return state.song[schedSongPos];
}

function scheduleStep(patIdx, step, t) {
  const pat = state.patterns[patIdx];
  const solo = anySolo();
  for (let ch = 0; ch < NUM_CH; ch++) {
    const v = pat.steps[ch][step];
    if (!v) continue;
    const c = state.channels[ch];
    if (solo ? !c.solo : c.mute) continue;
    triggerChannel(ch, t, pat.vel[ch][step], pat.pitch[ch][step], v === 2);
  }
  playheadQueue.push({ time: t, step, pat: patIdx, songPos: schedSongPos });
}

function schedulerTick() {
  const now = amy_sysclock();
  while (endAt === null && schedTime < now + LOOKAHEAD_MS) {
    const patIdx = schedPatternIdx();
    const pat = state.patterns[patIdx];
    if (schedStep >= pat.length) {
      schedStep = 0;
      if (songActive()) {
        schedSongPos++;
        if (schedSongPos >= state.song.length) {
          if (state.loop) schedSongPos = 0;
          else { endAt = schedTime; break; }
        }
      } else if (!state.loop) { endAt = schedTime; break; }
      continue;
    }
    let t = schedTime;
    if (schedStep % 2 === 1) t += (state.shuffle / 100) * stepDurMs() * 0.5;
    scheduleStep(patIdx, schedStep, t);
    schedTime += stepDurMs();
    schedStep++;
  }
  // playhead UI
  while (playheadQueue.length && playheadQueue[0].time <= now) {
    const ph = playheadQueue.shift();
    if (songActive() && ph.pat !== state.pattern) {
      state.pattern = ph.pat;
      drawPatternButtons();
      drawSteps();
    }
    if (songActive()) markSongPos(ph.songPos);
    drawPlayhead(ph.step);
    document.getElementById("poslcd").textContent = `${ph.pat + 1}.${ph.step + 1}`;
  }
  if (endAt !== null && now >= endAt && playheadQueue.length === 0) stopPlayback();
}

function startPlayback() {
  if (state.playing) return;
  state.playing = true;
  schedStep = 0;
  schedSongPos = 0;
  endAt = null;
  if (songActive()) {
    state.pattern = state.song[0];
    drawPatternButtons(); drawSteps();
  }
  schedTime = amy_sysclock() + 100;
  timer = setInterval(schedulerTick, TICK_MS);
  document.getElementById("play").classList.add("active");
}

function stopPlayback() {
  state.playing = false;
  clearInterval(timer);
  timer = null;
  playheadQueue.length = 0;
  endAt = null;
  drawPlayhead(-1);
  markSongPos(-1);
  document.getElementById("play").classList.remove("active");
  document.getElementById("poslcd").textContent = `${state.pattern + 1}.1`;
}

// ---------------------------------------------------------------- knobs

function makeKnob(def, get, set) {
  const el = document.createElement("div");
  el.className = "knob";
  el.innerHTML = `<div class="knobdial"></div><div class="knoblabel">${def.label}</div><div class="knobval"></div>`;
  const dial = el.querySelector(".knobdial");
  const val = el.querySelector(".knobval");
  function draw() {
    const v = get();
    const frac = (v - def.min) / (def.max - def.min);
    dial.style.setProperty("--rot", `${-135 + frac * 270}deg`);
    val.textContent = def.fmt(v);
  }
  let dragging = null;
  el.addEventListener("pointerdown", e => {
    dragging = { y: e.clientY, v: get() };
    el.setPointerCapture(e.pointerId);
    e.preventDefault();
  });
  el.addEventListener("pointermove", e => {
    if (!dragging) return;
    const range = def.max - def.min;
    let v = dragging.v + (dragging.y - e.clientY) * range / 150;
    v = Math.max(def.min, Math.min(def.max, Math.round(v / def.step) * def.step));
    set(v);
    draw();
  });
  el.addEventListener("pointerup", () => { dragging = null; });
  if (def.reset !== undefined) {
    el.addEventListener("dblclick", () => { set(def.reset); draw(); });
  }
  draw();
  el._draw = draw;   // let external state changes refresh the dial
  return el;
}

// ---------------------------------------------------------------- channel UI

const LOG_CUTOFF_MIN = Math.log2(50);
const LOG_CUTOFF_MAX = Math.log2(CUTOFF_MAX);

function fmtHz(hz) {
  return hz >= CUTOFF_MAX ? "OPEN" : hz >= 1000 ? `${(hz / 1000).toFixed(1)}k` : `${hz}`;
}

const CH_KNOBS = [
  { key: "pitch", label: "Pitch", min: -24, max: 24, step: 1, reset: 0, fmt: v => (v > 0 ? "+" : "") + v },
  { key: "offset", label: "Offset", min: 0, max: 0.95, step: 0.01, reset: 0, fmt: v => v.toFixed(2) },
  { key: "pan", label: "Pan", min: 0, max: 1, step: 0.01, reset: 0.5, fmt: v => v.toFixed(2) },
  { key: "vol", label: "Vol", min: 0, max: 1.2, step: 0.01, reset: 0.8, fmt: v => v.toFixed(2) },
  // log-scaled LPF24 cutoff; at max the filter is bypassed
  { key: "cutoff", label: "Cutoff", min: LOG_CUTOFF_MIN, max: LOG_CUTOFF_MAX, step: 0.01, reset: LOG_CUTOFF_MAX,
    fmt: v => fmtHz(Math.round(2 ** v)), log: true, filter: true },
  { key: "res", label: "Res", min: 0.1, max: 8, step: 0.05, reset: 0.7, fmt: v => v.toFixed(2), filter: true },
];

const stepEls = [];   // [ch][step]

function buildChannels() {
  const holder = document.getElementById("channels");
  holder.innerHTML = "";
  stepEls.length = 0;
  for (let ch = 0; ch < NUM_CH; ch++) {
    const row = document.createElement("div");
    row.className = "channel";

    const led = document.createElement("div");
    led.className = "chled";
    row.appendChild(led);

    const name = document.createElement("button");
    name.className = "soundname";
    name.textContent = manifest[state.channels[ch].sample].name;
    name.addEventListener("click", () => openPicker(ch));
    row.appendChild(name);

    for (const def of CH_KNOBS) {
      row.appendChild(makeKnob(def,
        () => def.log ? Math.log2(state.channels[ch][def.key]) : state.channels[ch][def.key],
        v => {
          state.channels[ch][def.key] = def.log ? Math.round(2 ** v) : v;
          if (def.filter) applyChannelFilter(ch);
        }));
    }

    const ms = document.createElement("div");
    ms.className = "mutesolo";
    const mbtn = document.createElement("button");
    mbtn.className = "msbtn"; mbtn.textContent = "M";
    mbtn.classList.toggle("active-m", state.channels[ch].mute);
    mbtn.addEventListener("click", () => {
      state.channels[ch].mute = !state.channels[ch].mute;
      mbtn.classList.toggle("active-m", state.channels[ch].mute);
    });
    const sbtn = document.createElement("button");
    sbtn.className = "msbtn"; sbtn.textContent = "S";
    sbtn.classList.toggle("active-s", state.channels[ch].solo);
    sbtn.addEventListener("click", () => {
      state.channels[ch].solo = !state.channels[ch].solo;
      sbtn.classList.toggle("active-s", state.channels[ch].solo);
    });
    ms.appendChild(mbtn); ms.appendChild(sbtn);
    row.appendChild(ms);

    const steps = document.createElement("div");
    steps.className = "steps";
    stepEls.push([]);
    for (let s = 0; s < NUM_STEPS; s++) {
      const st = document.createElement("div");
      st.className = "step";
      st.appendChild(document.createElement("div")).className = "bar";
      attachStepHandlers(st, ch, s);
      steps.appendChild(st);
      stepEls[ch].push(st);
    }
    row.appendChild(steps);
    holder.appendChild(row);
  }
}

function attachStepHandlers(st, ch, s) {
  let dragging = null;
  st.addEventListener("pointerdown", e => {
    if (state.lane === "hit") return;
    const pat = state.patterns[state.pattern];
    const lane = state.lane === "vel" ? pat.vel : pat.pitch;
    dragging = { y: e.clientY, v: lane[ch][s], moved: false };
    st.setPointerCapture(e.pointerId);
    e.preventDefault();
  });
  st.addEventListener("pointermove", e => {
    if (!dragging) return;
    dragging.moved = true;
    const pat = state.patterns[state.pattern];
    if (state.lane === "vel") {
      let v = dragging.v + (dragging.y - e.clientY) / 60;
      pat.vel[ch][s] = Math.max(0, Math.min(1, v));
    } else {
      let v = dragging.v + (dragging.y - e.clientY) / 5;
      pat.pitch[ch][s] = Math.max(-12, Math.min(12, Math.round(v)));
    }
    drawStep(ch, s);
  });
  st.addEventListener("pointerup", () => { dragging = null; });
  st.addEventListener("click", () => {
    const pat = state.patterns[state.pattern];
    if (state.lane === "hit") {
      pat.steps[ch][s] = (pat.steps[ch][s] + 1) % 3;
      drawStep(ch, s);
      drawPatternButtons();
    }
  });
}

function drawStep(ch, s) {
  const pat = state.patterns[state.pattern];
  const v = pat.steps[ch][s];
  const el = stepEls[ch][s];
  el.classList.toggle("on", v === 1);
  el.classList.toggle("accent", v === 2);
  el.classList.toggle("off", v === 0);
  el.classList.toggle("beyond", s >= pat.length);
  const bar = el.firstChild;
  if (state.lane === "vel") {
    const h = pat.vel[ch][s] * 24;
    bar.style.bottom = "3px"; bar.style.height = `${h}px`;
    bar.classList.remove("negative");
    el.title = `vel ${pat.vel[ch][s].toFixed(2)}`;
  } else if (state.lane === "pitch") {
    const p = pat.pitch[ch][s];
    const h = Math.abs(p) / 12 * 12;
    if (p >= 0) { bar.style.bottom = "15px"; bar.style.height = `${Math.max(h, 1)}px`; }
    else { bar.style.bottom = `${15 - h}px`; bar.style.height = `${h}px`; }
    bar.classList.toggle("negative", p < 0);
    el.title = `pitch ${p > 0 ? "+" : ""}${p}`;
  } else {
    el.title = "";
  }
}

function drawSteps() {
  for (let ch = 0; ch < NUM_CH; ch++)
    for (let s = 0; s < NUM_STEPS; s++) drawStep(ch, s);
  document.getElementById("patlen").value = state.patterns[state.pattern].length;
}

function drawPlayhead(step) {
  for (let ch = 0; ch < NUM_CH; ch++)
    for (let s = 0; s < NUM_STEPS; s++)
      stepEls[ch][s].classList.toggle("playhead", s === step);
}

// ---------------------------------------------------------------- patterns & song

function patternHasData(p) {
  return state.patterns[p].steps.some(row => row.some(v => v));
}

function drawPatternButtons() {
  document.querySelectorAll(".patbtn").forEach((b, i) => {
    b.classList.toggle("current", i === state.pattern);
    b.classList.toggle("hasdata", patternHasData(i));
  });
}

// Rebuild the song chip list. Only called when the song itself changes —
// during playback we just move the .playing highlight (markSongPos), because
// rebuilding the DOM under the pointer would eat clicks on the chips.
function drawSongChips() {
  const holder = document.getElementById("songlist");
  holder.innerHTML = "";
  state.song.forEach((p, i) => {
    const chip = document.createElement("span");
    chip.className = "songchip";
    const sel = document.createElement("select");
    for (let n = 0; n < NUM_PATTERNS; n++) {
      const o = document.createElement("option");
      o.value = n;
      o.textContent = n + 1;
      if (n === p) o.selected = true;
      sel.appendChild(o);
    }
    sel.addEventListener("change", () => { state.song[i] = +sel.value; });
    const del = document.createElement("button");
    del.textContent = "×";
    del.title = "remove from song";
    del.addEventListener("click", () => {
      state.song.splice(i, 1);
      drawSongChips();
    });
    chip.appendChild(sel);
    chip.appendChild(del);
    holder.appendChild(chip);
  });
}

function markSongPos(playingPos) {
  document.querySelectorAll(".songchip").forEach((c, i) =>
    c.classList.toggle("playing", i === playingPos));
}

function buildPatterns() {
  const holder = document.getElementById("patterns");
  for (let p = 0; p < NUM_PATTERNS; p++) {
    const b = document.createElement("button");
    b.className = "patbtn";
    b.textContent = p + 1;
    b.addEventListener("click", () => {
      state.pattern = p;
      drawPatternButtons();
      drawSteps();
      if (!state.playing) document.getElementById("poslcd").textContent = `${p + 1}.1`;
    });
    holder.appendChild(b);
  }
  document.getElementById("copypat").addEventListener("click", () => {
    for (let p = 0; p < NUM_PATTERNS; p++) {
      if (!patternHasData(p) && p !== state.pattern) {
        const src = state.patterns[state.pattern];
        state.patterns[p] = {
          steps: src.steps.map(r => r.slice()),
          vel: src.vel.map(r => r.slice()),
          pitch: src.pitch.map(r => r.slice()),
          length: src.length,
        };
        state.pattern = p;
        drawPatternButtons(); drawSteps();
        return;
      }
    }
  });
  document.getElementById("clearpat").addEventListener("click", () => {
    state.patterns[state.pattern] = newPattern();
    drawPatternButtons(); drawSteps();
  });
  document.getElementById("modepat").addEventListener("click", () => setMode("pattern"));
  document.getElementById("modesong").addEventListener("click", () => setMode("song"));
  document.getElementById("songadd").addEventListener("click", () => {
    state.song.push(state.pattern);
    drawSongChips();
  });
}

function setMode(m) {
  state.mode = m;
  document.getElementById("modepat").classList.toggle("current", m === "pattern");
  document.getElementById("modesong").classList.toggle("current", m === "song");
  document.getElementById("patpanel").classList.toggle("songmode", m === "song");
}

// Rebuild every control from state — used at boot and when a whole setup is
// loaded (example song or share link applied at runtime).
function rebuildUI() {
  buildChannels();
  buildFx();
  setMode(state.mode);
  setLane(state.lane);
  drawPatternButtons();
  drawSongChips();
  document.getElementById("bpm").value = state.bpm;
  document.getElementById("shuffle").value = state.shuffle;
  document.getElementById("shuffleval").textContent = `${state.shuffle}%`;
  document.getElementById("loop").checked = state.loop;
  document.getElementById("songname").value = state.name;
  document.getElementById("poslcd").textContent = `${state.pattern + 1}.1`;
}

function setLane(lane) {
  state.lane = lane;
  document.querySelectorAll(".lanebtn").forEach(b => b.classList.toggle("current", b.dataset.lane === lane));
  document.body.classList.remove("lane-vel", "lane-pitch");
  if (lane !== "hit") document.body.classList.add(`lane-${lane}`);
  drawSteps();
}

// ---------------------------------------------------------------- filter & fx UI

function buildFx() {
  const fx = document.getElementById("fxknobs");
  fx.innerHTML = "";
  // gap: true inserts a group separator before that knob
  const defs = [
    { label: "Reverb", min: 0, max: 1, step: 0.01, reset: 0, key: "reverb", fmt: v => v.toFixed(2) },
    { label: "Liveness", min: 0, max: 1, step: 0.01, reset: 0.85, key: "liveness", fmt: v => v.toFixed(2) },
    { label: "Chorus", min: 0, max: 1, step: 0.01, reset: 0, key: "chorus", fmt: v => v.toFixed(2), gap: true },
    { label: "Echo", min: 0, max: 1, step: 0.01, reset: 0, key: "echo", fmt: v => v.toFixed(2), gap: true },
    { label: "Time", min: 20, max: 743, step: 1, reset: 250, key: "echoDelay", fmt: v => `${v}ms` },
    { label: "Feedbk", min: 0, max: 0.95, step: 0.01, reset: 0.3, key: "echoFb", fmt: v => v.toFixed(2) },
    // EQ shelves at AMY's low/mid/high centers, in dB
    { label: "EQ Lo", min: -15, max: 15, step: 0.5, reset: 0, key: "eqL", fmt: v => `${v > 0 ? "+" : ""}${v}dB`, gap: true },
    { label: "EQ Mid", min: -15, max: 15, step: 0.5, reset: 0, key: "eqM", fmt: v => `${v > 0 ? "+" : ""}${v}dB` },
    { label: "EQ Hi", min: -15, max: 15, step: 0.5, reset: 0, key: "eqH", fmt: v => `${v > 0 ? "+" : ""}${v}dB` },
    // master volume lives on state, not state.fx
    { label: "Volume", min: 0, max: 10, step: 0.1, reset: 1, key: "masterVol", master: true, fmt: v => v.toFixed(1), gap: true },
  ];
  for (const def of defs) {
    if (def.gap) {
      const gap = document.createElement("div");
      gap.className = "fxgap";
      fx.appendChild(gap);
    }
    const knob = def.master
      ? makeKnob(def,
          () => state.masterVol,
          v => {
            state.masterVol = v;
            if (audioOn) amy_send({ volume: v });
          })
      : makeKnob(def,
          () => state.fx[def.key],
          v => {
            state.fx[def.key] = v;
            if (def.key === "echoDelay") setEchoSync(null);  // manual move unsyncs
            applyFx();
          });
    fx.appendChild(knob);
    if (def.key === "echoDelay") {
      echoTimeKnob = knob;
      fx.appendChild(buildEchoSyncButtons());
    }
  }
}

// ---- echo tempo sync: divisor of a quarter note (1 = 1/4, 2 = 1/8, ...)

let echoTimeKnob = null;
const ECHO_SYNCS = [{ label: "1/4", div: 1 }, { label: "1/8", div: 2 }, { label: "1/16", div: 4 }, { label: "1/32", div: 8 }];

function echoSyncMs(div) {
  // clamped to AMY's echo max delay line
  return Math.min(743, Math.round(60000 / state.bpm / div));
}

function buildEchoSyncButtons() {
  const holder = document.createElement("div");
  holder.className = "syncbtns";
  for (const s of ECHO_SYNCS) {
    const b = document.createElement("button");
    b.className = "syncbtn" + (state.fx.echoSync === s.div ? " current" : "");
    b.textContent = s.label;
    b.title = "sync echo time to note length";
    b.dataset.div = s.div;
    b.addEventListener("click", () => setEchoSync(state.fx.echoSync === s.div ? null : s.div));
    holder.appendChild(b);
  }
  return holder;
}

function setEchoSync(div) {
  state.fx.echoSync = div;
  document.querySelectorAll(".syncbtn").forEach(b =>
    b.classList.toggle("current", div !== null && +b.dataset.div === div));
  if (div !== null) {
    state.fx.echoDelay = echoSyncMs(div);
    if (echoTimeKnob) echoTimeKnob._draw();
    applyFx();
  }
}

// keep a synced echo tracking the tempo
function refreshEchoSync() {
  if (state.fx.echoSync !== null) {
    state.fx.echoDelay = echoSyncMs(state.fx.echoSync);
    if (echoTimeKnob) echoTimeKnob._draw();
    applyFx();
  }
}

// ---------------------------------------------------------------- sound picker

let pickerChannel = 0;
let pickerBank = null;

function openPicker(ch) {
  pickerChannel = ch;
  pickerBank = manifest[state.channels[ch].sample].bank;
  document.getElementById("picker").classList.remove("hidden");
  drawPicker();
}

function drawPicker() {
  const banks = [];
  for (const m of manifest) if (!banks.includes(m.bank)) banks.push(m.bank);
  const bankHolder = document.getElementById("pickerbanks");
  bankHolder.innerHTML = "";
  for (const bank of banks) {
    const b = document.createElement("button");
    b.className = "bankbtn" + (bank === pickerBank ? " current" : "");
    b.textContent = manifest.find(m => m.bank === bank).bank_name;
    b.addEventListener("click", () => { pickerBank = bank; drawPicker(); });
    bankHolder.appendChild(b);
  }
  const list = document.getElementById("pickerlist");
  list.innerHTML = "";
  manifest.forEach((m, idx) => {
    if (m.bank !== pickerBank) return;
    const b = document.createElement("button");
    b.className = "samplebtn" + (idx === state.channels[pickerChannel].sample ? " current" : "");
    b.innerHTML = `${m.name}<span class="dur">${m.seconds.toFixed(2)}s</span>`;
    b.addEventListener("click", async () => {
      await loadPreset(idx);
      state.channels[pickerChannel].sample = idx;
      document.querySelectorAll(".soundname")[pickerChannel].textContent = m.name;
      drawPicker();
      if (audioOn) triggerChannel(pickerChannel);
    });
    list.appendChild(b);
  });
}

// ---------------------------------------------------------------- boot

function defaultPattern() {
  // a little four-on-the-floor starter so play does something out of the box
  const p = state.patterns[0].steps;
  for (let s = 0; s < 16; s += 4) p[0][s] = s === 0 ? 2 : 1;   // kick
  p[1][4] = 1; p[1][12] = 1;                                    // snare
  for (let s = 0; s < 16; s += 2) p[4][s] = 1;                  // closed hat
  p[5][14] = 1;                                                 // open hat
}

async function boot() {
  manifest = await (await fetch("samples/manifest.json")).json();
  DEFAULT_KIT.forEach((file, ch) => {
    const idx = manifest.findIndex(m => m.file === file);
    if (idx >= 0) state.channels[ch].sample = idx;
  });
  // a share link in the URL wins over the local save
  let sharedIn = false;
  try {
    const payload = await loadFromHash();
    if (payload) {
      sharedIn = applySavePayload(payload);
      if (sharedIn) history.replaceState(null, "", location.pathname);
    }
  } catch (e) {
    console.warn("ignoring bad share link:", e);
  }
  const restored = sharedIn || loadState();
  buildPatterns();
  if (!restored) defaultPattern();
  rebuildUI();

  // persist on any interaction (debounced), plus when the tab goes away
  for (const evt of ["click", "change", "input", "pointerup"]) {
    document.addEventListener(evt, scheduleSave);
  }
  document.addEventListener("visibilitychange", () => { if (document.hidden) saveState(); });
  stateLoaded = true;
  if (sharedIn) saveState();   // a loaded share link becomes the local state

  const shareBtn = document.getElementById("sharebtn");
  shareBtn.addEventListener("click", () => {
    // Safari revokes the user gesture as soon as the handler awaits, which
    // makes a post-await writeText() throw NotAllowedError. The sanctioned
    // workaround: synchronously hand the clipboard a ClipboardItem whose
    // content is a *promise* — the write is authorized now, resolved later.
    const urlPromise = makeShareUrl();
    const copied = () => {
      shareBtn.textContent = "Copied!";
      setTimeout(() => { shareBtn.textContent = "Share"; }, 2000);
    };
    const manual = async () => { window.prompt("Copy this link:", await urlPromise); };
    if (typeof ClipboardItem !== "undefined" && navigator.clipboard?.write) {
      const item = new ClipboardItem({
        "text/plain": urlPromise.then(u => new Blob([u], { type: "text/plain" })),
      });
      navigator.clipboard.write([item]).then(copied).catch(() =>
        // e.g. Firefox: no promise-in-ClipboardItem — try the direct path
        urlPromise.then(u => navigator.clipboard.writeText(u)).then(copied).catch(manual));
    } else {
      urlPromise.then(u => navigator.clipboard.writeText(u)).then(copied).catch(manual);
    }
  });

  document.getElementById("songname").addEventListener("input", e => {
    state.name = e.target.value.slice(0, 64);
  });

  const songSel = document.getElementById("songsel");
  for (const song of EXAMPLE_SONGS) {
    const o = document.createElement("option");
    o.value = song.name;
    o.textContent = song.name;
    songSel.appendChild(o);
  }
  songSel.addEventListener("change", async () => {
    const song = EXAMPLE_SONGS.find(s => s.name === songSel.value);
    songSel.value = "";   // let the same song be picked again later
    if (!song) return;
    try {
      loadSongState(await decodeShare(song.data));
    } catch (e) {
      console.warn("could not load example song:", e);
    }
  });

  document.getElementById("reset").addEventListener("click", () => {
    if (!confirm("Clear the saved song, patterns and settings?")) return;
    stateLoaded = false;             // block the unload/interaction saves
    localStorage.removeItem(SAVE_KEY);
    window.location.reload();
  });

  // Startup modal: one click starts audio (browsers require a gesture) and
  // loads the samples, then the machine is fully live.
  const startModal = document.getElementById("startmodal");
  startModal.addEventListener("click", async () => {
    if (audioOn) return;
    startModal.classList.add("loading");
    setLoadStatus("starting audio…");
    await amy_js_start();
    audioOn = true;
    amy_send({ volume: state.masterVol });
    applyAllChannelFilters();
    applyFx();
    await loadAllSamples();
    startModal.classList.add("hidden");
  });

  document.getElementById("play").addEventListener("click", () => {
    if (!audioOn) return;
    startPlayback();
  });
  document.getElementById("stop").addEventListener("click", stopPlayback);
  document.getElementById("bpm").addEventListener("input", e => {
    state.bpm = Math.max(40, Math.min(240, +e.target.value || 120));
    refreshEchoSync();
  });
  document.getElementById("shuffle").addEventListener("input", e => {
    state.shuffle = +e.target.value;
    document.getElementById("shuffleval").textContent = `${state.shuffle}%`;
  });
  document.getElementById("patlen").addEventListener("input", e => {
    const v = Math.max(1, Math.min(16, +e.target.value || 16));
    state.patterns[state.pattern].length = v;
    drawSteps();
  });
  document.getElementById("loop").addEventListener("change", e => { state.loop = e.target.checked; });
  document.querySelectorAll(".lanebtn").forEach(b => {
    b.addEventListener("click", () => setLane(b.dataset.lane));
  });
  document.getElementById("pickerclose").addEventListener("click", () => {
    document.getElementById("picker").classList.add("hidden");
  });
  document.getElementById("picker").addEventListener("click", e => {
    if (e.target.id === "picker") document.getElementById("picker").classList.add("hidden");
  });

  // keyboard: space = play/stop
  document.addEventListener("keydown", e => {
    if (e.code === "Space" && e.target.tagName !== "INPUT") {
      e.preventDefault();
      state.playing ? stopPlayback() : (audioOn && startPlayback());
    }
  });
}

boot();
