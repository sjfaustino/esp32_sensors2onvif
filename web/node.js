// One node's own page, opened as #node:AA-BB-CC-DD-EE-01 from its address: its board and firmware, the hardware it
// runs, its battery over time, its signal, restarts and update, each of its sensors with its week, and its latest events.
(() => {
  const { fill, heardAgo, signal, batteryLeft, sensorLink, chip, explain, checkStep } = window.hub;
  const SENSORS = {
    1: tr("generic, active high"),
    2: tr("generic, active low"),
    3: "RCWL-0516",
    4: "HC-SR501",
    5: "LD2410",
  };
  const CELLS = { 0: tr("none"), 1: tr("lithium-ion"), 2: "LiFePO4" };
  const DETECTED = [0, 3, 10, 13]; // its alerts, whether sent to the NVR or only logged: each shows it saw someone
  let shown = "",
    drawn = "",
    slowAt = 0,
    lastTree = null,
    lastEvents = [],
    checked = null, // when its first hour's checks all passed, from checked.txt in its folder; "" for not yet
    again = ""; // when they were started over, by the hub's clock: only events after it count
  // Its first hour: heard at a good signal, a test alert the NVR got, and a walk past each of its sensors. Each is ticked
  // as the hub sees it, from its reports and the latest events; once all three are, checked.txt keeps that they were.
  // Checked again, it keeps when instead, as again=TIME COUNT: the newest event's time then, and how many had that time,
  // as the hub's times are in whole seconds. Only events after those count.
  function sinceAgain(all) {
    if (!again) return all;
    const [time, count] = again.split(" ");
    let fresh = all.filter((e) => e.received == time).length - (+count || 0); // the newest of that second are new
    return all.filter((e) => e.received > time || (e.received == time && fresh-- > 0));
  }
  function firstHour(mac, node, rows, all) {
    const events = sinceAgain(all),
      rowOf = (i) => rows.find((r) => r.input == i) || {},
      mine = (i) => events.filter((e) => e.sensor == mac + "/" + i),
      heard = rows.filter((r) => r.rssi != null),
      worst = heard.reduce((w, r) => (w == null || r.rssi < w.rssi ? r : w), null),
      good = rows.length > 0 && heard.length == rows.length && heard.every((r) => r.rssi >= -75 && !r.weak);
    // A test alert reached the NVR when no "not delivered" came after it, once the hub has checked: when it is over.
    const tested = (i) => {
      const list = mine(i),
        t = list.findIndex((e) => e.kind == 15);
      return t < 0
        ? "none"
        : list.slice(0, t).some((e) => e.kind == 5)
          ? "missed"
          : rowOf(i).state == "active"
            ? "checking"
            : "reached";
    };
    const tests = node.sensors.map((s) => tested(s.input)),
      walked = node.sensors.every((s) => mine(s.input).some((e) => DETECTED.includes(e.kind) && e.active));
    if (tests.includes("checking")) slowAt = 0; // the events again on the next refresh, to see the check's outcome
    const reached = tests.length > 0 && tests.every((t) => t == "reached");
    return {
      done: good && reached && walked,
      steps: [
        checkStep(
          1,
          good,
          tr("Heard at a good signal"),
          !worst
            ? tr("Not heard yet: press its reset button, and it reports at once.")
            : worst.weak
              ? tr(
                  "Its signal is {signal}, and weak on average. Move the node, or the hub, until it is good or strong: higher, nearer, with fewer walls between them. Each report updates this.",
                  { signal: signal(worst.rssi) },
                )
              : tr(
                  "Its signal is {signal}. Move the node, or the hub, until it is good or strong: higher, nearer, with fewer walls between them. Each report updates this.",
                  { signal: signal(worst.rssi) },
                ),
        ),
        checkStep(
          2,
          reached,
          tr("A test alert reached the NVR"),
          tests.includes("missed")
            ? tr(
                "The NVR did not get its test alert. See on the Overview that the NVR is listening, then send another.",
              )
            : tests.includes("checking")
              ? tr("Sent. Once the alert is over, the hub sees whether the NVR got it.")
              : tr("Send it a test alert: the NVR should record, as it would for an intruder."),
          el("button", {
            textContent: tr("Send a test alert"),
            onclick: async (ev) => {
              ev.target.disabled = true;
              try {
                for (const s of node.sensors) await call("POST", "/api/test-alert?path=" + mac + "/" + s.input, "x=1");
              } catch (e) {}
              slowAt = 0;
              load(mac, false);
            },
          }),
        ),
        checkStep(
          3,
          walked,
          tr("Walk past it"),
          tr(
            "Walk past it from where an intruder would come, and each of its sensors must see you. During a walk test its light flashes 3, 2 or 1 times for a strong, fair or weak signal.",
          ),
          el("button", {
            textContent: tr("Start a walk test on it"),
            onclick: () => call("POST", "/api/walk-test?path=" + mac, "go=1").catch(() => {}),
          }),
        ),
      ],
    };
  }
  async function showFirstHour(mac, node, rows) {
    if (checked == null) {
      const r = await fetch("/api/file?path=" + mac + "/checked.txt");
      const text = r.ok ? await r.text() : "";
      checked = (text.match(/^checked=(.*)$/m) || [])[1] || "";
      again = (text.match(/^again=(.*)$/m) || [])[1] || "";
    }
    if (shown != mac) return;
    if (!checked) {
      const hour = firstHour(mac, node, rows, lastEvents);
      if (hour.done) {
        const at = new Date().toISOString();
        await call("PUT", "/api/file?path=" + mac + "/checked.txt", "checked=" + at + "\n");
        checked = at;
      } else {
        $("ndCheckNote").textContent = tr("Three checks before you trust it, each ticked as the hub sees it done.");
        $("ndCheck").replaceChildren(...hour.steps);
        return;
      }
    }
    const when = Date.parse(checked);
    $("ndCheckNote").replaceChildren(
      chip(tr("Checked"), "ok"),
      " " +
        (isNaN(when)
          ? tr("Its signal was good, the NVR got its test alert, and it saw someone walk past.")
          : tr("Its signal was good, the NVR got its test alert, and it saw someone walk past, by {date}.", {
              date: new Date(when).toLocaleDateString(),
            })) +
        " ",
      el("button", {
        textContent: tr("Check it again"),
        onclick: async () => {
          try {
            lastEvents = await call("GET", "/api/events"); // as they are now, not as last read
            const newest = (lastEvents.find((e) => e.received) || {}).received || "",
              mark = newest && newest + " " + lastEvents.filter((e) => e.received == newest).length;
            await call("PUT", "/api/file?path=" + mac + "/checked.txt", "again=" + mark + "\n");
            checked = "";
            again = mark;
          } catch (e) {}
          load(mac, false);
        },
      }),
    );
    $("ndCheck").replaceChildren();
  }
  // "NUMBER,SENSOR,SENSORPIN,TAMPERPIN,BATTERYPIN,CELL", as the hub keeps it, in words.
  function hardwareWords(text) {
    const v = (text || "").split(",").map(Number);
    if (v.length != 6) return "";
    const pin = (p) => (p == 255 ? tr("none") : "GPIO " + p);
    return tr("{sensor} on {pin}; tamper switch: {tamper}; battery: {battery}", {
      sensor: SENSORS[v[1]] || tr("sensor {n}", { n: v[1] }),
      pin: pin(v[2]),
      tamper: pin(v[3]),
      battery: v[5] ? tr("{cell} on {pin}", { cell: CELLS[v[5]], pin: pin(v[4]) }) : tr("none"),
    });
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
        lastEvents = events;
      }
      if (shown != mac) return;
      const rows = esp.nodes.filter((n) => n.node == mac),
        node = tree.nodes.find((n) => n.mac == mac);
      if (!node) {
        $("ndTitle").textContent = tr("No such node");
        $("ndFacts").replaceChildren();
        return;
      }
      const first = rows[0] || {},
        names = node.sensors.map((s) => s.name).filter(Boolean);
      $("ndTitle").textContent = names.join(", ") || mac;
      const runs = hardwareWords(node.hwRuns),
        set = hardwareWords(node.hwSet);
      fill("ndFacts", [
        [tr("Address"), mac],
        [tr("Board"), first.chip || tr("Not known yet")],
        [
          tr("Firmware"),
          (first.version || tr("Not known yet")) +
            (first.update ? ", " + hubText(first.update) : "") +
            (first.restarts == null
              ? ""
              : "; " + tr("{n} restarts, the last: {why}", { n: first.restarts, why: hubText(first.restart) })),
        ],
        [
          tr("Hardware"),
          runs
            ? runs +
              (node.hwSet && node.hwSet != node.hwRuns
                ? " " + tr("(it takes {set} after its next report)", { set })
                : "")
            : tr("Its firmware is from before hardware settings"),
        ],
        [
          tr("Paired"),
          node.paired
            ? first.sealed
              ? tr("Yes, its messages sealed")
              : tr("Yes")
            : tr("No: it uses the shared ESP-NOW key"),
        ],
        [tr("Last heard"), heardAgo(first.ago)],
        [tr("Signal"), signal(first.rssi) + (first.weak ? tr(", weak on average") : "")],
        [
          tr("Battery"),
          (first.battery == null ? tr("Not known") : first.battery + "%" + (first.low ? tr(" (low)") : "")) +
            (first.saving ? tr(", saving it") : "") +
            (first.daysLeft == null ? "" : ", " + batteryLeft(first.daysLeft)),
        ],
        [
          tr("Reports"),
          first.every ? tr("every {n} minutes", { n: Math.round(first.every / 60) }) : tr("Not known yet"),
        ],
      ]);
      await showFirstHour(mac, node, rows);
      const STATES = {
        active: tr("firing now"),
        idle: tr("quiet"),
        silent: tr("silent"),
        never: tr("not heard yet"),
      };
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
                sensorLink(mac + "/" + s.input, s.name || tr("Sensor {n}", { n: s.input })),
                " ",
                chip(
                  STATES[row.state] || tr("unknown"),
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
                  el("b", { textContent: hubText(e.event).charAt(0).toUpperCase() + hubText(e.event).slice(1) }),
                  " " + (e.name || e.sensor) + ", " + (e.received ? hub.ago(e.received) : "") + ". ",
                  explain(e),
                ),
              )
            : [el("li", { textContent: tr("None among the latest 64 events.") })]),
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
    checked = null;
    again = "";
    lastEvents = [];
    load(arg, true);
    hub.repeat(() => load(arg, false), 5000);
  });
})();
