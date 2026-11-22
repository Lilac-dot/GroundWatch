import { createClient } from "https://cdn.jsdelivr.net/npm/@supabase/supabase-js@2.45.4/+esm";
import { SUPABASE_URL, SUPABASE_KEY, DEVICES, ONLINE_SECONDS, LDR_DARK } from "./config.js";
import { lineChart, barChart, table } from "./charts.js";

const $ = (id) => document.getElementById(id);
const sb = createClient(SUPABASE_URL, SUPABASE_KEY);

const ZONES = [
  { key: "l", name: "Left", short: "L", color: "var(--zone-l)" },
  { key: "c", name: "Centre", short: "C", color: "var(--zone-c)" },
  { key: "r", name: "Right", short: "R", color: "var(--zone-r)" },
];
const ZONE_BY_LETTER = { L: ZONES[0], C: ZONES[1], R: ZONES[2] };
const ALARMS = new Set(["PERSON", "DIGGING"]);
const ALARM_HOLD_S = 30;   // the status card stays red this long after an alarm

const ICONS = {
  PERSON: '<path d="M9 4.5c1.4 0 2.2 1.5 2 3.4-.2 1.8-1.2 3.1-2.3 3-1.1 0-1.8-1.5-1.6-3.3C7.3 5.7 8 4.5 9 4.5zM8.6 13.2c1 0 1.6.8 1.4 1.9l-.3 1.6c-.2 1-1 1.6-1.8 1.4-.8-.1-1.2-1-1-2l.3-1.5c.2-.9.7-1.4 1.4-1.4zM15.4 8c1 0 1.7 1.3 1.6 2.8-.2 1.6-1.1 2.7-2 2.6-1 0-1.6-1.3-1.4-2.9.2-1.5.9-2.5 1.8-2.5zM15 15.6c.8 0 1.3.7 1.2 1.6l-.2 1.3c-.2.9-.9 1.4-1.6 1.2-.7-.1-1-.9-.9-1.7l.3-1.3c.1-.7.6-1.1 1.2-1.1z"/>',
  DIGGING: '<path d="M14 3l7 7M17.5 6.5 9 15M9 15l-3.5 3.5a2.1 2.1 0 0 1-3-3L6 12l3 3zM12.5 4.5l7 7"/>',
  ANIMAL: '<circle cx="7" cy="9" r="1.8"/><circle cx="11" cy="6.5" r="1.8"/><circle cx="15.5" cy="7" r="1.8"/><circle cx="18.5" cy="10.5" r="1.8"/><path d="M8.5 16.5c0-2.5 2-4.5 4.5-4.5s4 2 4 4c0 2-1.5 2.8-3 2.5-1.2-.2-1.8-.2-3 .2-1.6.5-2.5-.5-2.5-2.2z"/>',
  NOISE: '<path d="M3 9h11a3 3 0 1 0-3-3M3 13h15a3 3 0 1 1-3 3M3 17h7"/>',
  QUIET: '<path d="M12 21c-4-3-7-6.5-7-10.5A7 7 0 0 1 12 3a7 7 0 0 1 7 7.5c0 4-3 7.5-7 10.5z"/><path d="M12 8v6M9.5 11.5 12 14l2.5-2.5"/>',
  OFFLINE: '<path d="M4 8.5a12 12 0 0 1 16 0M7 12a7.5 7.5 0 0 1 10 0M10 15.5a3 3 0 0 1 4 0"/><path d="M4 4l16 16"/>',
};
const icon = (k) => `<svg viewBox="0 0 24 24" aria-hidden="true">${ICONS[k]}</svg>`;
const EVENT_TEXT = { PERSON: "Person", DIGGING: "Digging", ANIMAL: "Animal", NOISE: "Noise" };
const TIPS = [
  "Footsteps arrive at a steady rhythm. Animals don't.",
  "The zone is whichever piezo felt the step first.",
  "Wind raises the background level, and the trigger rises with it.",
  "At night the board lowers its trigger level.",
  "No PIR, no alarm: ground noise alone is logged, not raised.",
];

