// Small SVG charts with a hover layer. Colours are CSS variables, so the
// light and dark themes switch without re-rendering. All text goes in with
// textContent (data never becomes HTML).

const NS = "http://www.w3.org/2000/svg";
const tip = () => document.getElementById("tooltip");
const reduceMotion = () => matchMedia("(prefers-reduced-motion: reduce)").matches;
let uid = 0;

function el(name, attrs = {}, parent) {
  const n = document.createElementNS(NS, name);
  for (const [k, v] of Object.entries(attrs)) n.setAttribute(k, v);
  if (parent) parent.appendChild(n);
  return n;
}

// axis max that divides evenly into `ticks` steps of 1, 2, 2.5 or 5 x 10^n
function niceMax(v, ticks = 4, integer = true) {
  const raw = Math.max(v, integer ? ticks : 1e-9) / ticks;
  const p = 10 ** Math.floor(Math.log10(raw));
  for (const m of [1, 2, 2.5, 5, 10]) {
    const step = m * p;
    if (integer && !Number.isInteger(step)) continue;
    if (step >= raw) return step * ticks;
  }
  return 10 * p * ticks;
}

const fmtTime = (d) => d.toLocaleTimeString([], { hour: "2-digit", minute: "2-digit" });

// monotone cubic (Fritsch-Carlson): smooth, never overshoots below zero
function smooth(pts) {
  const n = pts.length;
  if (n < 3) return pts.map(([x, y], i) => `${i ? "L" : "M"}${x.toFixed(1)},${y.toFixed(1)}`).join("");
  const dx = [], s = [], m = new Array(n);
  for (let i = 0; i < n - 1; i++) { dx[i] = pts[i + 1][0] - pts[i][0]; s[i] = (pts[i + 1][1] - pts[i][1]) / (dx[i] || 1); }
  m[0] = s[0]; m[n - 1] = s[n - 2];
  for (let i = 1; i < n - 1; i++) m[i] = s[i - 1] * s[i] <= 0 ? 0 : (s[i - 1] + s[i]) / 2;
  for (let i = 0; i < n - 1; i++) {
    if (s[i] === 0) { m[i] = m[i + 1] = 0; continue; }
    const a = m[i] / s[i], b = m[i + 1] / s[i], h = a * a + b * b;
    if (h > 9) { const t = 3 / Math.sqrt(h); m[i] = t * a * s[i]; m[i + 1] = t * b * s[i]; }
  }
  let d = `M${pts[0][0].toFixed(1)},${pts[0][1].toFixed(1)}`;
  for (let i = 0; i < n - 1; i++) {
    const [x0, y0] = pts[i], [x1, y1] = pts[i + 1], h = dx[i] / 3;
    d += `C${(x0 + h).toFixed(1)},${(y0 + m[i] * h).toFixed(1)} ${(x1 - h).toFixed(1)},${(y1 - m[i + 1] * h).toFixed(1)} ${x1.toFixed(1)},${y1.toFixed(1)}`;
  }
  return d;
}

// ---------- tooltip ----------
export function showTip(evt, title, rows) {
  const t = tip();
  t.replaceChildren();
  const h = document.createElement("div");
  h.className = "tt-title";
  h.textContent = title;
  t.appendChild(h);
  for (const r of rows) {
    const row = document.createElement("div");
    row.className = "tt-row";
    const s = document.createElement("span");
    if (r.color) {
      const i = document.createElement("i");
      i.style.background = r.color;
      s.appendChild(i);
    }
    s.appendChild(document.createTextNode(r.label));
    const b = document.createElement("b");
    b.textContent = r.value;
    row.append(s, b);
    t.appendChild(row);
  }
  t.hidden = false;
  const pad = 14, w = t.offsetWidth, hgt = t.offsetHeight;
  let x = evt.clientX + pad, y = evt.clientY + pad;
  if (x + w > innerWidth - 8) x = evt.clientX - w - pad;
  if (y + hgt > innerHeight - 8) y = evt.clientY - hgt - pad;
  t.style.left = `${x}px`;
  t.style.top = `${y}px`;
}
export function hideTip() { tip().hidden = true; }

