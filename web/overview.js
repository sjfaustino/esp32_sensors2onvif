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
    [30, "30 minutes"],
    [60, "1 hour"],
    [120, "2 hours"],
    [240, "4 hours"],
    [480, "8 hours"],
    [720, "12 hours"],
    [1440, "1 day"],
    [0, "until armed again"],
  ];
  for (const id of ["armFor", "rearmDefault"])
    $(id).replaceChildren(
      ...DISARM_TIMES.map(([m, t]) => el("option", { value: m, textContent: (id == "armFor" && m ? "for " : "") + t })),
    );
  let armWasMode = null,
    armMode = null; // as the hub last said, for a change to be undone back to
  function timeLeft(s) {
    const m = Math.ceil(s / 60);
    return m < 60 ? m + " min" : Math.floor(m / 60) + " h" + (m % 60 ? " " + (m % 60) + " min" : "");
  }
  async function loadArm() {
    try {
      const a = await call("GET", "/api/arm");
      armMode = a.mode;
      $("armPanel").className = "armPanel " + a.mode;
      $("armTitle").textContent = MODE_WORDS[a.mode] || a.mode;
      $("armState").textContent =
        a.mode == "away"
          ? "Every sensor counts."
          : a.mode == "home"
            ? "Interior sensors are logged but not sent to the NVR."
            : (!a.timed
                ? "Until armed again."
                : a.left_s != null
                  ? "Arms again in " + timeLeft(a.left_s) + "."
                  : "Arms again soon.") + " Alerts are logged but not sent to the NVR, so nothing is recorded.";
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
    silent: "check its battery, or whether it was moved",
    "low battery": "replace or charge it",
    "weak signal": "move it or the hub closer",
    "messages refused": "it may have the wrong key; pair it again",
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
          ? "Before the disarm at " + clock(at(events[end]))
          : "Since it was last armed"
        : mode == "disarmed"
          ? "While armed, " + clock(at(events[start])) + " to " + clock(at(events[end]))
          : "Since armed at " + clock(at(events[start]));
    const count = (start < 0 ? "at least " : "") + alerts.length + (alerts.length == 1 ? " alert" : " alerts");
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
          (alerts.length > 6 ? " and more" : "") +
          (missed
            ? "; " +
              missed +
              (caughtUp
                ? " came while the NVR was not listening, and were played to it once it listened again"
                : " did not reach the NVR yet; the hub keeps them until it listens")
            : nvrSet
              ? "; all reached the NVR"
              : "; with no NVR address set, the hub cannot tell whether they reached it")
        : "no alerts.")
    );
  }
  // What someone checking the hub wants at a glance. Each line says what is wrong, if anything.
  async function loadOverview() {
    loadArm();
    try {
      const o = await call("GET", "/api/overview");
      const who = (s) => (s.name ? s.name + " (" + s.sensor + ")" : s.sensor);
      const listOf = (items, line) =>
        el("span", null, ...(items.length ? items.flatMap((x, i) => (i ? ["; "] : []).concat(line(x))) : ["None"]));
      // Home Assistant's connection, as the hub words it: not set up, connected, connecting, or why it failed.
      const mqtt = o.mqtt || "",
        mqttChip =
          mqtt == "not set up"
            ? chip("Not set up", "none")
            : mqtt == "connected"
              ? chip("Connected", "ok")
              : mqtt == "connecting"
                ? chip("Connecting", "warn")
                : chip(mqtt.charAt(0).toUpperCase() + mqtt.slice(1), "bad");
      fill("ovNow", [
        ["Alerts", listOf(o.active, (a) => [sensorLink(a.sensor, who(a)), a.past ? " (a past event replaying)" : ""])],
        [
          "Sensor problems",
          listOf(o.problems, (p) => [
            sensorLink(p.sensor, who(p)),
            ": " + p.problem + (PROBLEM_HINT[p.problem] ? " (" + PROBLEM_HINT[p.problem] + ")" : ""),
          ]),
        ],
        [
          "Last alert",
          o.last
            ? who(o.last) +
              ", " +
              o.last.event +
              (o.last.received ? ", " + new Date(o.last.received).toLocaleString() : "")
            : "None in the history",
        ],
      ]);
      fill("ovInfo", [
        [
          "NVR",
          !o.nvr.set
            ? chip("No address set", "warn")
            : o.nvr.listening
              ? chip("Listening" + (o.nvr.streams ? ", watching the video" : ""), "ok")
              : chip("Not listening: alerts are not reaching it", "bad"),
        ],
        [
          "Notifications",
          !o.webhook.set
            ? chip("Not set up", "none")
            : o.webhook.last
              ? "Last message: " + o.webhook.last
              : "Nothing sent yet",
        ],
        ["Home Assistant", mqttChip],
      ]);
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
            i.ok ? "" : el("a", { href: "#" + i.view, textContent: "Fix" }),
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
      ? "Getting started: step " + (done + 1) + " of " + items.length
      : "Getting started";
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
        undo: before && before != mode ? () => setMode(before, 0, "Back to " + MODE_WORDS[before].toLowerCase()) : null,
      });
    } catch (e) {
      notice("Not changed: " + e.message, { err: true });
    }
    loadArm();
  }
  $("armAway").onclick = () => setMode("away");
  $("armHome").onclick = () => setMode("home");
  $("disarmBtn").onclick = async () => {
    const minutes = $("armFor").value,
      how = DISARM_TIMES.find(([m]) => m == minutes)[1],
      when = minutes == 0 ? how : "for " + how;
    if (
      await ask("Alerts will be logged but not sent to the NVR, so nothing is recorded.", {
        title: "Disarm the hub " + when + "?",
        ok: "Disarm",
      })
    )
      setMode("disarmed", minutes, "Disarmed " + when);
  };
  $("rearmDefault").onchange = async () => {
    try {
      await call("POST", "/api/arm/rearm", "minutes=" + $("rearmDefault").value);
      notice("Saved");
    } catch (e) {
      notice("Not saved: " + e.message, { err: true });
    }
    loadArm();
  };
  (window.viewHooks = window.viewHooks || []).push((view) => {
    if (view == "overview") {
      loadOverview();
      repeat(loadOverview, 3000);
    }
  });
  Object.assign(window.hub, { loadOverview });
})();