// ------------------------------ state ------------------------------
const state = {
  preview: false,
  device: DEVICES[0],
  hours: 1,
  minutes: [],   // per-minute summaries, oldest first
  events: [],    // non-NONE windows, newest first
  latest: null,  // newest window of any kind
  channel: null,
};

// ------------------------------ auth ------------------------------
async function boot() {
  applyTheme(loadTheme());
  if (new URLSearchParams(location.search).has("preview")) return startPreview();
  const { data } = await sb.auth.getSession();
  if (data.session) startApp(data.session);
  else showLogin();
  sb.auth.onAuthStateChange((evt, session) => {
    if (evt === "SIGNED_OUT") showLogin();
  });
}

function showLogin() {
  $("app").hidden = true;
  $("login").hidden = false;
}

$("login-form").addEventListener("submit", async (e) => {
  e.preventDefault();
  $("login-error").textContent = "";
  const btn = e.submitter;
  btn.disabled = true;
  const { data, error } = await sb.auth.signInWithPassword({ email: $("email").value.trim(), password: $("password").value });
  btn.disabled = false;
  if (error) { $("login-error").textContent = error.message; return; }
  startApp(data.session);
});
$("preview-btn").addEventListener("click", () => {
  history.replaceState(null, "", "?preview");
  startPreview();
});
$("signout-btn").addEventListener("click", async () => {
  if (state.preview) { history.replaceState(null, "", location.pathname); location.reload(); return; }
  await sb.auth.signOut();
});

// ------------------------------ shell ------------------------------
function initShell() {
  $("login").hidden = true;
  $("app").hidden = false;
  const sel = $("device");
  sel.replaceChildren(...DEVICES.map((d) => { const o = document.createElement("option"); o.value = o.textContent = d; return o; }));
  sel.value = state.device;
  sel.onchange = () => { state.device = sel.value; reload(); };
  for (const b of $("range").querySelectorAll("button")) {
    b.onclick = () => {
      for (const x of $("range").querySelectorAll("button")) x.setAttribute("aria-checked", x === b);
      state.hours = +b.dataset.h;
      reload();
    };
  }
  for (const b of document.querySelectorAll("[data-table]")) {
    b.onclick = () => {
      const k = b.dataset.table, t = $(`table-${k}`), chart = t.previousElementSibling;
      t.hidden = !t.hidden;
      chart.hidden = !t.hidden;
      b.textContent = t.hidden ? "Table" : "Chart";
    };
  }
  const h = new Date().getHours();
  $("greeting").textContent = h < 12 ? "Good morning" : h < 18 ? "Good afternoon" : "Good evening";
  $("tip-text").textContent = TIPS[Math.floor(Math.random() * TIPS.length)];
  drawMapBase();
  tickClock();
  setInterval(tickClock, 1000);
  addEventListener("resize", debounce(render, 150));
}

async function startApp() {
  state.preview = false;
  initShell();
  $("preview-chip").hidden = true;
  await reload();
}

function startPreview() {
  state.preview = true;
  state.device = "demo";
  initShell();
  $("preview-chip").hidden = false;
  $("device").disabled = true;
  $("subtitle").textContent = "Sample data generated in your browser, not from the board";
  demoInit();
  render();
  setInterval(() => { demoTick(); render(); }, 2000);
}

