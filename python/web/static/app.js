// Polls /api/state a few times a second and redraws the page.
// Plain JavaScript, no libraries.

const POLL_MS = 250;
let tick = 0.01;

const $ = (id) => document.getElementById(id);
const dollars = (ticks) => (ticks == null ? "–" : (ticks * tick).toFixed(2));
const money = (v) => (v < 0 ? "−$" : "$") + Math.abs(v).toFixed(2);

function renderLadder(state) {
  const asks = state.asks.slice().reverse(); // highest ask at the top
  const bids = state.bids;
  const maxQty = Math.max(1, ...asks.map((l) => l[1]), ...bids.map((l) => l[1]));
  const row = (cls, [price, qty]) =>
    `<tr class="${cls}"><td class="price"><span>${dollars(price)}</span></td>` +
    `<td><div class="bar" style="width:${(qty / maxQty) * 100}%"></div><span>${qty}</span></td></tr>`;

  const spread =
    state.best_bid != null && state.best_ask != null
      ? `spread ${dollars(state.best_ask - state.best_bid)}`
      : "one side empty";
  $("ladder").innerHTML =
    asks.map((l) => row("ask-row", l)).join("") +
    `<tr class="spread-row"><td colspan="2">${spread}</td></tr>` +
    bids.map((l) => row("bid-row", l)).join("");
}

function renderTrades(state) {
  $("trades").innerHTML = state.trades
    .slice(0, 20)
    .map((t) => {
      const tag = t.who ? `<span class="tag ${t.who === "you" ? "you" : ""}">${t.who}</span>` : "";
      return `<tr><td>${dollars(t.price)}</td><td>${t.quantity}</td><td>${tag}</td></tr>`;
    })
    .join("");
}

function renderStats(state) {
  $("fair").textContent = (state.fair_price * tick).toFixed(2);
  $("bbo").textContent = `${dollars(state.best_bid)} / ${dollars(state.best_ask)}`;
  $("position").textContent = state.market_maker.position;
  $("pnl").textContent = money(state.market_maker.pnl);
  $("orders").textContent = state.orders_in_book.toLocaleString();
  const p50 = state.latency.p50;
  $("p50").textContent = p50 == null ? "–" : `${(p50 / 1000).toFixed(2)} µs`;
}

// P&L line chart with a zero line and a hover crosshair + tooltip.
let pnlPoints = [];
function renderPnl(state) {
  const values = state.pnl_history;
  const svg = $("pnl-chart").querySelector("svg");
  if (values.length < 2) {
    svg.innerHTML = "";
    return;
  }
  const W = 300, H = 140, pad = 6;
  const lo = Math.min(0, ...values), hi = Math.max(0, ...values);
  const span = hi - lo || 1;
  const x = (i) => (i / (values.length - 1)) * W;
  const y = (v) => pad + (1 - (v - lo) / span) * (H - 2 * pad);
  $("pnl-note").textContent =
    `Marked to the fair price. Last ${((values.length - 1) * 10).toLocaleString()} steps, ` +
    `low ${money(Math.min(...values))}, high ${money(Math.max(...values))}, now ${money(values[values.length - 1])}.`;
  pnlPoints = values.map((v, i) => [x(i), y(v), v]);
  const path = pnlPoints.map(([px, py], i) => `${i ? "L" : "M"}${px.toFixed(1)},${py.toFixed(1)}`).join("");
  const area = `${path}L${W},${y(0)}L0,${y(0)}Z`;
  svg.innerHTML =
    `<path d="${area}" fill="var(--series-soft)"/>` +
    `<line x1="0" x2="${W}" y1="${y(0)}" y2="${y(0)}" stroke="var(--zero)" stroke-width="1" vector-effect="non-scaling-stroke"/>` +
    `<path d="${path}" fill="none" stroke="var(--series)" stroke-width="2" vector-effect="non-scaling-stroke" stroke-linejoin="round"/>` +
    `<line id="crosshair" x1="0" x2="0" y1="0" y2="${H}" stroke="var(--text-3)" stroke-width="1" vector-effect="non-scaling-stroke" visibility="hidden"/>`;
}

function setupPnlHover() {
  const box = $("pnl-chart");
  const tip = box.querySelector(".tooltip");
  box.addEventListener("mousemove", (event) => {
    if (pnlPoints.length < 2) return;
    const rect = box.getBoundingClientRect();
    const fx = (event.clientX - rect.left) / rect.width;
    const i = Math.round(fx * (pnlPoints.length - 1));
    const [, , value] = pnlPoints[Math.max(0, Math.min(i, pnlPoints.length - 1))];
    const cross = box.querySelector("#crosshair");
    if (cross) {
      cross.setAttribute("x1", fx * 300);
      cross.setAttribute("x2", fx * 300);
      cross.setAttribute("visibility", "visible");
    }
    const ago = (pnlPoints.length - 1 - i) * 10;
    tip.textContent = `${money(value)} · ${ago} steps ago`;
    tip.style.display = "block";
    tip.style.left = `${Math.min(event.clientX - rect.left + 10, rect.width - tip.offsetWidth)}px`;
    tip.style.top = "0px";
  });
  box.addEventListener("mouseleave", () => {
    tip.style.display = "none";
    const cross = box.querySelector("#crosshair");
    if (cross) cross.setAttribute("visibility", "hidden");
  });
}

function renderLatency(state) {
  const lat = state.latency;
  const max = Math.max(1, ...lat.histogram.map((b) => b.count));
  $("latency").innerHTML = lat.histogram
    .map((b) => {
      const pct = lat.count ? ((b.count / lat.count) * 100).toFixed(1) : "0.0";
      return `<div class="hist-row" title="${b.count.toLocaleString()} calls (${pct}%)"><span class="num">${b.label}</span>` +
        `<div class="hist-track"><div class="hist-bar" style="width:${(b.count / max) * 100}%"></div></div>` +
        `<span class="num">${pct}%</span></div>`;
    })
    .join("");
  $("latency-note").textContent = lat.count
    ? `Last ${lat.count.toLocaleString()} calls. p50 ${(lat.p50 / 1000).toFixed(2)} µs, p99 ${(lat.p99 / 1000).toFixed(2)} µs. ` +
      "Timed from Python, so this includes the Python-to-C++ call overhead."
    : "";
}

async function poll() {
  try {
    const response = await fetch("/api/state");
    const state = await response.json();
    tick = state.tick_dollars;
    renderStats(state);
    renderLadder(state);
    renderTrades(state);
    renderPnl(state);
    renderLatency(state);
  } catch (error) {
    $("order-result").textContent = "Lost connection to the server.";
  }
  setTimeout(poll, POLL_MS);
}

function setupOrderForm() {
  const form = $("order-form");
  let side = "buy";
  form.querySelectorAll("button[data-side]").forEach((b) =>
    b.addEventListener("click", () => (side = b.dataset.side))
  );
  form.type.addEventListener("change", () => (form.price.disabled = form.type.value === "market"));
  form.addEventListener("submit", async (event) => {
    event.preventDefault();
    const order = { side, type: form.type.value, quantity: form.quantity.value, price: form.price.value };
    const response = await fetch("/api/order", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(order),
    });
    const result = await response.json();
    $("order-result").textContent = result.ok
      ? `Order ${result.order_id}: filled ${result.filled} in ${result.trades} trade(s)` +
        (result.resting ? ", the rest is waiting in the book." : ".")
      : `Rejected: ${result.error}`;
  });
}

setupOrderForm();
setupPnlHover();
poll();
