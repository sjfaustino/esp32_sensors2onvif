// One sensor's own page, opened as #sensor:AA-BB-CC-DD-EE-01/1 from its name anywhere: how it is now, when it fired
// over the last week, its battery over time, and its latest events, each explained.
(() => {
  const ALERT_EVENTS = [
    "alert",
    "delayed alert",
    "alert in quiet hours",
    "alert while disarmed",
    "alert while armed home",
    "test alert",
  ];
  let shown = "",
    timer = 0;

  // One line of the hub's CSV, which quotes a field holding a comma or a quote.
  function csvFields(line) {
    const out = [];
    let field = "",
      quoted = false;
    for (let i = 0; i < line.length; i++) {
      const ch = line[i];
      if (quoted) {
        if (ch == '"' && line[i + 1] == '"') {
          field += '"';
          i++;
        } else if (ch == '"') quoted = false;
        else field += ch;
      } else if (ch == '"') quoted = true;
      else if (ch == ",") {
        out.push(field);
        field = "";
      } else field += ch;
    }
    out.push(field);
    return out;
  }

  // Seven rows, today last, of 24 hours each, shaded by how many alerts there were, from this sensor's history over the
  // last 8 days, which covers 7 local days wherever the browser is. The hub sends one download at a time, so a busy one
  // is asked again a few times.
  // box: where to draw it; still: whether the page that asked is still open, so a late answer draws nothing.
  async function drawWeek(key, box = $("snWeek"), still = () => shown == key) {
    box.replaceChildren(el("p", { className: "note", textContent: "Reading the history..." }));
    let text,
      why = "";
    for (let i = 0; i < 5 && text == null && still(); i++) {
      try {
        const r = await fetch("/api/events.csv?sensor=" + key + "&days=8");
        if (r.ok) text = await r.text();
        else why = (await r.json().catch(() => ({}))).error || "";
        if (r.status != 503) break;
      } catch (e) {}
      if (text == null) await new Promise((done) => setTimeout(done, 3000));
    }
    if (!still()) return; // another page was opened meanwhile
    if (text == null) {
      box.replaceChildren(
        el("p", { className: "note", textContent: "Could not read the history" + (why ? ": " + why : ".") }),
      );
      return;
    }
    const today = new Date();
    today.setHours(0, 0, 0, 0);
    const first = today.getTime() - 6 * 86400000,
      counts = Array.from({ length: 7 }, () => new Array(24).fill(0));
    let total = 0;
    for (const line of text.split("\n").slice(1)) {
      if (!line) continue;
      const f = csvFields(line);
      if (f[2] != key || !ALERT_EVENTS.includes(f[4])) continue;
      const t = Date.parse(f[1] || f[0]);
      if (!(t >= first)) continue;
      const d = new Date(t),
        day = Math.floor((new Date(d).setHours(0, 0, 0, 0) - first) / 86400000);
      if (day < 0 || day > 6) continue;
      counts[day][d.getHours()]++;
      total++;
    }
    const most = Math.max(1, ...counts.flat());
    const table = el(
      "table",
      { className: "week" },
      el(
        "tr",
        null,
        el("th"),
        ...Array.from({ length: 24 }, (_, h) =>
          el("th", { textContent: h % 6 == 0 ? String(h).padStart(2, "0") : "" }),
        ),
      ),
      ...counts.map((hours, day) =>
        el(
          "tr",
          null,
          el("th", {
            textContent:
              day == 6 ? "Today" : new Date(first + day * 86400000).toLocaleDateString([], { weekday: "short" }),
          }),
          ...hours.map((n, h) =>
            el("td", {
              className: n ? "hit" : "",
              style: n ? "opacity:" + (0.35 + (0.65 * n) / most) : "",
              title: n + (n == 1 ? " alert, " : " alerts, ") + String(h).padStart(2, "0") + ":00",
            }),
          ),
        ),
      ),
    );
    box.replaceChildren(
      el("p", {
        className: "note",
        textContent: total
          ? total + (total == 1 ? " alert" : " alerts") + " in the last 7 days, by hour of the day (local time)."
          : "No alerts in the last 7 days.",
      }),
      table,
    );
  }

  // Its readings from battery.txt, one every 6 hours, as a line over time.
  async function drawBattery(node, box = $("snBattery")) {
    let points = [];
    try {
      const r = await fetch("/api/file?path=" + node + "/battery.txt");
      if (r.ok)
        points = (await r.text())
          .split("\n")
          .map((l) => l.split(" ").map(Number))
          .filter((p) => p.length == 2 && p[0] > 0 && p[1] >= 0);
    } catch (e) {}
    if (points.length < 2) {
      box.replaceChildren(
        el("p", { className: "note", textContent: "Not enough readings yet: the hub keeps one every 6 hours." }),
      );
      return;
    }
    const W = 320,
      H = 90,
      t0 = points[0][0],
      t1 = points[points.length - 1][0],
      x = (t) => 30 + ((t - t0) / Math.max(1, t1 - t0)) * (W - 40),
      y = (p) => 10 + ((100 - p) / 100) * (H - 25);
    const ns = "http://www.w3.org/2000/svg",
      svg = document.createElementNS(ns, "svg");
    svg.setAttribute("viewBox", "0 0 " + W + " " + H);
    svg.setAttribute("class", "chart");
    const add = (tag, attrs, text) => {
      const e = document.createElementNS(ns, tag);
      for (const k in attrs) e.setAttribute(k, attrs[k]);
      if (text) e.textContent = text;
      svg.append(e);
    };
    for (const p of [0, 50, 100]) {
      add("line", { x1: 30, x2: W - 10, y1: y(p), y2: y(p), class: "grid" });
      add("text", { x: 2, y: y(p) + 4 }, p + "%");
    }
    add("polyline", { points: points.map((p) => x(p[0]) + "," + y(p[1])).join(" "), class: "line" });
    const day = (t) => new Date(t * 1000).toLocaleDateString([], { day: "numeric", month: "short" });
    add("text", { x: 30, y: H - 2 }, day(t0));
    add("text", { x: W - 10, y: H - 2, "text-anchor": "end" }, day(t1));
    box.replaceChildren(svg);
  }

  async function refresh(key, full) {
    try {
      const [esp, events] = await Promise.all([call("GET", "/api/espnow"), call("GET", "/api/events")]);
      const n = esp.nodes.find((r) => r.node + "/" + r.input == key);
      if (!n) {
        $("snTitle").textContent = "No such sensor";
        $("snFacts").replaceChildren();
        return;
      }
      $("snTitle").textContent = n.name || key;
      const STATES = {
        active: "Firing now",
        idle: "Quiet",
        silent: "Silent: not heard for too long",
        never: "Not heard yet",
      };
      hub.fill("snFacts", [
        ["Sensor", key + (n.sensor ? ", " + n.sensor : "")],
        ["State", STATES[n.state] || n.state],
        ["Last heard", hub.heardAgo(n.ago)],
        [
          "Battery",
          (n.battery == null ? "Not known" : n.battery + "%" + (n.low ? " (low)" : "")) +
            (n.daysLeft == null ? "" : ", " + hub.batteryLeft(n.daysLeft)),
        ],
        ["Signal", hub.signal(n.rssi) + (n.weak ? ", weak on average" : "")],
        ["Last alert took", n.responseMs == null ? "-" : (n.responseMs / 1000).toFixed(1) + " s to reach the hub"],
        [
          "Firmware",
          (n.version || "-") + (n.restarts == null ? "" : ", " + n.restarts + " restarts, the last: " + n.restart),
        ],
      ]);
      const mine = events.filter((e) => e.sensor == key).slice(0, 10);
      $("snEvents").replaceChildren(
        ...(mine.length
          ? mine.map((e) =>
              el(
                "li",
                null,
                el("b", { textContent: e.event.charAt(0).toUpperCase() + e.event.slice(1) }),
                " " + (e.received ? hub.ago(e.received) : "") + ". ",
                hub.explain(e),
              ),
            )
          : [el("li", { textContent: "None among the latest 64 events." })]),
      );
      if (full && shown == key) {
        drawWeek(key);
        drawBattery(n.node);
      }
    } catch (e) {}
  }

  $("snTest").onclick = async () => {
    try {
      await call("POST", "/api/test-alert?path=" + shown, "x=1");
      $("snMsg").className = "portMsg";
      $("snMsg").textContent = "Test alert sent. The NVR should now record it: check that the recording is there.";
    } catch (e) {
      $("snMsg").className = "portMsg err";
      $("snMsg").textContent = e.message;
    }
  };

  window.sensorCharts = { drawWeek, drawBattery }; // a node's page draws them too; window.hub comes later, from index.js
  (window.viewHooks = window.viewHooks || []).push((view, arg) => {
    clearInterval(timer);
    if (view != "sensor") return;
    shown = arg;
    $("snMsg").textContent = "";
    refresh(arg, true);
    timer = setInterval(() => document.hidden || refresh(arg, false), 3000);
  });
})();
