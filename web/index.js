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
        if (v instanceof Node) b.append(v);
        else b.textContent = v;
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
      const form = m.closest && m.closest("[data-save]");
      if (form) markClean(form.dataset.save);
      return j || true;
    } catch (e) {
      m.className = "portMsg err";
      m.textContent = failed || "The hub did not answer. Try again.";
      return false;
    }
  }
  // A part of a settings page, marked data-save with its Save button's id, has changes not saved once someone edits it:
  // a bar at the foot of the page then offers to save or discard them, and leaving the page asks first. A successful
  // send from inside the part (see send) marks it saved.
  let dirty = null;
  function showUnsaved() {
    $("unsaved").hidden = !dirty;
    document.body.classList.toggle("dirty", !!dirty);
  }
  function markDirty(field) {
    const form = field.closest("[data-save]");
    if (!form) return;
    dirty = form.dataset.save;
    showUnsaved();
  }
  function markClean(key) {
    if (key && key != dirty) return;
    dirty = null;
    showUnsaved();
  }
  // The page's own scripts set values without these events, so any that comes is someone's edit.
  const edited = (e) => {
    if (e.target.closest && e.target.type != "file") markDirty(e.target);
  };
  document.addEventListener("input", edited);
  document.addEventListener("change", edited);
  // A loading table shows grey bars until its rows arrive; whatever fills it replaces them.
  function skeleton(t) {
    const cells = t.classList.contains("info") ? 2 : 1;
    for (let i = 0; i < 3; i++)
      t.append(el("tr", { className: "skel" }, ...Array.from({ length: cells }, () => el("td", null, el("span")))));
  }
  // A place with nothing in it yet: what is missing, what to do, and buttons that do it.
  function empty(target, title, text, ...actions) {
    target.replaceChildren(
      el(
        "div",
        { className: "empty" },
        el("b", { textContent: title }),
        text,
        actions.length ? el("div", { className: "acts" }, ...actions) : "",
      ),
    );
  }
  const size = (n) => (n < 1024 * 1024 ? Math.round(n / 1024) + " KB" : (n / 1024 / 1024).toFixed(1) + " MB");
  // A file input inside a drop zone takes a file dropped on it too, and shows the chosen file's name and size.
  for (const d of document.querySelectorAll(".drop")) {
    const input = d.querySelector("input"),
      name = d.querySelector(".dropFile");
    const chosen = () => {
      const f = input.files[0];
      d.classList.toggle("has", !!f);
      name.textContent = f ? f.name + ", " + size(f.size) : "";
    };
    input.addEventListener("change", chosen);
    d.addEventListener("dragover", (e) => {
      e.preventDefault();
      d.classList.add("over");
    });
    d.addEventListener("dragleave", () => d.classList.remove("over"));
    d.addEventListener("drop", (e) => {
      e.preventDefault();
      d.classList.remove("over");
      if (!e.dataTransfer.files.length) return;
      input.files = e.dataTransfer.files;
      chosen();
    });
  }
  // A label for a state at a glance. kind: ok, warn, bad or none (not set up).
  const chip = (text, kind) => el("span", { className: "chip " + kind, textContent: text });
  // The alarm's mode in the bar at the top of every page: away, home or disarmed.
  const MODE_WORDS = { away: "Armed away", home: "Armed home", disarmed: "Disarmed" };
  function showMode(mode) {
    const c = $("modeChip");
    if (!MODE_WORDS[mode]) return;
    c.className = "chip " + mode;
    c.textContent = MODE_WORDS[mode];
    c.hidden = false;
  }
  // The one timer a page refreshes itself with, stopped as another page shows: see repeat.
  let pageTimer = 0;
  function repeat(fn, ms) {
    clearInterval(pageTimer);
    pageTimer = setInterval(fn, ms);
  }
  const headRow = (names) => el("tr", null, ...names.map((h) => el("th", { textContent: h, className: "left" })));
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
    showMode(o.mode);
    const s = $("status");
    s.className = "status " + (bad.length ? "bad" : attention.length ? "attention" : "ok");
    s.href = bad.length ? "#nvr" : "#overview"; // where to put it right; on the Overview, see the click below
    s.dataset.steps = todo ? "1" : "";
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
    shown = view;
    document.querySelectorAll(".view").forEach((v) => (v.hidden = v.id != "view-" + view));
    $("view-" + view)
      .querySelectorAll("table.info:empty, table.list:empty")
      .forEach(skeleton);
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
    chip,
    showMode,
    MODE_WORDS,
    send,
    sensorLink,
    readyItems,
    showStatus,
    loadStatus,
    repeat,
    empty,
    size,
    markDirty,
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
  // Leaving a page with changes not saved asks first; staying puts the address back. Within one page nothing is asked.
  let shown = "";
  const viewOf = (hash) => hash.split(":")[0] || "overview";
  addEventListener("hashchange", async () => {
    const to = location.hash.slice(1);
    if (dirty && viewOf(to) != shown) {
      history.replaceState(null, "", "#" + shown); // fires no hashchange
      if (
        !(await ask("The changes on this page are not saved.", {
          title: "Leave without saving?",
          ok: "Leave",
          cancel: "Stay",
        }))
      )
        return;
      markClean();
      location.hash = to;
      return;
    }
    show(to);
  });
  addEventListener("beforeunload", (e) => {
    if (!dirty) return;
    e.preventDefault();
    e.returnValue = "";
  });
  $("unsavedSave").onclick = () => $(dirty).click();
  $("unsavedDiscard").onclick = () => {
    markClean();
    show(location.hash.slice(1)); // each page's script loads its values again
  };
  // On the Overview, the status line about setup steps leads to them.
  $("status").onclick = (e) => {
    if (shown != "overview" || !$("status").dataset.steps) return;
    e.preventDefault();
    const steps = $("wizard").hidden ? $("ovReadyBox") : $("wizard");
    steps.scrollIntoView({ behavior: "smooth", block: "center" });
  };
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
      if (i.name) {
        const name = i.name.replace(/\.local$/, "");
        document.title = name + ": alarm hub";
        $("hubTitle").textContent = name;
      }
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
