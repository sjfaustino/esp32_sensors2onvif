// The Events page, with what each event means, and the walk test.
(() => {
  const { ago, headRow, heardAgo, sensorLink, signal, repeat } = window.hub;
  // What each event means and what to do about it, for the Events page and each sensor's own page.
  const EXPLAIN = {
    0: [
      "The sensor saw something, and the NVR was told, so it records.",
      "Check the recording if nobody was expected.",
    ],
    1: [
      "No report from this sensor for longer than its silent time. Its battery may be flat, or it was unplugged, moved or taken.",
      "Check the sensor and its battery. This clears by itself once it reports again.",
    ],
    2: ["Its battery is at 20% or less.", "Replace or charge it soon."],
    3: [
      "The sensor fired during its quiet hours, so the NVR was not told.",
      "If this should have been recorded, change the quiet hours in the sensor's Alert settings.",
    ],
    4: [
      "Its radio signal has stayed below -80 dBm, so its messages may be lost.",
      "Move the node or the hub closer, or raise the node off the ground and away from metal.",
    ],
    5: [
      "The NVR was not listening when this alert came, so it did not record it then. The hub keeps it and plays it as a past event once the NVR listens again.",
      "Check that the NVR is on and on the network.",
    ],
    6: [
      "The NVR has not been listening for 3 minutes, so alerts are not being recorded.",
      "Check that the NVR is on, on the network, and still has the hub's channel set up.",
    ],
    7: ["The NVR is listening again. Alerts kept for it play now.", ""],
    8: ["The hub started, for the reason shown.", "If power cuts are common, a small UPS keeps it running."],
    9: [
      "The hub was off its Wi-Fi for the time shown. Alerts in that time reached the NVR afterwards, as past events.",
      "If this happens often, check the Wi-Fi where the hub is. Someone may be keeping it off on purpose.",
    ],
    10: [
      "The sensor fired while the hub was disarmed, so the NVR was not told.",
      "Arm the hub whenever nobody is home.",
    ],
    11: ["The hub was armed, from where the details say.", ""],
    12: ["The hub was disarmed, from where the details say. Alerts are logged but not recorded.", ""],
    13: [
      "An interior sensor fired while the hub was armed home, so the NVR was not told.",
      "If it should always record, mark it perimeter in its Alert settings.",
    ],
    14: ["The hub was armed home: interior sensors are logged but not recorded.", ""],
    15: [
      "A test alert, sent from the page or Home Assistant. It records like a real one.",
      "Check that the NVR has the recording.",
    ],
    16: [
      "The hub restarted by itself three times within an hour: it is crashing, losing power or being restarted.",
      "Check its power supply. Device Info shows the reasons.",
    ],
    17: [
      "The node restarted because of a fault, such as a brownout from a weak battery, or a bad wire.",
      "Check its battery and its wiring.",
    ],
    18: ["From its readings, the battery will run out within a week.", "Replace or charge it."],
    19: [
      "Several nodes stopped reporting at the same time. Batteries fail one at a time, so something may be blocking the radio, such as a jammer.",
      "Check the property now: this can mean a break-in.",
    ],
    20: [
      "The sensor fired 20 times within an hour. Every alert still went to the NVR, but a fault or a bad aim is likelier than that many intruders.",
      "Check what it sees, such as moving plants, sun, pets or heating, and its wiring.",
    ],
  };
  function explain(e) {
    if (e.kind == 0 && !e.active)
      return "The sensor reports nothing in front of it any more. Its alert ends when the alert time runs out.";
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
        ...(esp.nodes.length ? [headRow(["Sensor", "", "Signal", "Hub heard it in"])] : []),
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
                  ? "✓ fired" +
                    (f.count > 1 ? " " + f.count + " times" : "") +
                    ", " +
                    heardAgo(Math.round((Date.now() - f.at) / 1000))
                  : "waiting",
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
        $("walkMsg").textContent = "No sensor nodes yet. Pair one under Sensors Manager > Nodes and pairing.";
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
      $("walkMsg").textContent =
        "On for " +
        n +
        (n == 1 ? " node" : " nodes") +
        " for 10 minutes. Walk past each sensor; each node starts on its next report.";
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
      $("walkMsg").textContent = "Stopped. What fired stays listed until you leave this page.";
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
      walkTimer = setInterval(walkTick, 1500);
    }
  }
  const DIRECTIONS = ["", "approaching", "moving away", "left to right", "right to left"];
  // The same wording as the hub's own messages: "7 min", "2 h 13 min", "3 days 4 h".
  function duration(s) {
    const m = Math.ceil(s / 60);
    return m < 60
      ? m + " min"
      : m < 2880
        ? Math.floor(m / 60) + " h " + (m % 60) + " min"
        : Math.floor(m / 1440) + " days " + (Math.floor(m / 60) % 24) + " h";
  }
  // Filtered here, from the list the hub sends, so the hub builds nothing more for it.
  const ALERT_KINDS = [0, 3, 5, 10, 13, 15],
    PROBLEM_KINDS = [1, 2, 4, 5, 6, 9, 16, 17, 18, 19, 20];
  let events = [];
  async function loadEvents() {
    try {
      events = await call("GET", "/api/events");
      const seen = [...new Set(events.map((e) => e.sensor))].sort(),
        chosen = $("evSensor").value;
      $("evSensor").replaceChildren(
        el("option", { value: "", textContent: "every sensor and the hub" }),
        ...seen.map((s) => {
          const named = events.find((e) => e.sensor == s && e.name);
          return el("option", {
            value: s,
            textContent: s == "hub" ? "the hub" : named ? named.name + " (" + s + ")" : s,
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
        ...(list.length ? [headRow(["Time", "Sensor", "Event", "Details", "Battery"])] : []),
        ...list.map((e) => {
          const when = e.happened || e.received,
            what = e.event.charAt(0).toUpperCase() + e.event.slice(1); // the hub names every event
          const off = e.off_s != null ? "off for up to " + duration(e.off_s) : "";
          const details = [e.detail || "", off, e.camera && e.active ? "look at " + e.camera : ""]
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
            { className: "explains", title: "Tap for what this means" },
            el("td", {
              textContent: when ? ago(when) : "Clock not set",
              title: when ? new Date(when).toLocaleString() : "",
            }),
            el("td", null, e.sensor == "hub" ? "Hub" : sensorLink(e.sensor, e.name || e.sensor)),
            ...[what, details || "-", e.battery == null ? "-" : e.battery + "%"].map((v) =>
              el("td", { textContent: v }),
            ),
          );
          row.onclick = () => {
            const next = row.nextElementSibling;
            if (next && next.classList.contains("explanation")) next.remove();
            else row.after(el("tr", { className: "explanation" }, el("td", { colSpan: 5, textContent: explain(e) })));
          };
          return row;
        }),
      );
      $("evNone").textContent = list.length
        ? ""
        : events.length
          ? "None of these among the latest 64 events."
          : "No events yet.";
    }
  }
  $("evSensor").onchange = showEvents;
  $("evKind").onchange = showEvents;
  (window.viewHooks = window.viewHooks || []).push((view) => {
    if (view == "events") {
      loadEvents();
      repeat(loadEvents, 3000);
    }
    setWalk(view == "walk");
  });
  Object.assign(window.hub, { explain });
})();
