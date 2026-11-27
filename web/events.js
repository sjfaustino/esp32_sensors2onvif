// The Events page, with what each event means, and the walk test.
(() => {
  const { ago, headRow, heardAgo, sensorLink, signal, repeat, chip, empty } = window.hub;
  // What each event means and what to do about it, for the Events page and each sensor's own page.
  const EXPLAIN = {
    0: [
      tr("The sensor saw something, and the NVR was told, so it records."),
      tr("Check the recording if nobody was expected."),
    ],
    1: [
      tr(
        "No report from this sensor for longer than its silent time. Its battery may be flat, or it was unplugged, moved or taken.",
      ),
      tr("Check the sensor and its battery. This clears by itself once it reports again."),
    ],
    2: [tr("Its battery is at 20% or less."), tr("Replace or charge it soon.")],
    3: [
      tr("The sensor fired during its quiet hours, so the NVR was not told."),
      tr("If this should have been recorded, change the quiet hours in the sensor's Alert settings."),
    ],
    4: [
      tr("Its radio signal has stayed below -80 dBm, so its messages may be lost."),
      tr("Move the node or the hub closer, or raise the node off the ground and away from metal."),
    ],
    5: [
      tr(
        "The NVR was not listening when this alert came, so it did not record it then. The hub keeps it and plays it as a past event once the NVR listens again.",
      ),
      tr("Check that the NVR is on and on the network."),
    ],
    6: [
      tr("The NVR has not been listening for 3 minutes, so alerts are not being recorded."),
      tr("Check that the NVR is on, on the network, and still has the hub's channel set up."),
    ],
    7: [tr("The NVR is listening again. Alerts kept for it play now."), ""],
    8: [tr("The hub started, for the reason shown."), tr("If power cuts are common, a small UPS keeps it running.")],
    9: [
      tr(
        "The hub was off its Wi-Fi for the time shown. Alerts in that time reached the NVR afterwards, as past events.",
      ),
      tr("If this happens often, check the Wi-Fi where the hub is. Someone may be keeping it off on purpose."),
    ],
    10: [
      tr("The sensor fired while the hub was disarmed, so the NVR was not told."),
      tr("Arm the hub whenever nobody is home."),
    ],
    11: [tr("The hub was armed, from where the details say."), ""],
    12: [tr("The hub was disarmed, from where the details say. Alerts are logged but not recorded."), ""],
    13: [
      tr("An interior sensor fired while the hub was armed home, so the NVR was not told."),
      tr("If it should always record, mark it perimeter in its Alert settings."),
    ],
    14: [tr("The hub was armed home: interior sensors are logged but not recorded."), ""],
    15: [
      tr("A test alert, sent from the page or Home Assistant. It records like a real one."),
      tr("Check that the NVR has the recording."),
    ],
    16: [
      tr("The hub restarted by itself three times within an hour: it is crashing, losing power or being restarted."),
      tr("Check its power supply. Device Info shows the reasons."),
    ],
    17: [
      tr("The node restarted because of a fault, such as a brownout from a weak battery, or a bad wire."),
      tr("Check its battery and its wiring."),
    ],
    18: [tr("From its readings, the battery will run out within a week."), tr("Replace or charge it.")],
    19: [
      tr(
        "Several nodes stopped reporting at the same time. Batteries fail one at a time, so something may be blocking the radio, such as a jammer.",
      ),
      tr("Check the property now: this can mean a break-in."),
    ],
    20: [
      tr(
        "The sensor fired 20 times within an hour. Every alert still went to the NVR, but a fault or a bad aim is likelier than that many intruders.",
      ),
      tr("Check what it sees, such as moving plants, sun, pets or heating, and its wiring."),
    ],
    21: [
      tr("A time server gave a time far from the hub's clock, by the step shown, so the hub kept its own time."),
      tr("If this repeats, check the time servers on the Time page: a wrong clock would misdate every alert."),
    ],
  };
  function explain(e) {
    if (e.kind == 0 && !e.active)
      return tr("The sensor reports nothing in front of it any more. Its alert ends when the alert time runs out.");
    const [what, todo] = EXPLAIN[e.kind] || ["", ""];
    return what + (todo ? " " + todo : "");
  }
  // The walk test: each sensor that fires while someone walks the property, with its signal and how long the hub took.
  const WALK_KINDS = [0, 3, 10, 13, 15]; // alerts, whether or not the arm mode sent them on
  let walkTimer = 0,
    walkSeen = null;
  const walkFired = new Map();
  const eventKey = (e) => e.received + " " + e.sensor + " " + e.event;
  async function walkTick() {
    try {
      const [events, esp] = await Promise.all([call("GET", "/api/events"), call("GET", "/api/espnow")]);
      for (const e of events) {
        if (!walkSeen || walkSeen.has(eventKey(e))) continue;
        walkSeen.add(eventKey(e));
        if (!WALK_KINDS.includes(e.kind) || !e.active || e.sensor == "hub") continue;
        const f = walkFired.get(e.sensor) || { count: 0 };
        f.count++;
        f.at = Date.now();
        walkFired.set(e.sensor, f);
      }
      $("walkList").replaceChildren(
        ...(esp.nodes.length ? [headRow([tr("Sensor"), "", tr("Signal"), tr("Hub heard it in")])] : []),
        ...esp.nodes.map((n) => {
          const f = walkFired.get(n.node + "/" + n.input);
          return el(
            "tr",
            null,
            ...[
              n.name || n.node + "/" + n.input,
              !walkSeen
                ? ""
                : f
                  ? "✓ " +
                    (f.count > 1 ? tr("fired {n} times", { n: f.count }) : tr("fired")) +
                    ", " +
                    heardAgo(Math.round((Date.now() - f.at) / 1000))
                  : tr("waiting"),
              signal(n.rssi),
              n.responseMs == null ? "-" : (n.responseMs / 1000).toFixed(1) + " s",
            ].map((v) => el("td", { textContent: v })),
          );
        }),
      );
      if (typeof plan != "undefined")
        plan.draw($("walkPlan"), esp.nodes, (n) => {
          const f = walkFired.get(n.node + "/" + n.input);
          return !walkSeen ? "ok" : f && Date.now() - f.at < 5000 ? "alert" : f ? "fired" : "waiting";
        });
      if (!esp.nodes.length)
        $("walkMsg").textContent = tr("No sensor nodes yet. Pair one under Sensors > Nodes and pairing.");
    } catch (e) {}
  }
  async function walkAll(stop) {
    const esp = await call("GET", "/api/espnow");
    const nodes = [...new Set(esp.nodes.map((n) => n.node))];
    for (const node of nodes) await call("POST", "/api/walk-test?path=" + node, stop ? "stop=1" : "go=1");
    return nodes.length;
  }
  $("walkStart").onclick = async () => {
    try {
      walkSeen = new Set((await call("GET", "/api/events")).map(eventKey));
      walkFired.clear();
      const n = await walkAll(false);
      $("walkMsg").className = "portMsg";
      $("walkMsg").textContent = trCount(
        n,
        "On for {n} node for 10 minutes. Walk past each sensor; each node starts on its next report.",
        "On for {n} nodes for 10 minutes. Walk past each sensor; each node starts on its next report.",
      );
      walkTick();
    } catch (e) {
      $("walkMsg").className = "portMsg err";
      $("walkMsg").textContent = e.message;
    }
  };
  $("walkStop").onclick = async () => {
    try {
      await walkAll(true);
      $("walkMsg").className = "portMsg";
      $("walkMsg").textContent = tr("Stopped. What fired stays listed until you leave this page.");
    } catch (e) {
      $("walkMsg").className = "portMsg err";
      $("walkMsg").textContent = e.message;
    }
  };
  function setWalk(on) {
    clearInterval(walkTimer);
    if (!on) walkSeen = null;
    if (on) {
      walkTick();
      walkTimer = setInterval(() => document.hidden || walkTick(), 1500);
    }
  }
  const DIRECTIONS = ["", tr("approaching"), tr("moving away"), tr("left to right"), tr("right to left")];
  // The same wording as the hub's own messages: "7 min", "2 h 13 min", "3 days 4 h".
  function duration(s) {
    const m = Math.ceil(s / 60);
    return m < 60
      ? tr("{m} min", { m })
      : m < 2880
        ? tr("{h} h {m} min", { h: Math.floor(m / 60), m: m % 60 })
        : tr("{d} days {h} h", { d: Math.floor(m / 1440), h: Math.floor(m / 60) % 24 });
  }
  // Filtered here, from the list the hub sends, so the hub builds nothing more for it.
  const ALERT_KINDS = [0, 3, 5, 10, 13, 15],
    PROBLEM_KINDS = [1, 2, 4, 5, 6, 9, 16, 17, 18, 19, 20, 21];
  // Each event's label in the colour of what it is: red an alert, amber a problem, green the NVR back, the arm modes in
  // their own colours, grey the rest.
  function kindChip(e, what) {
    const k = e.kind,
      kind =
        k == 0
          ? e.active
            ? "bad"
            : "none"
          : k == 15
            ? "bad"
            : k == 7
              ? "ok"
              : k == 11
                ? "away"
                : k == 12
                  ? "disarmed"
                  : k == 14
                    ? "home"
                    : PROBLEM_KINDS.includes(k)
                      ? "warn"
                      : "none";
    return chip(what, kind);
  }
  const PAGE = 25; // events shown at first, and added by Show more
  let events = [],
    showing = PAGE;
  const opened = new Set(); // the events whose explanation is open, by eventKey
  async function loadEvents() {
    try {
      events = await call("GET", "/api/events");
      const seen = [...new Set(events.map((e) => e.sensor))].sort(),
        chosen = $("evSensor").value;
      $("evSensor").replaceChildren(
        el("option", { value: "", textContent: tr("every sensor and the hub") }),
        ...seen.map((s) => {
          const named = events.find((e) => e.sensor == s && e.name);
          return el("option", {
            value: s,
            textContent: s == "hub" ? tr("the hub") : named ? named.name + " (" + s + ")" : s,
          });
        }),
      );
      $("evSensor").value = seen.includes(chosen) ? chosen : "";
      showEvents();
    } catch (e) {}
  }
  function showEvents() {
    const sensor = $("evSensor").value,
      kind = $("evKind").value;
    const list = events.filter(
      (e) =>
        (!sensor || e.sensor == sensor) &&
        (!kind ||
          (kind == "alerts" && ALERT_KINDS.includes(e.kind) && e.sensor != "hub") ||
          (kind == "problems" && PROBLEM_KINDS.includes(e.kind)) ||
          (kind == "hub" && e.sensor == "hub")),
    );
    {
      $("evTable").replaceChildren(
        ...(list.length ? [headRow([tr("Time"), tr("Sensor"), tr("Event"), tr("Details"), tr("Battery")])] : []),
        ...list.slice(0, showing).flatMap((e) => {
          const when = e.happened || e.received,
            named = hubText(e.event), // the hub names every event
            what = named.charAt(0).toUpperCase() + named.slice(1);
          const off = e.off_s != null ? tr("off for up to {time}", { time: duration(e.off_s) }) : "";
          const details = [
            hubText(e.detail || ""),
            off,
            e.camera && e.active ? tr("look at {camera}", { camera: e.camera }) : "",
          ]
            .concat(
              e.targets.map((t) =>
                [t.distance_cm == null ? "" : (t.distance_cm / 100).toFixed(2) + " m", DIRECTIONS[t.direction] || ""]
                  .filter(Boolean)
                  .join(" "),
              ),
            )
            .filter(Boolean)
            .join(", ");
          const row = el(
            "tr",
            { className: "explains", title: tr("Tap for what this means") },
            el("td", {
              textContent: when ? ago(when) : tr("Clock not set"),
              title: when ? new Date(when).toLocaleString() : "",
            }),
            el("td", null, e.sensor == "hub" ? tr("Hub") : sensorLink(e.sensor, e.name || e.sensor)),
            el("td", null, kindChip(e, what)),
            ...[details, e.battery == null ? "" : e.battery + "%"].map((v) => el("td", { textContent: v })),
          );
          // Its explanation stays open through the refresh every few seconds, until tapped again.
          const key = eventKey(e),
            explanation = () =>
              el("tr", { className: "explanation" }, el("td", { colSpan: 5, textContent: explain(e) }));
          row.onclick = () => {
            const next = row.nextElementSibling;
            if (next && next.classList.contains("explanation")) {
              next.remove();
              opened.delete(key);
            } else {
              row.after(explanation());
              opened.add(key);
            }
          };
          return opened.has(key) ? [row, explanation()] : [row];
        }),
      );
      const more = list.length - showing;
      $("evMore").hidden = more <= 0;
      $("evMore").textContent = tr("Show {n} more", { n: Math.min(more, PAGE) });
      if (list.length) $("evNone").replaceChildren();
      else if (events.length)
        empty(
          $("evNone"),
          tr("None of these"),
          tr("Nothing of this kind among the latest 64 events. The CSV has the whole history."),
        );
      else
        empty(
          $("evNone"),
          tr("No events yet"),
          tr("Alerts, problems and the hub's own events show here as they happen."),
          el("a", { className: "btn", href: "#overview", textContent: tr("Send a test alert from the Overview") }),
        );
    }
  }
  const refilter = () => {
    showing = PAGE;
    showEvents();
  };
  $("evSensor").onchange = refilter;
  $("evKind").onchange = refilter;
  $("evMore").onclick = () => {
    showing += PAGE;
    showEvents();
  };
  (window.viewHooks = window.viewHooks || []).push((view) => {
    if (view == "events") {
      showing = PAGE;
      opened.clear();
      loadEvents();
      repeat(loadEvents, 3000);
    }
    setWalk(view == "walk");
  });
  Object.assign(window.hub, { explain });
})();
