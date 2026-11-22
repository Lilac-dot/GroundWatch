// Small SVG charts with a hover layer. Colours are CSS variables, so the
// light and dark themes switch without re-rendering. All text goes in with
// textContent (data never becomes HTML).

const NS = "http://www.w3.org/2000/svg";
const tip = () => document.getElementById("tooltip");

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

function yAxis(svg, m, w, h, ymax, ticks = 4, fmt = (v) => v) {
  for (let i = 0; i <= ticks; i++) {
    const v = (ymax * i) / ticks, y = h - m.b - ((h - m.t - m.b) * i) / ticks;
    el("line", { x1: m.l, x2: w - m.r, y1: y, y2: y, class: "grid-line" }, svg);
    const t = el("text", { x: m.l - 8, y: y + 3.5, "text-anchor": "end", class: "axis-text" }, svg);
    t.textContent = fmt(Math.round(v));
  }
}

function xTimeAxis(svg, m, w, h, t0, t1) {
  const n = w < 420 ? 3 : 5;
  for (let i = 0; i <= n; i++) {
    const t = new Date(t0 + ((t1 - t0) * i) / n);
    const x = m.l + ((w - m.l - m.r) * i) / n;
    const tx = el("text", { x, y: h - m.b + 16, "text-anchor": i === 0 ? "start" : i === n ? "end" : "middle", class: "axis-text" }, svg);
    tx.textContent = fmtTime(t);
  }
}

function emptyText(svg, w, h, msg) {
  const t = el("text", { x: w / 2, y: h / 2, class: "empty-text" }, svg);
  t.textContent = msg;
}

/**
 * Line chart over time.
 * opts: { t0, t1, points: [{t: ms, values: [..]}], series: [{name, color}],
 *         ymin, ref: {value, label}, endLabels, unit }
 */
