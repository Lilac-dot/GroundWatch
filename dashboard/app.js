import { createClient } from "https://cdn.jsdelivr.net/npm/@supabase/supabase-js@2.45.4/+esm";
import { SUPABASE_URL, SUPABASE_KEY, DEVICES, ONLINE_SECONDS, LDR_DARK } from "./config.js";
import { lineChart, table } from "./charts.js?v=4";

const $ = (id) => document.getElementById(id);
const sb = createClient(SUPABASE_URL, SUPABASE_KEY);
const reduceMotion = () => matchMedia("(prefers-reduced-motion: reduce)").matches;

const ZONES = [
  { key: "l", name: "Left", short: "L", color: "var(--zone-l)" },
  { key: "c", name: "Centre", short: "C", color: "var(--zone-c)" },
  { key: "r", name: "Right", short: "R", color: "var(--zone-r)" },
];
const ZONE_BY_LETTER = { L: ZONES[0], C: ZONES[1], R: ZONES[2] };
const ALARMS = new Set(["PERSON", "DIGGING"]);
const ALARM_HOLD_S = 30;   // the status card stays red this long after an alarm
const FEED_MAX = 6;        // recent detections shown
const SPARK_BARS = 12;     // bars in the alarms card

const ICONS = {
  PERSON: '<path d="M9 4.5c1.4 0 2.2 1.5 2 3.4-.2 1.8-1.2 3.1-2.3 3-1.1 0-1.8-1.5-1.6-3.3C7.3 5.7 8 4.5 9 4.5zM8.6 13.2c1 0 1.6.8 1.4 1.9l-.3 1.6c-.2 1-1 1.6-1.8 1.4-.8-.1-1.2-1-1-2l.3-1.5c.2-.9.7-1.4 1.4-1.4zM15.4 8c1 0 1.7 1.3 1.6 2.8-.2 1.6-1.1 2.7-2 2.6-1 0-1.6-1.3-1.4-2.9.2-1.5.9-2.5 1.8-2.5zM15 15.6c.8 0 1.3.7 1.2 1.6l-.2 1.3c-.2.9-.9 1.4-1.6 1.2-.7-.1-1-.9-.9-1.7l.3-1.3c.1-.7.6-1.1 1.2-1.1z"/>',
  DIGGING: '<path d="M14 3l7 7M17.5 6.5 9 15M9 15l-3.5 3.5a2.1 2.1 0 0 1-3-3L6 12l3 3zM12.5 4.5l7 7"/>',
  ANIMAL: '<circle cx="7" cy="9" r="1.8"/><circle cx="11" cy="6.5" r="1.8"/><circle cx="15.5" cy="7" r="1.8"/><circle cx="18.5" cy="10.5" r="1.8"/><path d="M8.5 16.5c0-2.5 2-4.5 4.5-4.5s4 2 4 4c0 2-1.5 2.8-3 2.5-1.2-.2-1.8-.2-3 .2-1.6.5-2.5-.5-2.5-2.2z"/>',
  NOISE: '<path d="M3 9h11a3 3 0 1 0-3-3M3 13h15a3 3 0 1 1-3 3M3 17h7"/>',
  QUIET: '<path d="M12 21c-4-3-7-6.5-7-10.5A7 7 0 0 1 12 3a7 7 0 0 1 7 7.5c0 4-3 7.5-7 10.5z"/><path d="M12 8v6M9.5 11.5 12 14l2.5-2.5"/>',
  OFFLINE: '<path d="M4 8.5a12 12 0 0 1 16 0M7 12a7.5 7.5 0 0 1 10 0M10 15.5a3 3 0 0 1 4 0"/><path d="M4 4l16 16"/>',
  SUN: '<circle cx="12" cy="12" r="4"/><path d="M12 2.5v2M12 19.5v2M2.5 12h2M19.5 12h2M5.3 5.3l1.4 1.4M17.3 17.3l1.4 1.4M5.3 18.7l1.4-1.4M17.3 6.7l1.4-1.4"/>',
  MOON: '<path d="M20 14.5A8 8 0 1 1 9.5 4a6.5 6.5 0 0 0 10.5 10.5z"/>',
};
const icon = (k) => `<svg viewBox="0 0 24 24" aria-hidden="true">${ICONS[k]}</svg>`;
const EVENT_TEXT = { PERSON: "Person", DIGGING: "Digging", ANIMAL: "Animal", NOISE: "Noise" };