// ------------------------------ data ------------------------------
async function reload() {
  if (state.preview) { render(); return; }
  document.querySelectorAll(".chart").forEach((c) => c.classList.add("loading"));
  const since = new Date(Date.now() - state.hours * 3600e3).toISOString();
  try {
    const [minutes, events, latest] = await Promise.all([
      fetchAll(sb.from("events_minutely").select("*").eq("device", state.device).gte("minute", since).order("minute")),
      sb.from("events")
        .select("created_at,event,zone,peak_l,peak_c,peak_r,hits_l,hits_c,hits_r,pir,night")
        .eq("device", state.device).neq("event", "NONE").gte("created_at", since)
        .order("created_at", { ascending: false }).limit(100),
      sb.from("events").select("created_at,event,zone,ldr,night").eq("device", state.device)
        .order("created_at", { ascending: false }).limit(1),
    ]);
    if (events.error) throw events.error;
    state.minutes = minutes.map(normMinute);
    state.events = events.data;
    state.latest = latest.data?.[0] || null;
  } catch (err) {
    console.error(err);
    setConn("off", "Can't read data");
  }
  document.querySelectorAll(".chart").forEach((c) => c.classList.remove("loading"));
  subscribe();
  render();
}

// PostgREST returns at most 1000 rows per request; 24 h of minutes is 1440
async function fetchAll(query) {
  const out = [];
  for (let from = 0; ; from += 1000) {
    const { data, error } = await query.range(from, from + 999);
    if (error) throw error;
    out.push(...data);
    if (data.length < 1000) return out;
  }
}

function normMinute(r) {
  return { ...r, t: new Date(r.minute).getTime() };
}

function subscribe() {
  if (state.channel) sb.removeChannel(state.channel);
  state.channel = sb.channel(`events-${state.device}`)
    .on("postgres_changes", { event: "INSERT", schema: "public", table: "events", filter: `device=eq.${state.device}` },
        (msg) => { addWindow(msg.new); render(); })
    .subscribe((s) => { if (s === "SUBSCRIBED") setConn(null); });
}

// fold one new 2 s window into the client-side state (live updates and preview)
function addWindow(w) {
  state.latest = w;
  if (w.event !== "NONE") {
    state.events.unshift(w);
    state.events.length = Math.min(state.events.length, 100);
    state.flash = { zone: w.zone, at: Date.now() };
  }
  const t = Math.floor(new Date(w.created_at).getTime() / 60000) * 60000;
  let m = state.minutes[state.minutes.length - 1];
  if (!m || m.t !== t) {
    m = { t, minute: new Date(t).toISOString(), peak_l: 0, peak_c: 0, peak_r: 0, hits_l: 0, hits_c: 0, hits_r: 0,
          ldr: w.ldr, night: false, pir: false, person: 0, digging: 0, animal: 0, noise: 0, windows: 0, _ldrSum: 0 };
    state.minutes.push(m);
  }
  for (const z of "lcr") {
    m[`peak_${z}`] = Math.max(m[`peak_${z}`], w[`peak_${z}`] || 0);
    m[`hits_${z}`] += w[`hits_${z}`] || 0;
  }
  m.windows += 1;
  m._ldrSum = (m._ldrSum ?? m.ldr * (m.windows - 1)) + (w.ldr ?? 0);
  m.ldr = Math.round(m._ldrSum / m.windows);
  m.night ||= !!w.night;
  m.pir ||= !!w.pir;
  const k = { PERSON: "person", DIGGING: "digging", ANIMAL: "animal", NOISE: "noise" }[w.event];
  if (k) m[k] += 1;
  const cutoff = Date.now() - 24 * 3600e3;          // keep the longest range; render() slices
  while (state.minutes.length && state.minutes[0].t < cutoff) state.minutes.shift();
}