function frame(container) {
  const w = Math.max(container.clientWidth, 240), h = container.clientHeight || 200;
  container.replaceChildren();
  const svg = el("svg", { viewBox: `0 0 ${w} ${h}`, role: "img" }, container);
  return { svg, w, h };
}

function yAxis(svg, m, w, h, ymax, ticks = 4) {
  for (let i = 0; i <= ticks; i++) {
    const v = (ymax * i) / ticks, y = h - m.b - ((h - m.t - m.b) * i) / ticks;
    el("line", { x1: m.l, x2: w - m.r, y1: y, y2: y, class: "grid-line" }, svg);
    const t = el("text", { x: m.l - 8, y: y + 3.5, "text-anchor": "end", class: "axis-text" }, svg);
    t.textContent = Math.round(v);
  }
}

function xTimeAxis(svg, m, w, h, t0, t1) {
  const n = w < 420 ? 3 : 5;
  for (let i = 0; i <= n; i++) {
    const t = new Date(t0 + ((t1 - t0) * i) / n);
    const x = m.l + ((w - m.l - m.r) * i) / n;
    const tx = el("text", { x, y: h - m.b + 18, "text-anchor": i === 0 ? "start" : i === n ? "end" : "middle", class: "axis-text" }, svg);
    tx.textContent = fmtTime(t);
  }
}

/**
 * Line chart over time.
 * opts: { t0, t1, points: [{t: ms, values: [..]}], series: [{name, short, color}],
 *         ymin, gapMs, endLabels, unit, label, animate }
 * animate: draw the lines in from the left (used on load / range change,
 * not on every live update).
 */