// ------------------------------ state ------------------------------
const state = {
  preview: false,
  device: DEVICES[0],
  hours: 1,
  minutes: [],   // per-minute summaries, oldest first
  events: [],    // non-NONE windows, newest first
  latest: null,  // newest window of any kind
  channel: null,
  animate: true, // play chart / list entrances on the next render
};

// ------------------------------ auth ------------------------------
async function boot() {
  applyTheme(loadTheme());
  if (new URLSearchParams(location.search).has("preview")) return startPreview();
  const { data } = await sb.auth.getSession();
  if (data.session) startApp(data.session);
  else showLogin();
  sb.auth.onAuthStateChange((evt) => {
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
  sel.onchange = () => { state.device = sel.value; state.animate = true; setEyebrow(); reload(); };
  for (const b of $("range").querySelectorAll("button")) {
    b.onclick = () => {
      if (b.getAttribute("aria-checked") === "true") return;
      for (const x of $("range").querySelectorAll("button")) x.setAttribute("aria-checked", x === b);
      state.hours = +b.dataset.h;
      state.animate = true;
      moveThumb();
      reload();
    };
  }
  for (const b of document.querySelectorAll("[data-table]")) {
    b.onclick = () => {
      const k = b.dataset.table, t = $(`table-${k}`), chart = t.previousElementSibling;
      t.hidden = !t.hidden;
      chart.hidden = !t.hidden;
      b.textContent = t.hidden ? "Table" : "Chart";
      if (t.hidden) { state.animate = true; render(); }
    };
  }
  // nav highlight follows the section in view
  const links = [...document.querySelectorAll(".nav a")];
  const io = new IntersectionObserver((entries) => {
    for (const en of entries) if (en.isIntersecting) {
      for (const a of links) a.classList.toggle("active", a.getAttribute("href") === `#${en.target.id}`);
    }
  }, { rootMargin: "-40% 0px -55% 0px" });
  for (const a of links) { const s = document.querySelector(a.getAttribute("href")); if (s) io.observe(s); }

  // staggered entrance; class removed afterwards so hover transforms still work
  document.querySelectorAll(".heading, .stats .stat, .band, .duo .open").forEach((n, i) => {
    n.style.setProperty("--i", i);
    n.classList.add("rise");
    n.addEventListener("animationend", (e) => { if (e.animationName === "rise") n.classList.remove("rise"); }, { once: false });
  });

  $("map-legend").replaceChildren(...ZONES.map((z) => {
    const s = document.createElement("span"), i = document.createElement("i");
    i.style.background = z.color;
    s.append(i, document.createTextNode(z.name));
    return s;
  }));
  drawMapBase();
  setEyebrow();
  tickClock();
  setInterval(tickClock, 1000);
  requestAnimationFrame(moveThumb);
  addEventListener("resize", debounce(() => { moveThumb(); render(); }, 150));
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
  if ($("app").hidden) return;
  const now = Date.now(), t0 = now - state.hours * 3600e3;
  const mins = state.minutes.filter((m) => m.t >= t0);
  const evs = state.events.filter((e) => new Date(e.created_at).getTime() >= t0);
  const animate = state.animate;
  state.animate = false;

  renderStatus(now);
  renderStats(now, t0, mins, animate);
  renderMap(now, mins);
  renderFeed(evs, animate);

  // ground activity; longer ranges group minutes (strongest reading per group)
  const stepMin = state.hours === 1 ? 1 : state.hours === 6 ? 5 : 15;
  const grouped = groupPeaks(mins, stepMin * 60e3);
  const per = stepMin === 1 ? "minute" : `${stepMin} min`;
  $("activity-kicker").textContent = `Strongest vibration per ${per} · by zone`;
  if (!$("chart-activity").hidden) {
    lineChart($("chart-activity"), {
      t0, t1: now, label: `Strongest vibration per ${per} by zone (ADC counts above baseline)`,
      series: ZONES.map((z) => ({ name: z.name, short: z.short, color: z.color })),
      points: grouped.map((m) => ({ t: m.t, values: [m.peak_l, m.peak_c, m.peak_r] })),
      ymin: 20, endLabels: true, gapMs: stepMin * 60e3 * 2, animate,
    });
  }
  table($("table-activity"), [stepMin === 1 ? "Minute" : "From", "Left", "Centre", "Right"],
    [...grouped].reverse().map((m) => [fmtTime(m.t), m.peak_l, m.peak_c, m.peak_r]));
}

let lastAlarmKey = null;
function renderStatus(now) {
  const card = $("status-card"), lat = state.latest;
  const lastAlarm = state.events.find((e) => ALARMS.has(e.event));
  const alarmAge = lastAlarm ? (now - new Date(lastAlarm.created_at).getTime()) / 1000 : Infinity;
  const seenAge = lat ? (now - new Date(lat.created_at).getTime()) / 1000 : Infinity;
  const alarmOn = alarmAge < ALARM_HOLD_S;
  card.classList.toggle("alarm", alarmOn);
  card.classList.toggle("offline", !alarmOn && seenAge > ONLINE_SECONDS);
  let key;
  if (alarmOn) {
    key = lastAlarm.event;
    const z = ZONE_BY_LETTER[lastAlarm.zone];
    $("status-eyebrow").textContent = "Alarm";
    $("status-title").textContent = `${EVENT_TEXT[lastAlarm.event]} detected`;
    $("status-sub").textContent = `${z ? `${z.name} zone` : "Zone unknown"} · ${ago(alarmAge)}`;
    // shake once per new alarm
    const ak = lastAlarm.created_at;
    if (ak !== lastAlarmKey) {
      lastAlarmKey = ak;
      card.classList.remove("fresh"); void card.offsetWidth; card.classList.add("fresh");
      card.addEventListener("animationend", () => card.classList.remove("fresh"), { once: true });
    }
  } else if (seenAge > ONLINE_SECONDS) {
    key = "OFFLINE";
    $("status-eyebrow").textContent = "Status";
    $("status-title").textContent = lat ? "Board offline" : "No data yet";
    $("status-sub").textContent = lat ? `Last heard ${ago(seenAge)}` : "Start the bridge to see data";
  } else {
    key = "QUIET";
    $("status-eyebrow").textContent = "Status";
    $("status-title").textContent = "All quiet";
    $("status-sub").textContent = lastAlarm ? `Last alarm ${ago(alarmAge)}` : "Listening on all three zones";
  }
  const ic = $("status-icon");
  if (ic.dataset.k !== key) { ic.dataset.k = key; ic.innerHTML = icon(key); }
  if (!state.preview || seenAge <= ONLINE_SECONDS) setConn(seenAge <= ONLINE_SECONDS ? "on" : "off",
    seenAge <= ONLINE_SECONDS ? "Board online" : lat ? `Seen ${ago(seenAge)}` : "No data yet");
}

function renderStats(now, t0, mins, animate) {
  const tot = (k) => mins.reduce((a, m) => a + (m[k] || 0), 0);
  const person = tot("person"), digging = tot("digging"), animal = tot("animal");
  countTo($("t-alarms"), person + digging);
  countTo($("t-events"), person + digging + animal);
  $("t-alarms-foot").textContent = `last ${state.hours === 1 ? "hour" : `${state.hours} hours`}`;
  $("t-events-foot").textContent = `${person} person · ${digging} digging · ${animal} animal`;

  // alarm sparkline: SPARK_BARS equal buckets across the range
  const span = (now - t0) / SPARK_BARS, counts = new Array(SPARK_BARS).fill(0);
  for (const m of mins) {
    const i = Math.min(SPARK_BARS - 1, Math.floor((m.t - t0) / span));
    if (i >= 0) counts[i] += m.person + m.digging;
  }
  const max = Math.max(...counts, 1), bars = $("alarm-bars");
  if (animate || bars.children.length !== SPARK_BARS) {
    bars.replaceChildren(...counts.map((_, i) => { const b = document.createElement("i"); b.style.setProperty("--i", i); return b; }));
  }
  counts.forEach((c, i) => {
    const b = bars.children[i];
    b.style.height = `${Math.max(12, (c / max) * 100)}%`;
    b.classList.toggle("zero", c === 0);
  });

  const lat = state.latest, li = $("light-icon");
  if (lat) {
    const night = !!lat.night;
    $("t-light").textContent = night ? "Night" : "Day";
    $("t-light-foot").textContent = `LDR ${lat.ldr ?? "–"} · night below ${LDR_DARK}`;
    const k = night ? "MOON" : "SUN";
    if (li.dataset.k !== k) { li.dataset.k = k; li.innerHTML = icon(k); li.classList.toggle("night", night); }
  } else {
    $("t-light").textContent = "–";
    if (!li.dataset.k) { li.dataset.k = "SUN"; li.innerHTML = icon("SUN"); }
  }
}

// numbers roll from their old value to the new one
function countTo(node, target) {
  const from = node._v ?? 0;
  node._v = target;
  if (from === target) { if (!node._raf) node.textContent = target; return; }
  if (from !== target && node._started) { node.classList.remove("bump"); void node.offsetWidth; node.classList.add("bump"); }
  node._started = true;
  if (reduceMotion()) { node.textContent = target; return; }
  const t0 = performance.now(), dur = 800;
  cancelAnimationFrame(node._raf);
  const step = (t) => {
    const p = Math.min(1, (t - t0) / dur), e = 1 - (1 - p) ** 3;
    node.textContent = Math.round(from + (target - from) * e);
    node._raf = p < 1 ? requestAnimationFrame(step) : 0;
  };
  node._raf = requestAnimationFrame(step);
}

// ------------------------------ map ------------------------------
const ZX = [210, 500, 790], FY = 158;
function drawMapBase() {
  const svg = $("map");
  const trees = [[60, 70], [110, 48], [150, 84], [340, 40], [655, 60], [880, 44], [930, 82], [70, 250], [400, 268], [610, 262], [930, 250]];
  svg.innerHTML = `
    <g class="contours">
      <path class="contour" d="M-20 70 C120 40 220 95 360 62 S600 20 720 55 900 90 1020 50"/>
      <path class="contour c2" d="M-20 40 C130 14 240 64 380 34 S610 -4 730 26 910 60 1040 20"/>
      <path class="contour" d="M-20 250 C110 220 230 275 380 240 S640 200 760 236 920 270 1020 236"/>
      <path class="contour c3" d="M-20 280 C120 252 250 300 400 268 S650 232 780 262 930 296 1040 262"/>
    </g>
    <path class="stream" d="M655 300 C640 260 680 230 662 196 S690 120 672 80 698 20 690 -10"/>
    <path class="trail" d="M20 272 C150 258 260 284 420 266 S700 254 820 268 940 262 990 256"/>
    ${trees.map(([x, y], i) => `<circle class="tree" cx="${x}" cy="${y}" r="${13 + (x % 7)}" style="animation-delay:${-i * 0.7}s"/>`).join("")}
    <text class="place" x="40" y="128">OUTSIDE ↑</text>
    <text class="place" x="40" y="200">INSIDE ↓</text>
    <path class="fence" d="M30 ${FY} H970"/>
    <path class="wave" d="M30 ${FY} H970"/>
    ${ZONES.map((z, i) => `
      <g data-zone="${z.short}">
        <circle class="idle" cx="${ZX[i]}" cy="${FY}" r="22" style="stroke:${z.color};animation-delay:${i * 1.05}s"/>
        <circle class="ring-pulse r1" cx="${ZX[i]}" cy="${FY}" r="22" style="stroke:${z.color}"/>
        <circle class="ring-pulse r2" cx="${ZX[i]}" cy="${FY}" r="22" style="stroke:${z.color}"/>
        <circle class="ring-pulse r3" cx="${ZX[i]}" cy="${FY}" r="22" style="stroke:${z.color}"/>
        <g class="node" style="animation-delay:${300 + i * 150}ms">
          <circle class="zone-ring" cx="${ZX[i]}" cy="${FY}" r="24" style="stroke:${z.color}"/>
          <circle class="zone-dot" cx="${ZX[i]}" cy="${FY}" r="12" style="fill:${z.color}"/>
        </g>
        <text class="zone-label" x="${ZX[i]}" y="${FY - 46}">${z.name}</text>
        <text class="zone-val" x="${ZX[i]}" y="${FY + 64}" id="map-val-${z.short}">0</text>
        <text class="zone-cap" x="${ZX[i]}" y="${FY + 84}">PEAK</text>
      </g>`).join("")}`;
}

function renderMap(now, mins) {
  const last5 = mins.filter((m) => m.t >= now - 5 * 60e3);
  ZONES.forEach((z) => {
    const peak = last5.reduce((a, m) => Math.max(a, m[`peak_${z.key}`] || 0), 0);
    countTo($(`map-val-${z.short}`), peak);
  });
  const f = state.flash;
  for (const g of $("map").querySelectorAll("[data-zone]")) {
    const hot = !!(f && f.zone === g.dataset.zone && now - f.at < 12000);
    for (const r of g.querySelectorAll(".ring-pulse")) r.classList.toggle("hot", hot);
  }
}

// ------------------------------ feed ------------------------------
let feedKeys = new Set();
function renderFeed(evs, animate) {
  const list = $("feed");
  const shown = evs.slice(0, FEED_MAX);
  $("feed-empty").hidden = shown.length > 0;
  $("feed-count").textContent = evs.length ? `${evs.length} in range` : "In range";
  const keys = new Set();
  list.replaceChildren(...shown.map((e, idx) => {
    const key = `${e.created_at}${e.event}`;
    keys.add(key);
    const li = document.createElement("li");
    if (animate) { li.className = "enter"; li.style.setProperty("--i", idx); }
    else if (feedKeys.size && !feedKeys.has(key)) li.className = "new";
    const ic = document.createElement("span");
    ic.className = `ic ${ALARMS.has(e.event) ? "alarm" : e.event === "ANIMAL" ? "warn" : "calm"}`;
    ic.innerHTML = icon(e.event);
    const body = document.createElement("div");
    const what = document.createElement("div");
    what.className = "what";
    what.textContent = EVENT_TEXT[e.event];
    const meta = document.createElement("div");
    meta.className = "meta";
    meta.textContent = `${fmtTime(new Date(e.created_at).getTime(), true)} · ${ALARMS.has(e.event) ? "alarm" : e.event === "NOISE" ? "ignored" : "logged"}`;
    body.append(what, meta);
    const zone = document.createElement("span");
    zone.className = "zone";
    const zz = ZONE_BY_LETTER[e.zone];
    if (zz) { const d = document.createElement("i"); d.style.background = zz.color; zone.append(d); }
    zone.append(document.createTextNode(zz ? zz.name : "–"));
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
  for (let t = now - 24 * 3600e3; t <= now; t += 2000) addWindow(demoWindow(t));
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
function setEyebrow() {
  const d = new Date().toLocaleDateString([], { weekday: "long", day: "numeric", month: "long" });
  $("eyebrow-date").textContent = `${d} · ${state.device}`;
}
function tickClock() {
  $("clock").textContent = new Date().toLocaleTimeString([], { hour: "2-digit", minute: "2-digit", second: "2-digit" });
}
function moveThumb() {
  const seg = $("range"), b = seg.querySelector('[aria-checked="true"]'), th = seg.querySelector(".seg-thumb");
  if (!b || !b.offsetWidth) return;
  th.style.width = `${b.offsetWidth}px`;
  th.style.transform = `translateX(${b.offsetLeft}px)`;
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
function fmtTime(t, secs) {
  return new Date(t).toLocaleTimeString([], { hour: "2-digit", minute: "2-digit", ...(secs && { second: "2-digit" }) });
}
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
});

boot();