// ------------------------------ render ------------------------------
function render() {
  const now = Date.now(), t0 = now - state.hours * 3600e3;
  const mins = state.minutes.filter((m) => m.t >= t0);
  const evs = state.events.filter((e) => new Date(e.created_at).getTime() >= t0);

  renderStatus(now);
  renderTiles(now, mins);
  renderMap(now, mins);
  renderFeed(evs);

  // ground activity
  $("activity-legend").replaceChildren(...ZONES.map((z) => {
    const s = document.createElement("span"), i = document.createElement("i");
    i.style.background = z.color;
    s.append(i, document.createTextNode(`${z.name} (${z.short})`));
    return s;
  }));
  // longer ranges group minutes (strongest reading per group) so the lines stay readable
  const stepMin = state.hours === 1 ? 1 : state.hours === 6 ? 5 : 15;
  const grouped = groupPeaks(mins, stepMin * 60e3);
  const per = stepMin === 1 ? "minute" : `${stepMin} minutes`;
  $("activity-sub").textContent = `Strongest vibration per ${per}, by zone (ADC counts above baseline)`;
  lineChart($("chart-activity"), {
    t0, t1: now, label: `Strongest vibration per ${per} by zone`,
    series: ZONES.map((z) => ({ name: z.name, short: z.short, color: z.color })),
    points: grouped.map((m) => ({ t: m.t, values: [m.peak_l, m.peak_c, m.peak_r] })),
    ymin: 20, endLabels: true, gapMs: stepMin * 60e3 * 2,
  });
  table($("table-activity"), [stepMin === 1 ? "Minute" : "From", "Left", "Centre", "Right"],
    [...grouped].reverse().map((m) => [fmtTime(m.t), m.peak_l, m.peak_c, m.peak_r]));

  // alarms per hour (or per 10 min for the 1 h view)
  const bucket = state.hours === 1 ? 10 * 60e3 : 3600e3;
  const n = Math.round((state.hours * 3600e3) / bucket);
  const start = Math.floor(now / bucket) * bucket - (n - 1) * bucket;
  const counts = Array.from({ length: n }, (_, i) => ({ t: start + i * bucket, v: 0 }));
  for (const m of mins) {
    const i = Math.floor((m.t - start) / bucket);
    if (i >= 0 && i < n) counts[i].v += m.person + m.digging;
  }
  const alarmsTitle = state.hours === 1 ? "Alarms per 10 minutes" : "Alarms per hour";
  $("chart-alarms").closest(".card").querySelector("h2").textContent = alarmsTitle;
  barChart($("chart-alarms"), {
    label: alarmsTitle, unit: "alarms",
    bars: counts.map((c) => ({
      label: fmtTime(c.t), value: c.v, color: "var(--series-alarm)",
      tip: `${fmtTime(c.t)} – ${fmtTime(c.t + bucket)}`,
    })),
  });
  table($("table-alarms"), ["From", "Alarms"], [...counts].reverse().map((c) => [fmtTime(c.t), c.v]));

  // hits by zone
  const hits = ZONES.map((z) => mins.reduce((a, m) => a + (m[`hits_${z.key}`] || 0), 0));
  barChart($("chart-zones"), {
    horizontal: true, label: "Hits by zone", unit: "hits",
    bars: ZONES.map((z, i) => ({ label: z.name, value: hits[i], color: z.color })),
  });
  table($("table-zones"), ["Zone", "Hits"], ZONES.map((z, i) => [z.name, hits[i]]));

  // light
  lineChart($("chart-light"), {
    t0, t1: now, label: "Light level per minute",
    series: [{ name: "Light (LDR)", color: "var(--series-light)" }],
    points: mins.filter((m) => m.ldr != null).map((m) => ({ t: m.t, values: [m.ldr] })),
    ymax: 256, ref: { value: LDR_DARK, label: "night" },
  });
  table($("table-light"), ["Minute", "LDR", "Mode"],
    [...mins].reverse().map((m) => [fmtTime(m.t), m.ldr ?? "–", m.night ? "night" : "day"]));
}

