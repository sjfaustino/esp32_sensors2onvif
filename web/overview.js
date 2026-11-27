// The Overview: arming and disarming, what happened since the hub was armed, the hub at a glance, and Getting
// started.
(() => {
  const { fill, chip, readyItems, sensorLink, showStatus, showMode, MODE_WORDS, repeat } = window.hub;
  // The panel's icon for each mode: a closed lock, a house, an open lock. Fixed markup, never from the hub.
  const ICONS = {
    away: '<rect x="5" y="11" width="14" height="10" rx="2"/><path d="M8 11V7a4 4 0 0 1 8 0v4"/>',
    home: '<path d="M3 11l9-7 9 7"/><path d="M5 10v10h14V10"/><path d="M10 20v-6h4v6"/>',
    disarmed: '<rect x="5" y="11" width="14" height="10" rx="2"/><path d="M8 11V7a4 4 0 0 1 7.5-2"/>',
  };
  // Armed away, armed home, or disarmed. Home Assistant can change it too, so the page asks again while it shows it.
  // A disarm can last a while and then end by itself, so forgetting to arm again cannot leave the house unwatched.
  const DISARM_TIMES = [
    [30, tr("30 minutes")],
    [60, tr("1 hour")],
    [120, tr("2 hours")],
    [240, tr("4 hours")],
    [480, tr("8 hours")],
    [720, tr("12 hours")],
    [1440, tr("1 day")],
    [0, tr("until armed again")],
  ];
  for (const id of ["armFor", "rearmDefault"])
    $(id).replaceChildren(
      ...DISARM_TIMES.map(([m, t]) =>
        el("option", { value: m, textContent: id == "armFor" && m ? tr("for {time}", { time: t }) : t }),
      ),
    );
  let armWasMode = null,
    armMode = null; // as the hub last said, for a change to be undone back to
  function timeLeft(s) {
    const m = Math.ceil(s / 60);
    return m < 60
      ? tr("{m} min", { m })
      : m % 60
        ? tr("{h} h {m} min", { h: Math.floor(m / 60), m: m % 60 })
        : tr("{h} h", { h: Math.floor(m / 60) });
  }
  async function loadArm() {
    try {
      const a = await call("GET", "/api/arm");
      armMode = a.mode;
      $("armPanel").className = "armPanel " + a.mode;
      $("armTitle").textContent = MODE_WORDS[a.mode] || a.mode;
      $("armState").textContent =
        a.mode == "away"
          ? tr("Every sensor counts.")
          : a.mode == "home"
            ? tr("Interior sensors are logged but not sent to the NVR.")
            : (!a.timed
                ? tr("Until armed again.")
                : a.left_s != null
                  ? tr("Arms again in {time}.", { time: timeLeft(a.left_s) })
                  : tr("Arms again soon.")) +
              " " +
              tr("Alerts are logged but not sent to the NVR, so nothing is recorded.");
      if ($("armIcon").dataset.mode != a.mode) {
        $("armIcon").innerHTML =
          '<svg width="30" height="30" viewBox="0 0 24 24" fill="none" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">' +
          (ICONS[a.mode] || "") +
          "</svg>";
        $("armIcon").dataset.mode = a.mode;
      }
      showMode(a.mode);
      if (armWasMode !== a.mode) $("armFor").value = a.rearm; // only when the mode changes, so a choice being made stays
      armWasMode = a.mode;
      if (document.activeElement != $("rearmDefault")) $("rearmDefault").value = a.rearm;
      $("armAway").disabled = a.mode == "away";
      $("armHome").disabled = a.mode == "home";
      for (const [id, m] of [
        ["armAway", "away"],
        ["armHome", "home"],
        ["disarmBtn", "disarmed"],
      ])
        $(id).setAttribute("aria-pressed", a.mode == m);
      $("armWarn").hidden = a.mode != "disarmed";
    } catch (e) {}
  }
  // A short next step for each problem the Overview lists.
  const PROBLEM_HINT = {
    silent: tr("check its battery, or whether it was moved"),
    "low battery": tr("replace or charge it"),
    "weak signal": tr("move it or the hub closer"),
    "messages refused": tr("it may have the wrong key; pair it again"),
  };
  // "Since armed away at 23:10: 2 alerts, Back door 02:14 and Gate 02:15; all reached the NVR", from the events kept.
  function sinceArmed(events, mode, nvrSet) {
    const ARM = [11, 14],
      at = (e) => (e.received ? new Date(e.received) : null),
      clock = (d) =>
        !d
          ? "?"
          : d.toDateString() == new Date().toDateString()
            ? d.toLocaleTimeString([], { hour: "2-digit", minute: "2-digit" })
            : d.toLocaleString([], { weekday: "short", hour: "2-digit", minute: "2-digit" });
    let end = 0;
    if (mode == "disarmed") {
      end = events.findIndex((e) => e.kind == 12);
      if (end < 0) return "";
    }
    const start = events.findIndex((e, i) => i > end && ARM.includes(e.kind));
    const span = events.slice(mode == "disarmed" ? end + 1 : 0, start < 0 ? events.length : start);
    const alerts = span.filter((e) => e.sensor != "hub" && ((e.kind == 0 && e.active) || e.kind == 15)).reverse(),
      missed = span.filter((e) => e.kind == 5).length,
      // The NVR listened again after the newest of them: the hub played them to it then.
      lastMissed = events.findIndex((e) => e.kind == 5),
      caughtUp = lastMissed >= 0 && events.slice(0, lastMissed).some((e) => e.kind == 7);
    const head =
      start < 0
        ? mode == "disarmed"
          ? tr("Before the disarm at {time}", { time: clock(at(events[end])) })
          : tr("Since it was last armed")
        : mode == "disarmed"
          ? tr("While armed, {from} to {to}", { from: clock(at(events[start])), to: clock(at(events[end])) })
          : tr("Since armed at {time}", { time: clock(at(events[start])) });
    const count =
      start < 0
        ? trCount(alerts.length, "at least {n} alert", "at least {n} alerts")
        : trCount(alerts.length, "{n} alert", "{n} alerts");
    return (
      head +
      ": " +
      (alerts.length
        ? count +
          ", " +
          alerts
            .slice(0, 6)
            .map((e) => (e.name || e.sensor) + " " + clock(at(e)))
            .join(", ") +
          (alerts.length > 6 ? tr(" and more") : "") +
          "; " +
          (missed
            ? caughtUp
              ? tr("{n} came while the NVR was not listening, and were played to it once it listened again", {
                  n: missed,
                })
              : tr("{n} did not reach the NVR yet; the hub keeps them until it listens", { n: missed })
            : nvrSet
              ? tr("all reached the NVR")
              : tr("with no NVR address set, the hub cannot tell whether they reached it"))
        : tr("no alerts."))
    );
  }
  // What someone checking the hub wants at a glance. Each line says what is wrong, if anything. The summary every few
  // seconds; the arm panel's details and the alerts since arming only when the summary shows a change, or every 30 s.
  let changeKey = "",
    fullAt = 0;
  async function loadOverview(force) {
    try {
      const o = await call("GET", "/api/overview");
      const key = JSON.stringify([o.mode, o.last, o.active.length]),
        full = force === true || key != changeKey || Date.now() - fullAt > 30000;
      if (full) {
        changeKey = key;
        fullAt = Date.now();
        loadArm();
      }
      const who = (s) => (s.name ? s.name + " (" + s.sensor + ")" : s.sensor);
      const listOf = (items, line) =>
        el("span", null, ...(items.length ? items.flatMap((x, i) => (i ? ["; "] : []).concat(line(x))) : [tr("None")]));
      // Home Assistant's connection, as the hub words it: not set up, connected, connecting, or why it failed.
      const mqtt = o.mqtt || "",
        mqttChip =
          mqtt == "not set up" // not words: as the hub says it
            ? chip(tr("Not set up"), "none")
            : mqtt == "connected"
              ? chip(tr("Connected"), "ok")
              : mqtt == "connecting"
                ? chip(tr("Connecting"), "warn")
                : chip(hubText(mqtt).charAt(0).toUpperCase() + hubText(mqtt).slice(1), "bad");
      fill("ovNow", [
        [
          tr("Alerts"),
          listOf(o.active, (a) => [sensorLink(a.sensor, who(a)), a.past ? tr(" (a past event replaying)") : ""]),
        ],
        [
          tr("Sensor problems"),
          listOf(o.problems, (p) => [
            sensorLink(p.sensor, who(p)),
            ": " + hubText(p.problem) + (PROBLEM_HINT[p.problem] ? " (" + PROBLEM_HINT[p.problem] + ")" : ""),
          ]),
        ],
        [
          tr("Last alert"),
          o.last
            ? who(o.last) +
              ", " +
              hubText(o.last.event) +
              (o.last.received ? ", " + new Date(o.last.received).toLocaleString() : "")
            : tr("None in the history"),
        ],
      ]);
      fill("ovInfo", [
        [
          "NVR",
          !o.nvr.set
            ? chip(tr("No address set"), "warn")
            : o.nvr.listening
              ? chip(o.nvr.streams ? tr("Listening, watching the video") : tr("Listening"), "ok")
              : chip(tr("Not listening: alerts are not reaching it"), "bad"),
        ],
        // Only while alerts wait: kept for it, as while it is away, and played to it once it listens again.
        ...(o.nvr.waiting ? [[tr("Waiting for the NVR"), waitingCell(o.nvr.waiting)]] : []),
        [
          tr("Notifications"),
          !o.webhook.set
            ? chip(tr("Not set up"), "none")
            : o.webhook.last
              ? tr("Last message: {result}", { result: hubText(o.webhook.last) })
              : tr("Nothing sent yet"),
        ],
        ["Home Assistant", mqttChip], // not words: a name
      ]);
      if (full)
        call("GET", "/api/events")
          .then((events) => ($("ovSince").textContent = sinceArmed(events, o.mode, o.nvr.set)))
          .catch(() => {});
      showStatus(o);
      const items = readyItems(o);
      $("ovReadyBox").hidden = items.every((i) => i.ok);
      showWizard(items);
      $("ovReady").replaceChildren(
        ...items.map((i) =>
          el(
            "li",
            null,
            el("span", { className: i.ok ? "yes" : "no", textContent: i.ok ? "✓ " : "✗ " }),
            i.text,
            i.ok ? "" : el("a", { href: "#" + i.view, textContent: tr("Fix") }),
          ),
        ),
      );
    } catch (e) {}
  }
  // Until every step is done, the first one left, shown at the top of the Overview with what to do. Hidden for now, the
  // list stays at the foot of the page.
  function laterKey() {
    try {
      return localStorage.getItem("wizardLater") == "1";
    } catch (e) {
      return false;
    }
  }
  function showWizard(items) {
    const left = items.filter((i) => !i.ok),
      next = left[0];
    $("wizard").hidden = !next || laterKey();
    $("wizBack").hidden = !$("wizard").hidden;
    // While the step shown is the test, its button is the page's Test everything, so the page does not offer it twice.
    $("testRow").hidden = !$("wizard").hidden && !!next.run;
    if (!next) return;
    const done = items.length - left.length;
    $("wizTitle").textContent = done
      ? tr("Getting started: step {n} of {total}", { n: done + 1, total: items.length })
      : tr("Getting started");
    $("wizDone").style.width = Math.round((100 * done) / items.length) + "%";
    $("wizStep").textContent = next.text;
    $("wizHelp").textContent = next.help;
    $("wizGo").textContent = next.go;
    $("wizGo").onclick = () => (next.run ? next.run() : (location.hash = next.view));
  }
  $("wizLater").onclick = () => {
    try {
      localStorage.setItem("wizardLater", "1");
    } catch (e) {}
    $("wizard").hidden = true;
    $("wizBack").hidden = false;
    $("testRow").hidden = false;
  };
  $("wizBack").onclick = (e) => {
    e.preventDefault();
    try {
      localStorage.removeItem("wizardLater");
    } catch (er) {}
    loadOverview();
    scrollTo(0, 0);
  };
  // Changes the mode and says so at the foot of the page, with a way back to the mode before. A refusal is said there
  // too, with the hub's reason, and the page goes on showing the hub as it is.
  async function setMode(mode, minutes, done) {
    const before = armMode;
    try {
      await call("POST", "/api/arm", "mode=" + mode + "&for=" + (minutes || 0));
      notice(done || MODE_WORDS[mode], {
        undo:
          before && before != mode
            ? () => setMode(before, 0, tr("Back to {mode}", { mode: MODE_WORDS[before].toLowerCase() }))
            : null,
      });
    } catch (e) {
      notice(tr("Not changed: {why}", { why: e.message }), { err: true });
    }
    loadArm();
  }
  // Alerts waiting for the NVR, with a button to clear them, as test alerts it was away for would otherwise be recorded
  // later as past events. The hub logs the clearing.
  function waitingCell(n) {
    const clear = el("button", {
      className: "del",
      textContent: tr("Clear"),
      onclick: async () => {
        if (
          !(await ask(
            tr(
              "Clear them only if they were tests: the NVR will not record them. The hub logs that they were cleared.",
            ),
            { title: tr("Clear the {n} alerts waiting for the NVR?", { n }), ok: tr("Clear") },
          ))
        )
          return;
        try {
          const r = await call("POST", "/api/waiting/clear", "x=1");
          notice(tr("Cleared {n} waiting alerts", { n: r.cleared }));
        } catch (e) {
          notice(tr("Not cleared: {why}", { why: e.message }), { err: true });
        }
        loadOverview(true);
      },
    });
    return el("span", null, tr("{n} alerts, played once it listens", { n }) + " ", clear);
  }
  $("armAway").onclick = () => setMode("away");
  $("armHome").onclick = () => setMode("home");
  $("disarmBtn").onclick = async () => {
    const minutes = $("armFor").value,
      how = DISARM_TIMES.find(([m]) => m == minutes)[1],
      when = minutes == 0 ? how : tr("for {time}", { time: how });
    if (
      await ask(tr("Alerts will be logged but not sent to the NVR, so nothing is recorded."), {
        title: tr("Disarm the hub {when}?", { when }),
        ok: tr("Disarm"),
      })
    )
      setMode("disarmed", minutes, tr("Disarmed {when}", { when }));
  };
  $("rearmDefault").onchange = async () => {
    try {
      await call("POST", "/api/arm/rearm", "minutes=" + $("rearmDefault").value);
      notice(tr("Saved"));
    } catch (e) {
      notice(tr("Not saved: {why}", { why: e.message }), { err: true });
    }
    loadArm();
  };
  (window.viewHooks = window.viewHooks || []).push((view) => {
    if (view == "overview") {
      loadOverview(true);
      repeat(loadOverview, 3000);
    }
  });
  Object.assign(window.hub, { loadOverview });
})();