export function lineChart(container, opts) {
  const { svg, w, h } = frame(container);
  const m = { l: 34, r: opts.endLabels ? 30 : 10, t: 10, b: 24 };
  const { t0, t1, points, series } = opts;
  svg.setAttribute("aria-label", opts.label || "Line chart");
  let ymax = 0;
  for (const p of points) for (const v of p.values) if (v != null && v > ymax) ymax = v;
  if (opts.ref) ymax = Math.max(ymax, opts.ref.value);
  ymax = opts.ymax || niceMax(Math.max(ymax, opts.ymin || 0));
  yAxis(svg, m, w, h, ymax);
  xTimeAxis(svg, m, w, h, t0, t1);
  const X = (t) => m.l + ((t - t0) / (t1 - t0)) * (w - m.l - m.r);
  const Y = (v) => h - m.b - (v / ymax) * (h - m.t - m.b);

  if (opts.ref) {
    el("line", { x1: m.l, x2: w - m.r, y1: Y(opts.ref.value), y2: Y(opts.ref.value), class: "ref" }, svg);
    const rt = el("text", { x: w - m.r, y: Y(opts.ref.value) - 5, "text-anchor": "end", class: "ref-text" }, svg);
    rt.textContent = opts.ref.label;
  }
  if (!points.length) { emptyText(svg, w, h, "No data in this range"); return; }

  // lines break where data is missing (gap > opts.gapMs, default 2 min)
  series.forEach((s, si) => {
    let d = "", prev = null;
    for (const p of points) {
      const v = p.values[si];
      if (v == null) { prev = null; continue; }
      const cmd = prev == null || p.t - prev > (opts.gapMs || 120000) ? "M" : "L";
      d += `${cmd}${X(p.t).toFixed(1)},${Y(v).toFixed(1)}`;
      prev = p.t;
    }
    const path = el("path", { d, class: "series" }, svg);
    path.style.stroke = s.color;
  });
  // direct labels at the line ends, nudged apart so they never overlap
  if (opts.endLabels) {
    const labs = series.map((s, si) => {
      const last = [...points].reverse().find((p) => p.values[si] != null);
      return last && { text: s.short || s.name, x: X(last.t) + 6, y: Y(last.values[si]) + 4 };
    }).filter(Boolean).sort((a, b) => a.y - b.y);
    for (let i = 1; i < labs.length; i++) labs[i].y = Math.max(labs[i].y, labs[i - 1].y + 12);
    const over = labs.length ? labs[labs.length - 1].y - (h - m.b + 2) : 0;
    if (over > 0) labs.forEach((l) => { l.y -= over; });
    for (const l of labs) el("text", { x: l.x, y: l.y, class: "end-label" }, svg).textContent = l.text;
  }

  // hover: crosshair snaps to the nearest minute, tooltip lists every series
  const cross = el("line", { y1: m.t, y2: h - m.b, class: "crosshair", visibility: "hidden" }, svg);
  const dots = series.map((s) => {
    const c = el("circle", { r: 4.5, class: "hover-dot", visibility: "hidden" }, svg);
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

/**
 * Bar chart. opts: { bars: [{label, value, color, tip}], horizontal, label, unit }
 * Vertical bars are for time buckets; horizontal for categories (zones).
 */
export function barChart(container, opts) {
  const { svg, w, h } = frame(container);
  svg.setAttribute("aria-label", opts.label || "Bar chart");
  const bars = opts.bars;
  const total = bars.reduce((a, b) => a + b.value, 0);

  if (opts.horizontal) {
    const m = { l: 64, r: 44, t: 8, b: 8 };
    const vmax = niceMax(Math.max(...bars.map((b) => b.value), 1));
    const band = (h - m.t - m.b) / bars.length, bh = Math.min(26, band - 12);
    bars.forEach((b, i) => {
      const y = m.t + band * i + (band - bh) / 2;
      const len = (b.value / vmax) * (w - m.l - m.r);
      const lab = el("text", { x: m.l - 10, y: y + bh / 2 + 4, "text-anchor": "end", class: "end-label" }, svg);
      lab.textContent = b.label;
      el("rect", { x: m.l, y, width: w - m.l - m.r, height: bh, rx: 4, fill: "var(--panel-2)" }, svg);
      const r = el("rect", { x: m.l, y, width: Math.max(len, b.value ? 4 : 0), height: bh, rx: 4, class: "bar", tabindex: 0 }, svg);
      r.style.fill = b.color;
      const v = el("text", { x: m.l + Math.max(len, 0) + 6, y: y + bh / 2 + 4, class: "end-label" }, svg);
      v.textContent = b.value;
      const show = (e) => showTip(e, b.label, [{ label: opts.unit || "count", value: `${b.value}`, color: b.color }]);
      r.addEventListener("pointermove", show);
      r.addEventListener("pointerleave", hideTip);
      r.addEventListener("focus", () => { const q = r.getBoundingClientRect(); show({ clientX: q.right, clientY: q.top }); });
      r.addEventListener("blur", hideTip);
    });
    if (!total) emptyText(svg, w, h - 4, "");
    return;
  }

  const m = { l: 30, r: 8, t: 10, b: 24 };
  const vmax = niceMax(Math.max(...bars.map((b) => b.value), 1), 3);
  yAxis(svg, m, w, h, vmax, 3);
  const band = (w - m.l - m.r) / bars.length, gap = 2, bw = Math.max(band - gap, 1);
  const every = Math.ceil(bars.length / (w < 360 ? 4 : 6));
  bars.forEach((b, i) => {
    const x = m.l + band * i + gap / 2;
    const bh = (b.value / vmax) * (h - m.t - m.b);
    // hit area is the whole column, not just the painted bar
    const hit = el("rect", { x: m.l + band * i, y: m.t, width: band, height: h - m.t - m.b, fill: "transparent", tabindex: 0 }, svg);
    if (bh > 0) {
      const r = el("rect", { x, y: h - m.b - bh, width: bw, height: bh, rx: Math.min(4, bw / 2), class: "bar" }, svg);
      r.style.fill = b.color;
      r.style.pointerEvents = "none";
    }
    if (i % every === 0) {
      const t = el("text", { x: x + bw / 2, y: h - m.b + 16, "text-anchor": "middle", class: "axis-text" }, svg);
      t.textContent = b.label;
    }
    const show = (e) => showTip(e, b.tip || b.label, [{ label: opts.unit || "count", value: `${b.value}`, color: b.color }]);
    hit.addEventListener("pointermove", show);
    hit.addEventListener("pointerleave", hideTip);
    hit.addEventListener("focus", () => { const q = hit.getBoundingClientRect(); show({ clientX: q.right, clientY: q.top }); });
    hit.addEventListener("blur", hideTip);
  });
  if (!total) emptyText(svg, w, h / 2, "None in this range");
}

/** Plain table for the "Table" toggle (the accessible view of every chart). */
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