export function lineChart(container, opts) {
  const { svg, w, h } = frame(container);
  const m = { l: 34, r: opts.endLabels ? 30 : 10, t: 10, b: 26 };
  const { t0, t1, points, series } = opts;
  const animate = opts.animate && !reduceMotion();
  svg.setAttribute("aria-label", opts.label || "Line chart");
  let ymax = 0;
  for (const p of points) for (const v of p.values) if (v != null && v > ymax) ymax = v;
  ymax = opts.ymax || niceMax(Math.max(ymax, opts.ymin || 0));
  yAxis(svg, m, w, h, ymax);
  xTimeAxis(svg, m, w, h, t0, t1);
  const X = (t) => m.l + ((t - t0) / (t1 - t0)) * (w - m.l - m.r);
  const Y = (v) => h - m.b - (v / ymax) * (h - m.t - m.b);
  if (!points.length) { emptyText(svg, w, h, "No data in this range"); return; }

  // everything drawn inside a clip that sweeps open left to right
  const id = `gw${++uid}`;
  const defs = el("defs", {}, svg);
  const clip = el("clipPath", { id: `${id}-clip` }, defs);
  const clipRect = el("rect", { x: 0, y: 0, width: animate ? 0 : w, height: h }, clip);
  if (animate) {
    el("animate", { attributeName: "width", from: 0, to: w, dur: "1.4s", fill: "freeze",
      calcMode: "spline", keyTimes: "0;1", keySplines: "0.22 1 0.36 1" }, clipRect);
  }
  const plot = el("g", { "clip-path": `url(#${id}-clip)` }, svg);

  // lines break where data is missing (gap > opts.gapMs, default 2 min)
  series.forEach((s, si) => {
    const runs = [];
    let run = [], prev = null;
    for (const p of points) {
      const v = p.values[si];
      if (v == null || (prev != null && p.t - prev > (opts.gapMs || 120000))) { if (run.length) runs.push(run); run = []; }
      if (v != null) { run.push([X(p.t), Y(v)]); prev = p.t; } else prev = null;
    }
    if (run.length) runs.push(run);

    const grad = el("linearGradient", { id: `${id}-g${si}`, x1: 0, y1: 0, x2: 0, y2: 1 }, defs);
    el("stop", { offset: "0%", style: `stop-color:${s.color};stop-opacity:1` }, grad);
    el("stop", { offset: "100%", style: `stop-color:${s.color};stop-opacity:0` }, grad);

    let line = "", area = "";
    for (const r of runs) {
      const d = smooth(r);
      line += d;
      area += `${d}L${r[r.length - 1][0].toFixed(1)},${h - m.b}L${r[0][0].toFixed(1)},${h - m.b}Z`;
    }
    el("path", { d: area, class: "area", fill: `url(#${id}-g${si})` }, plot);
    const path = el("path", { d: line, class: "series" }, plot);
    path.style.stroke = s.color;
  });

  // direct labels at the line ends, nudged apart so they never overlap
  if (opts.endLabels) {
    const labs = series.map((s, si) => {
      const last = [...points].reverse().find((p) => p.values[si] != null);
      return last && { text: s.short || s.name, x: X(last.t) + 7, y: Y(last.values[si]) + 4 };
    }).filter(Boolean).sort((a, b) => a.y - b.y);
    for (let i = 1; i < labs.length; i++) labs[i].y = Math.max(labs[i].y, labs[i - 1].y + 13);
    const over = labs.length ? labs[labs.length - 1].y - (h - m.b + 2) : 0;
    if (over > 0) labs.forEach((l) => { l.y -= over; });
    for (const l of labs) el("text", { x: l.x, y: l.y, class: `end-label${animate ? " draw" : ""}` }, svg).textContent = l.text;
  }

  // hover: crosshair snaps to the nearest point, tooltip lists every series
  const cross = el("line", { y1: m.t, y2: h - m.b, class: "crosshair", visibility: "hidden" }, svg);
  const dots = series.map((s) => {
    const c = el("circle", { r: 5, class: "hover-dot", visibility: "hidden" }, svg);
    c.style.fill = s.color;
    return c;
  });
  const hit = el("rect", { x: m.l, y: m.t, width: w - m.l - m.r, height: h - m.t - m.b, fill: "transparent", tabindex: 0 }, svg);
  const pick = (clientX) => {
    const r = svg.getBoundingClientRect();
    const x = ((clientX - r.left) / r.width) * w;
    const t = t0 + ((x - m.l) / (w - m.l - m.r)) * (t1 - t0);
    let best = points[0];
    for (const p of points) if (Math.abs(p.t - t) < Math.abs(best.t - t)) best = p;
    return best;
  };
  const show = (p, evt) => {
    const x = X(p.t);
    cross.setAttribute("x1", x); cross.setAttribute("x2", x); cross.setAttribute("visibility", "visible");
    series.forEach((s, i) => {
      const v = p.values[i];
      if (v == null) { dots[i].setAttribute("visibility", "hidden"); return; }
      dots[i].setAttribute("cx", x); dots[i].setAttribute("cy", Y(v)); dots[i].setAttribute("visibility", "visible");
    });
    showTip(evt, fmtTime(new Date(p.t)), series.map((s, i) => ({
      label: s.name, color: s.color, value: p.values[i] == null ? "–" : `${p.values[i]}${opts.unit || ""}`,
    })));
  };
  const hide = () => { cross.setAttribute("visibility", "hidden"); dots.forEach((d) => d.setAttribute("visibility", "hidden")); hideTip(); };
  hit.addEventListener("pointermove", (e) => show(pick(e.clientX), e));
  hit.addEventListener("pointerleave", hide);
  hit.addEventListener("focus", () => {
    const p = points[points.length - 1], r = hit.getBoundingClientRect();
    show(p, { clientX: r.right - 20, clientY: r.top + 20 });
  });
  hit.addEventListener("blur", hide);
}

function emptyText(svg, w, h, msg) {
  const t = el("text", { x: w / 2, y: h / 2, class: "empty-text" }, svg);
  t.textContent = msg;
}

/** Plain table for the "Table" toggle (the accessible view of the chart). */
export function table(container, headers, rows) {
  const t = document.createElement("table");
  const tr = document.createElement("tr");
  for (const hd of headers) { const th = document.createElement("th"); th.textContent = hd; tr.appendChild(th); }
  const thead = document.createElement("thead"); thead.appendChild(tr); t.appendChild(thead);
  const tb = document.createElement("tbody");
  for (const r of rows) {
    const row = document.createElement("tr");
    for (const c of r) { const td = document.createElement("td"); td.textContent = c; row.appendChild(td); }
    tb.appendChild(row);
  }
  t.appendChild(tb);
  container.replaceChildren(t);
}