function renderStatus(now) {
  const card = $("status-card"), lat = state.latest;
  const lastAlarm = state.events.find((e) => ALARMS.has(e.event));
  const alarmAge = lastAlarm ? (now - new Date(lastAlarm.created_at).getTime()) / 1000 : Infinity;
  const seenAge = lat ? (now - new Date(lat.created_at).getTime()) / 1000 : Infinity;
  card.classList.toggle("alarm", alarmAge < ALARM_HOLD_S);
  if (alarmAge < ALARM_HOLD_S) {
    const z = ZONE_BY_LETTER[lastAlarm.zone];
    $("status-icon").innerHTML = icon(lastAlarm.event);
    $("status-eyebrow").textContent = "Alarm";
    $("status-title").textContent = `${EVENT_TEXT[lastAlarm.event]} detected`;
    $("status-sub").textContent = `${z ? `${z.name} zone (${z.short})` : "Zone unknown"} · ${ago(alarmAge)}`;
    $("status-icon").style.cssText = "";
  } else if (seenAge > ONLINE_SECONDS) {
    $("status-icon").innerHTML = icon("OFFLINE");
    $("status-icon").style.cssText = "background: var(--warn-bg); color: var(--warn)";
    $("status-eyebrow").textContent = "Status";
    $("status-title").textContent = lat ? "Board offline" : "No data yet";
    $("status-sub").textContent = lat ? `Last window ${ago(seenAge)}` : "Start the bridge: groundwatch_pc.py --cloud";
  } else {
    $("status-icon").innerHTML = icon("QUIET");
    $("status-icon").style.cssText = "";
    $("status-eyebrow").textContent = "Status";
    $("status-title").textContent = "All quiet";
    $("status-sub").textContent = lastAlarm ? `Last alarm ${ago(alarmAge)}` : "Listening on all three zones";
  }
  if (!state.preview || seenAge <= ONLINE_SECONDS) setConn(seenAge <= ONLINE_SECONDS ? "on" : "off",
    seenAge <= ONLINE_SECONDS ? "Board online" : lat ? `Seen ${ago(seenAge)}` : "No data yet");
}

function renderTiles(now, mins) {
  const alarms = mins.reduce((a, m) => a + m.person + m.digging, 0);
  const dets = mins.reduce((a, m) => a + m.person + m.digging + m.animal, 0);
  $("t-alarms").textContent = alarms;
  $("t-alarms-foot").textContent = `last ${state.hours === 1 ? "hour" : `${state.hours} hours`}`;
  $("t-events").textContent = dets;
  const lat = state.latest;
  if (lat) {
    const age = (now - new Date(lat.created_at).getTime()) / 1000;
    $("t-seen").textContent = age < 60 ? `${Math.round(age)} s` : ago(age).replace(" ago", "");
    $("t-seen-foot").textContent = `ago · ${new Date(lat.created_at).toLocaleTimeString()}`;
    $("t-light").textContent = lat.night ? "Night" : "Day";
    $("t-light-foot").textContent = `LDR ${lat.ldr ?? "–"} · night below ${LDR_DARK}`;
  } else {
    for (const id of ["t-seen", "t-light"]) $(id).textContent = "–";
  }
}

// ------------------------------ map ------------------------------
const ZX = [70, 180, 290];
function drawMapBase() {
  const svg = $("map");
  svg.innerHTML = `
    <rect class="field" x="4" y="4" width="352" height="158" rx="16"/>
    <g class="trees">${[[22, 30], [40, 22], [330, 28], [312, 138], [30, 136], [345, 128], [150, 24], [212, 150]]
      .map(([x, y]) => `<circle class="tree" cx="${x}" cy="${y}" r="${9 + (x % 5)}"/>`).join("")}</g>
    <path class="path" d="M18 146 C 90 134, 140 154, 200 143 S 300 132, 344 141"/>
    <path class="fence" d="M14 60 H 346"/>
    ${ZONES.map((z, i) => `
      <g data-zone="${z.short}">
        <circle class="ring-pulse" cx="${ZX[i]}" cy="60" r="24" fill="none" stroke-width="2" style="stroke:${z.color}" opacity="0"/>
        <circle class="zone-ring" cx="${ZX[i]}" cy="60" r="18" style="stroke:${z.color}"/>
        <circle class="zone-dot" cx="${ZX[i]}" cy="60" r="8" style="fill:${z.color}"/>
        <text class="zone-label" x="${ZX[i]}" y="34">${z.name}</text>
        <text class="zone-val" x="${ZX[i]}" y="100" id="map-val-${z.short}">–</text>
        <text class="zone-cap" x="${ZX[i]}" y="112">peak · 5 min</text>
      </g>`).join("")}`;
}

