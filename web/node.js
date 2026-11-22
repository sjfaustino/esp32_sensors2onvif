// One node's own page, opened as #node:AA-BB-CC-DD-EE-01 from its address: its board and firmware, the hardware it
// runs, its battery over time, its signal, restarts and update, each of its sensors with its week, and its latest events.
(() => {
  const { fill, heardAgo, signal, batteryLeft, sensorLink, chip, explain } = window.hub;
  const SENSORS = { 1: "generic, active high", 2: "generic, active low", 3: "RCWL-0516", 4: "HC-SR501", 5: "LD2410" };
  const CELLS = { 0: "none", 1: "lithium-ion", 2: "LiFePO4" };
  let shown = "",
    drawn = "",
    slowAt = 0,
    lastTree = null;
  // "NUMBER,SENSOR,SENSORPIN,TAMPERPIN,BATTERYPIN,CELL", as the hub keeps it, in words.
  function hardwareWords(text) {
    const v = (text || "").split(",").map(Number);
    if (v.length != 6) return "";
    const pin = (p) => (p == 255 ? "none" : "GPIO " + p);
    return (
      (SENSORS[v[1]] || "sensor " + v[1]) +
      " on " +
      pin(v[2]) +
      "; tamper switch: " +
      pin(v[3]) +
      "; battery: " +
      (v[5] ? CELLS[v[5]] + " on " + pin(v[4]) : "none")
    );
  }
  async function load(mac, full) {
    try {
      // Its facts every few seconds; what changes seldom, its sensors and events, on opening and every 30 s.
      const slow = full || Date.now() - slowAt > 30000;
      const [esp, tree, events] = await Promise.all([
        call("GET", "/api/espnow"),
        slow ? call("GET", "/api/tree") : Promise.resolve(lastTree),
        slow ? call("GET", "/api/events") : Promise.resolve(null),
      ]);
      if (slow) {
        slowAt = Date.now();
        lastTree = tree;
      }
      if (shown != mac) return;
      const rows = esp.nodes.filter((n) => n.node == mac),
        node = tree.nodes.find((n) => n.mac == mac);
      if (!node) {
        $("ndTitle").textContent = "No such node";
        $("ndFacts").replaceChildren();
        return;
      }
      const first = rows[0] || {},
        names = node.sensors.map((s) => s.name).filter(Boolean);
      $("ndTitle").textContent = names.join(", ") || mac;
      const runs = hardwareWords(node.hwRuns),
        set = hardwareWords(node.hwSet);
      fill("ndFacts", [
        ["Address", mac],
        ["Board", first.chip || "Not known yet"],
        [
          "Firmware",
          (first.version || "Not known yet") +
            (first.update ? ", " + first.update : "") +
            (first.restarts == null ? "" : "; " + first.restarts + " restarts, the last: " + first.restart),
        ],
        [
          "Hardware",
          runs
            ? runs + (node.hwSet && node.hwSet != node.hwRuns ? " (it takes " + set + " after its next report)" : "")
            : "Its firmware is from before hardware settings",
        ],
        [
          "Paired",
          node.paired ? (first.sealed ? "Yes, its messages sealed" : "Yes") : "No: it uses the shared ESP-NOW key",
        ],
        ["Last heard", heardAgo(first.ago)],
        ["Signal", signal(first.rssi) + (first.weak ? ", weak on average" : "")],
        [
          "Battery",
          (first.battery == null ? "Not known" : first.battery + "%" + (first.low ? " (low)" : "")) +
            (first.saving ? ", saving it" : "") +
            (first.daysLeft == null ? "" : ", " + batteryLeft(first.daysLeft)),
        ],
        ["Reports", first.every ? "every " + Math.round(first.every / 60) + " minutes" : "Not known yet"],
      ]);
      const STATES = { active: "firing now", idle: "quiet", silent: "silent", never: "not heard yet" };
      // Drawn once a visit, so the refresh leaves their weeks alone.
      if (full)
        $("ndSensors").replaceChildren(
          ...node.sensors.map((s) => {
            const row = rows.find((r) => r.input == s.input) || {};
            return el(
              "section",
              { className: "card" },
              el(
                "h2",
                null,
                sensorLink(mac + "/" + s.input, s.name || "Sensor " + s.input),
                " ",
                chip(
                  STATES[row.state] || "unknown",
                  row.state == "active" ? "bad" : row.state == "silent" ? "warn" : "none",
                ),
              ),
              el("div", { id: "ndWeek" + s.input }),
            );
          }),
        );
      const mine = events ? events.filter((e) => e.sensor.startsWith(mac + "/")).slice(0, 15) : null;
      if (mine)
        $("ndEvents").replaceChildren(
          ...(mine.length
            ? mine.map((e) =>
                el(
                  "li",
                  null,
                  el("b", { textContent: e.event.charAt(0).toUpperCase() + e.event.slice(1) }),
                  " " + (e.name || e.sensor) + ", " + (e.received ? hub.ago(e.received) : "") + ". ",
                  explain(e),
                ),
              )
            : [el("li", { textContent: "None among the latest 64 events." })]),
        );
      if (full && drawn != mac) {
        drawn = mac;
        window.sensorCharts.drawBattery(mac, $("ndBattery"));
        for (const s of node.sensors) // one at a time: the hub sends one history download at a time
          await window.sensorCharts.drawWeek(mac + "/" + s.input, $("ndWeek" + s.input), () => shown == mac);
      }
    } catch (e) {}
  }
  (window.viewHooks = window.viewHooks || []).push((view, arg) => {
    if (view != "node") {
      shown = "";
      return;
    }
    shown = arg;
    drawn = "";
    load(arg, true);
    hub.repeat(() => load(arg, false), 5000);
  });
})();
