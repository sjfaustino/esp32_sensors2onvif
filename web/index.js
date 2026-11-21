// The page's frame: the menu and which page shows, the status line at the top of every page with the setup steps it
// counts, and the helpers the other scripts share through window.hub. Each page's own script, loaded after this one,
// adds what it shows through window.viewHooks.
(() => {
  const firsts = [...document.querySelectorAll(".first")],
    items = [...document.querySelectorAll(".item")];
  function fill(id, rows) {
    const t = $(id);
    t.replaceChildren(
      ...rows.map(([k, v]) => {
        const r = t.insertRow(),
          a = r.insertCell(),
          b = r.insertCell();
        a.textContent = k;
        b.textContent = v;
        return r;
      }),
    );
  }
  // Sends a change to the hub and reports the outcome in m. True when the hub accepted it.
  async function send(m, url, body, done, method, failed) {
    m.className = "portMsg";
    try {
      const r = await fetch(url, { method: method || "POST", body });
      const j = await r.json();
      if (!r.ok) {
        m.className = "portMsg err";
        m.textContent = j.error;
        return false;
      }
      if (done) m.textContent = typeof done == "function" ? done(j) : done;
      return j || true;
    } catch (e) {
      m.className = "portMsg err";
      m.textContent = failed || "The hub did not answer. Try again.";
      return false;
    }
  }
  // The one timer a page refreshes itself with, stopped as another page shows: see repeat.
  let pageTimer = 0;
  function repeat(fn, ms) {
    clearInterval(pageTimer);
    pageTimer = setInterval(fn, ms);
  }
  const headRow = (names) =>
    el(
      "tr",
      null,
      ...names.map((h) => el("th", { textContent: h, style: "text-align:left;font-size:13px;padding-right:6px" })),
    );
  const sensorLink = (sensor, text) =>
    el("a", { href: "#sensor:" + sensor, textContent: text, onclick: (ev) => ev.stopPropagation() });
  // What must be right before the hub can be trusted, each with the page that puts it right and, for the steps shown
  // one at a time, what to do there.
  function readyItems(o) {
    const r = o.ready,
      test = r.testAgo_s;
    return [
      {
        ok: r.password,
        text: "A hub password of your own, not one built into the firmware",
        view: "http",
        go: "Choose a password",
        help: "The hub password guards this page, the NVR's login and the hub's setup network. Choose one of 8 to 63 characters that only you know; the NVR needs it too, so note it down.",
      },
      {
        ok: r.clock,
        text: "The hub's clock is set",
        view: "time",
        go: "Set the clock",
        help: "The hub needs the time to stamp each alert, keep quiet hours and send the daily check. With internet access it sets itself; otherwise give it a time server on your network, such as the router.",
      },
      {
        ok: o.nvr.set,
        text: "The NVR's address is set",
        view: "nvr",
        go: "Enter the NVR's address",
        help: "With the NVR's address, the hub can check that the NVR listens and that each alert was recorded, and play again any it missed.",
      },
      {
        ok: o.nvr.listening,
        text: "The NVR is listening",
        view: "nvr",
        go: "Open the NVR page",
        help: "On the NVR, add the hub as an ONVIF camera on a free channel: its address, port 8899, user admin and the hub password. Then turn on motion detection with recording for that channel. This step ticks itself once the NVR asks the hub for events.",
      },
      {
        ok: r.heard,
        text: "At least one sensor has reported",
        view: "espnow",
        go: "Pair a node",
        help: "Allow pairing, then start a sensor node in pairing mode and approve it once its fingerprint matches the one on its label. A device that calls a web address can be added there too.",
      },
      {
        ok: test != null && test < 30 * 86400,
        text:
          test == null
            ? "A test alert sent, and its recording found on the NVR"
            : "A test alert in the last month (the last was " + heardAgo(test) + ")",
        view: "overview",
        go: "Test everything",
        help: "Sends a test alert along the whole path and checks each step: the sensor, the hub, the NVR and your notifications. Then find its recording on the NVR, so you know an alert is kept. Do it once a month.",
        run: () => $("testAll").click(),
      },
      {
        ok: r.notify,
        text: "Notifications or Home Assistant set up, so problems reach you",
        view: "notify",
        go: "Set up notifications",
        help: "So an alert, a silent node or a flat battery reaches your phone: an ntfy or Gotify server on your network, Home Assistant, or Node-RED.",
      },
      {
        ok: r.daily,
        text: "A daily check, so you notice when the hub goes quiet",
        view: "notify",
        go: "Choose a time",
        help: "Once a day the hub sends one short message. If it stops arriving, the hub is off or cut off, and you know to look.",
      },
    ];
  }
  // One line at the top of every page: protecting, something needs attention, or not protecting, and why.
  function showStatus(o) {
    const bad = [],
      attention = [];
    if (o.nvr.set && !o.nvr.listening) bad.push("the NVR is not listening, so alerts are not recorded");
    if (o.mode == "disarmed") attention.push("disarmed");
    if (o.problems.length)
      attention.push(o.problems.length + (o.problems.length == 1 ? " sensor problem" : " sensor problems"));
    const todo = readyItems(o).filter((i) => !i.ok).length;
    if (todo) attention.push(todo + (todo == 1 ? " setup step" : " setup steps") + " left");
    const s = $("status");
    s.className = "status " + (bad.length ? "bad" : attention.length ? "attention" : "ok");
    s.textContent = bad.length
      ? "Not protecting: " + bad.concat(attention).join("; ")
      : attention.length
        ? "Needs attention: " + attention.join("; ")
        : "All good: armed " + o.mode + ", the NVR is listening, no problems.";
    s.hidden = false;
  }
  async function loadStatus() {
    try {
      showStatus(await call("GET", "/api/overview"));
    } catch (e) {}
  }
  // "5 min ago", from a time the hub gave, by this device's clock.
  function ago(iso) {
    const s = Math.max(0, Math.round((Date.now() - Date.parse(iso)) / 1000));
    return s < 10 ? "just now" : heardAgo(s);
  }
  // Signal strength as bars and a word, for people who do not read dBm. The node's light uses the same steps.
  function signal(rssi) {
    if (rssi == null) return "-";
    const [bars, word] =
      rssi >= -67 ? ["▂▄▆█", "strong"] : rssi >= -75 ? ["▂▄▆", "good"] : rssi >= -80 ? ["▂▄", "fair"] : ["▂", "weak"];
    return bars + " " + word + " (" + rssi + " dBm)";
  }
  function batteryLeft(days) {
    return days >= 60
      ? "about " + Math.round(days / 30) + " months left"
      : "about " + days + (days == 1 ? " day" : " days") + " left";
  }
  function heardAgo(s) {
    return s == null
      ? "Never"
      : s < 60
        ? s + " s ago"
        : s < 3600
          ? Math.floor(s / 60) + " min ago"
          : s < 86400
            ? Math.floor(s / 3600) + " h ago"
            : Math.floor(s / 86400) + " d ago";
  }
  function expand(f, on) {
    f.classList.toggle("open", on);
    f.nextElementSibling.classList.toggle("show", on);
  }
  // A hash such as "sensor:AA-BB-CC-DD-EE-01/1" opens a page for one thing; the part after the colon goes to its file.
  function show(hash) {
    const colon = hash.indexOf(":");
    let view = colon < 0 ? hash : hash.slice(0, colon),
      arg = "";
    try {
      arg = colon < 0 ? "" : decodeURIComponent(hash.slice(colon + 1));
    } catch (e) {
      view = "overview"; // a broken address, such as one cut short
    }
    if (!$("view-" + view)) view = "overview";
    document.querySelectorAll(".view").forEach((v) => (v.hidden = v.id != "view-" + view));
    items.forEach((i) => {
      const on = i.dataset.view == view;
      i.classList.toggle("active", on);
      if (on) {
        firsts.forEach((f) => expand(f, false));
        expand(i.parentElement.previousElementSibling, true);
      }
    });
    clearInterval(pageTimer);
    loadStatus();
    for (const hook of window.viewHooks || []) hook(view, arg); // each page's script loads what it shows
  }
  // For the pages' own scripts. Those loaded later add theirs: explain from events.js, loadOverview from overview.js.
  window.hub = {
    signal,
    batteryLeft,
    heardAgo,
    ago,
    headRow,
    fill,
    send,
    sensorLink,
    readyItems,
    showStatus,
    loadStatus,
    repeat,
  };
  firsts.forEach(
    (f) =>
      (f.onclick = () => {
        const on = !f.classList.contains("open");
        firsts.forEach((o) => expand(o, false));
        expand(f, on);
      }),
  );
  items.forEach(
    (i) =>
      (i.onclick = () => {
        location.hash = i.dataset.view;
      }),
  );
  addEventListener("hashchange", () => show(location.hash.slice(1)));
  addEventListener("DOMContentLoaded", () => show(location.hash.slice(1))); // once every page's script has run
  // A hub without a password asks for one on every page until it has one.
  $("saveNeedPw").onclick = async () => {
    const m = $("needPwMsg"),
      pw = $("needPw").value;
    if (pw != $("needPw2").value) {
      m.className = "portMsg err";
      m.textContent = "The two passwords differ.";
      return;
    }
    if (
      await send(
        m,
        "/api/settings",
        "password=" + encodeURIComponent(pw),
        "Saved. Log in with user admin and the new password.",
      )
    )
      setTimeout(() => location.reload(), 1500);
  };
  setInterval(loadStatus, 10000);
  fetch("/api/info")
    .then((r) => r.json())
    .then((i) => {
      $("openWarn").hidden = i.password;
      if (!i.password) $("needPw").focus();
      if (i.name) document.title = i.name.replace(/\.local$/, "") + ": alarm hub";
      $("armWarn").hidden = i.armed !== false;
    })
    .catch(() => {});
  fetch("/api/nvr")
    .then((r) => r.json())
    .then((n) => {
      $("nvrWarn").hidden = !!n.ip;
    })
    .catch(() => {});
})();