function renderMap(now, mins) {
  const last5 = mins.filter((m) => m.t >= now - 5 * 60e3);
  ZONES.forEach((z) => {
    const peak = last5.reduce((a, m) => Math.max(a, m[`peak_${z.key}`] || 0), 0);
    $(`map-val-${z.short}`).textContent = peak;
  });
  const f = state.flash;
  for (const g of $("map").querySelectorAll("[data-zone]")) {
    const ring = g.querySelector(".ring-pulse");
    const hot = f && f.zone === g.dataset.zone && now - f.at < 12000;
    ring.classList.toggle("hot", !!hot);
    ring.setAttribute("opacity", hot ? "1" : "0");
  }
}

// ------------------------------ feed ------------------------------
let feedKeys = new Set();
function renderFeed(evs) {
  const list = $("feed");
  const shown = evs.slice(0, 40);
  $("feed-empty").hidden = shown.length > 0;
  $("feed-count").textContent = evs.length ? `${evs.length} in range` : "";
  const keys = new Set();
  list.replaceChildren(...shown.map((e) => {
    const key = `${e.created_at}${e.event}`;
    keys.add(key);
    const li = document.createElement("li");
    if (feedKeys.size && !feedKeys.has(key)) li.className = "new";
    const ic = document.createElement("span");
    ic.className = `ic ${ALARMS.has(e.event) ? "alarm" : e.event === "ANIMAL" ? "warn" : "calm"}`;
    ic.innerHTML = icon(e.event);
    const body = document.createElement("div");
    const what = document.createElement("div");
    what.className = "what";
    what.textContent = EVENT_TEXT[e.event] + (ALARMS.has(e.event) ? " · alarm" : e.event === "NOISE" ? " · ignored" : " · logged");
    const meta = document.createElement("div");
    meta.className = "meta";
    const z = e.zone && e.zone.toLowerCase();
    const pk = z ? e[`peak_${z}`] : Math.max(e.peak_l, e.peak_c, e.peak_r);
    const hits = z ? e[`hits_${z}`] : 0;
    meta.textContent = `${new Date(e.created_at).toLocaleTimeString()} · peak ${pk} · ${hits} hit${hits === 1 ? "" : "s"}${e.pir ? " · PIR" : ""}${e.night ? " · night" : ""}`;
    body.append(what, meta);
    const zone = document.createElement("span");
    zone.className = "zone";
    const zz = ZONE_BY_LETTER[e.zone];
    zone.textContent = zz ? zz.name : "–";
    if (zz) { zone.style.borderColor = zz.color; zone.style.color = "var(--ink)"; }
    li.append(ic, body, zone);
    return li;
  }));
  feedKeys = keys;
}

// ------------------------------ preview data ------------------------------
// Plausible windows generated in the browser (no database), patterned on
// groundwatch_pc.py --demo: quiet most of the time, people by day, animals
// mostly at night, occasional wind noise.
let demoT;
function demoWindow(t) {
  const hour = new Date(t).getHours() + new Date(t).getMinutes() / 60;
  const night = hour < 6 || hour > 19;
  const ldr = Math.max(8, Math.round(night ? 25 + 15 * Math.random() : 120 + 110 * Math.sin(Math.PI * (hour - 6) / 13)));
  const r = Math.random();
  // per 2 s window: a person every ~10 min by day, digging rarely, animals mostly at night
  const pPerson = night ? 0.0008 : 0.0035, pDig = 0.0004, pAnimal = night ? 0.004 : 0.001, pNoise = 0.006;
  const ev = r < pPerson ? "PERSON" : r < pPerson + pDig ? "DIGGING" : r < pPerson + pDig + pAnimal ? "ANIMAL"
    : r < pPerson + pDig + pAnimal + pNoise ? "NOISE" : "NONE";
  const z = Math.random() < 0.5 ? 1 : Math.random() < 0.6 ? 0 : 2;   // the centre path is busiest
  const pk = { NONE: 0, NOISE: 26, ANIMAL: 30, PERSON: 64, DIGGING: 150 }[ev];
  const w = { created_at: new Date(t).toISOString(), event: ev, zone: ev === "NONE" ? null : "LCR"[z],
              pir: ev === "PERSON" || ev === "DIGGING" || ev === "ANIMAL", night, ldr };
  "lcr".split("").forEach((k, i) => {
    const p = pk ? Math.max(0, pk - Math.abs(i - z) * 22 + Math.round(8 * (Math.random() - 0.5))) : Math.round(4 * Math.random());
    w[`peak_${k}`] = p;
    w[`hits_${k}`] = p > 20 ? (ev === "PERSON" ? 3 + (i === z) : 1 + Math.floor(3 * Math.random())) : 0;
  });
  return w;
}
function demoInit() {
  state.minutes = []; state.events = []; state.latest = null;
  const now = Date.now();
  state.hours = 24;
  for (let t = now - 24 * 3600e3; t <= now; t += 2000) addWindow(demoWindow(t));
  state.hours = +$("range").querySelector('[aria-checked="true"]').dataset.h;
  // one fresh alarm so the preview shows the alarm state
  const w = demoWindow(now); w.event = "PERSON"; w.zone = "C"; w.pir = true; w.peak_c = 71; w.hits_c = 4;
  addWindow(w);
  state.flash = { zone: "C", at: now };
  demoT = now;
}
function demoTick() {
  demoT = Date.now();
  addWindow(demoWindow(demoT));
}

// ------------------------------ helpers ------------------------------
function setConn(kind, text) {
  const c = $("conn-chip");
  if (kind) { c.classList.toggle("on", kind === "on"); c.classList.toggle("off", kind === "off"); }
  if (text) $("conn-text").textContent = text;
}
function tickClock() {
  $("clock").textContent = new Date().toLocaleString([], { weekday: "short", day: "numeric", month: "short", hour: "2-digit", minute: "2-digit" });
}
function groupPeaks(mins, ms) {
  if (ms <= 60e3) return mins;
  const out = [];
  for (const m of mins) {
    const t = Math.floor(m.t / ms) * ms;
    let g = out[out.length - 1];
    if (!g || g.t !== t) { g = { t, peak_l: 0, peak_c: 0, peak_r: 0 }; out.push(g); }
    for (const z of "lcr") g[`peak_${z}`] = Math.max(g[`peak_${z}`], m[`peak_${z}`] || 0);
  }
  return out;
}
function fmtTime(t) { return new Date(t).toLocaleTimeString([], { hour: "2-digit", minute: "2-digit" }); }
function ago(s) {
  if (s < 60) return `${Math.max(1, Math.round(s))} s ago`;
  if (s < 3600) return `${Math.round(s / 60)} min ago`;
  if (s < 86400) return `${Math.round(s / 3600)} h ago`;
  return `${Math.round(s / 86400)} d ago`;
}
function debounce(f, ms) { let t; return () => { clearTimeout(t); t = setTimeout(f, ms); }; }

function loadTheme() { try { return localStorage.getItem("gw-theme"); } catch { return null; } }
function applyTheme(t) {
  if (t === "light" || t === "dark") document.documentElement.dataset.theme = t;
  else delete document.documentElement.dataset.theme;
}
$("theme-btn").addEventListener("click", () => {
  const dark = document.documentElement.dataset.theme === "dark" ||
    (!document.documentElement.dataset.theme && matchMedia("(prefers-color-scheme: dark)").matches);
  const next = dark ? "light" : "dark";
  applyTheme(next);
  try { localStorage.setItem("gw-theme", next); } catch { /* private mode */ }
  render();
});

boot();
