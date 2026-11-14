// The menu and every page but Sensors: network, ONVIF, HTTP, RTSP, ESP-NOW, NVR, live video, events, notifications,
// maintenance, time and device info.
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
      if (done) m.textContent = done;
      return true;
    } catch (e) {
      m.className = "portMsg err";
      m.textContent = failed || "The hub did not answer. Try again.";
      return false;
    }
  }
  const headRow = (names) =>
    el(
      "tr",
      null,
      ...names.map((h) => el("th", { textContent: h, style: "text-align:left;font-size:13px;padding-right:6px" })),
    );
  async function info(view) {
    try {
      const r = await fetch("/api/info");
      const i = await r.json();
      const at = i.webPort == 80 ? "" : ":" + i.webPort;
      $("onvifPort").value = i.onvifPort;
      $("webPort").value = i.webPort;
      $("rtspPort").value = i.rtspPort;
      if (view == "rtsp")
        fill("rtspInfo", [
          ["Video address", "rtsp://" + i.ip + ":" + i.rtspPort + "/stream"],
          ["Video streams open", i.streams],
        ]);
      $("hubName").value = i.name.replace(/\.local$/, "");
      $("hubHosts").value = i.hosts || "";
      $("protect").checked = i.password;
      $("pwRow").hidden = !i.password;
      $("webPw").placeholder = i.password ? "Leave empty to keep it" : "";
      if (view == "onvif")
        fill("onvifInfo", [
          ["Login", i.onvifLogin ? "Required: user admin and the hub password" : "None"],
          ["Device address", "http://" + i.ip + ":" + i.onvifPort + "/onvif/device_service"],
          ["Event subscribers", i.subscribers],
          ["Serial number", i.serial],
          ["Requests not supported", i.unsupported.join(", ") || "None"],
        ]);
      if (view == "http")
        fill("httpInfo", [
          ["Address", "http://" + i.ip + at + "/"],
          ["Name", "http://" + i.name + at + "/"],
          ["Sensor token", "add &token=" + i.token + " to /sensor requests"],
        ]);
    } catch (e) {}
  }
  // Asks once a second which picture is being sent, and fetches and decodes one only when it changes.
  let liveRun = 0,
    liveSeq = null,
    liveUrl = null;
  async function livePoll(run) {
    if (run != liveRun) return;
    let wait = 1000;
    const m = $("liveMsg");
    const get = (u) =>
      fetch(u, { cache: "no-store" }).catch(() => {
        throw new Error("Could not reach the hub. Trying again.");
      });
    try {
      const j = await (await get("/api/live")).json();
      if (run != liveRun) return;
      $("liveFile").textContent = j.file;
      if (j.seq !== liveSeq) {
        if (liveSeq === null) m.textContent = "Getting the decoder ready. The first time, this downloads about 30 MB.";
        const r = await get("/api/live/frame");
        if (!r.ok)
          throw new Error(
            r.status == 507 ? "The hub is short of memory. Trying again." : "The hub could not send the picture.",
          );
        const seq = Number(r.headers.get("X-Frame-Seq"));
        // The hub stops sending a picture that is replaced meanwhile, so the next one can be asked for.
        const data = new Uint8Array(
          await r.arrayBuffer().catch(() => {
            const e = new Error("The picture changed while it was being sent. Trying again.");
            e.soon = true;
            throw e;
          }),
        );
        let url;
        try {
          url = await decoded(data);
        } catch (e) {
          throw new Error(
            "Could not show the picture. The decoder needs internet access the first time. " + (e.message || ""),
          );
        }
        if (run != liveRun) {
          URL.revokeObjectURL(url);
          return;
        }
        if (liveUrl) URL.revokeObjectURL(liveUrl);
        liveUrl = url;
        const img = $("liveImg");
        img.src = url;
        img.style.display = "block";
        liveSeq = seq;
        m.className = "portMsg";
        m.textContent = "";
      }
    } catch (e) {
      wait = e.soon ? 500 : 5000;
      m.className = e.soon ? "portMsg" : "portMsg err";
      m.textContent = e.message || "Could not show the picture.";
    }
    setTimeout(() => livePoll(run), wait);
  }
  function setLive(on) {
    liveRun++;
    if (on) {
      liveSeq = null;
      livePoll(liveRun);
    }
  }
  function netState() {
    const off = $("netMode").value == "dhcp";
    ["netIp", "netMask", "netGw", "netDns"].forEach((i) => ($(i).disabled = off));
  }
  let sysTimer = 0;
  const ZONES = [
    -720, -660, -600, -540, -480, -420, -360, -300, -270, -240, -210, -180, -120, -60, 0, 60, 120, 180, 210, 240, 270,
    300, 330, 345, 360, 390, 420, 480, 540, 570, 600, 660, 720, 780,
  ];
  const two = (n) => String(n).padStart(2, "0");
  $("tmZone").replaceChildren(
    ...ZONES.map((z) =>
      el("option", {
        value: z,
        textContent: "UTC" + (z < 0 ? "-" : "+") + two(Math.floor(Math.abs(z) / 60)) + ":" + two(Math.abs(z) % 60),
      }),
    ),
  );
  function timeState() {
    const manual = $("tmMode").value == "manual";
    $("tmServer").disabled = manual;
    $("tmManual").disabled = !manual;
  }
  $("tmMode").onchange = timeState;
  async function tickTime() {
    try {
      const t = await (await fetch("/api/time")).json();
      $("tmNow").value = t.synced ? t.local : "Not set yet";
      return t;
    } catch (e) {
      return null;
    }
  }
  async function loadTime() {
    const t = await tickTime();
    if (!t) return;
    $("tmZone").value = t.offset;
    $("tmDst").value = t.dst;
    $("tmMode").value = t.ntp ? "ntp" : "manual";
    $("tmServer").value = t.server;
    timeState();
  }
  $("tmFromPc").onclick = () => {
    const d = new Date();
    $("tmMode").value = "manual";
    timeState();
    $("tmManual").value =
      d.getFullYear() +
      "-" +
      two(d.getMonth() + 1) +
      "-" +
      two(d.getDate()) +
      "T" +
      two(d.getHours()) +
      ":" +
      two(d.getMinutes()) +
      ":" +
      two(d.getSeconds());
    const m = $("tmMsg");
    m.className = "portMsg";
    m.textContent = "Read from this PC. Press Save to set the hub to this time.";
  };
  $("saveTime").onclick = async () => {
    const m = $("tmMsg"),
      manual = $("tmMode").value == "manual",
      v = $("tmManual").value;
    m.className = "portMsg";
    if (manual && !v) {
      m.className = "portMsg err";
      m.textContent = "Enter the date and time, or use Read from this PC.";
      return;
    }
    let body =
      "mode=" +
      (manual ? "manual" : "ntp") +
      "&offset=" +
      $("tmZone").value +
      "&dst=" +
      $("tmDst").value +
      "&server=" +
      encodeURIComponent($("tmServer").value);
    if (manual) body += "&epoch=" + Math.floor(new Date(v).getTime() / 1000);
    if (await send(m, "/api/time", body, "Saved.")) tickTime();
  };
  async function loadSchedule() {
    try {
      const s = await (await fetch("/api/schedule")).json();
      $("rbOn").checked = s.on;
      $("rbDay").value = s.day;
      $("rbTime").value = two(Math.floor(s.minute / 60)) + ":" + two(s.minute % 60);
    } catch (e) {}
  }
  $("saveRb").onclick = async () => {
    const m = $("rbMsg"),
      v = $("rbTime").value;
    m.className = "portMsg";
    if (!v) {
      m.className = "portMsg err";
      m.textContent = "Enter a time.";
      return;
    }
    const minute = Number(v.slice(0, 2)) * 60 + Number(v.slice(3, 5));
    send(
      m,
      "/api/schedule",
      "on=" + ($("rbOn").checked ? 1 : 0) + "&day=" + $("rbDay").value + "&minute=" + minute,
      "Saved.",
    );
  };
  async function sysAction(url, body, done, after) {
    if ((await send($("sysMsg"), url, body, done)) && after) setTimeout(after, 12000);
  }
  $("rebootNow").onclick = () => {
    if (confirm("Restart the hub now? The video stops for a few seconds."))
      sysAction("/api/reboot", "", "The hub is restarting.", () => location.reload());
  };
  $("resetAll").onclick = () => {
    if (
      confirm(
        "Erase the saved WiFi networks, ports, name, web password, fixed address and time settings? The hub will restart and may move to another address.",
      )
    )
      sysAction(
        "/api/reset",
        "confirm=yes",
        "Settings erased. The hub is restarting. If it cannot join a network, it opens its setup network.",
      );
  };
  async function loadDevice() {
    try {
      const j = await (await fetch("/api/device")).json();
      fill("devInfo", j.rows);
      if (!j.crash) {
        $("crashActs").replaceChildren();
        return;
      }
      if ($("crashActs").childElementCount) return; // refreshed every 5 s; leave the buttons alone
      $("crashActs").replaceChildren(
        el("a", {
          className: "btn",
          href: "/api/crash",
          download: "hub-crash.bin",
          textContent: "Download crash report",
        }),
        " ",
        el("button", {
          className: "del",
          textContent: "Delete crash report",
          onclick: () => {
            if (confirm("Delete the crash report?"))
              act(() => call("POST", "/api/crash/delete", "x=1"), "Deleted the crash report").then(loadDevice);
          },
        }),
        el("br"),
        "For diagnosis with the firmware it came from. It holds a copy of the hub's memory, which can include passwords.",
      );
    } catch (e) {}
  }
  function setSys(view) {
    clearInterval(sysTimer);
    if (view == "maintenance") loadSchedule();
    if (view == "events") {
      loadEvents();
      sysTimer = setInterval(loadEvents, 3000);
    }
    if (view == "time") {
      loadTime();
      sysTimer = setInterval(tickTime, 1000);
    }
    if (view == "device") {
      loadDevice();
      sysTimer = setInterval(loadDevice, 5000);
    }
    if (view == "notify") {
      loadHook(true);
      sysTimer = setInterval(() => loadHook(false), 3000);
    }
    if (view == "sensors") {
      loadArm();
      sysTimer = setInterval(loadArm, 5000);
    }
    if (view == "ha") {
      loadMqtt(true);
      sysTimer = setInterval(() => loadMqtt(false), 3000);
    }
  }
  // Armed or disarmed. Home Assistant can change it too, so the page asks again while it shows it.
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
      ...DISARM_TIMES.map(([m, t]) => el("option", { value: m, textContent: (id == "armFor" ? "for " : "") + t })),
    );
  let armWasArmed = null;
  function timeLeft(s) {
    const m = Math.ceil(s / 60);
    return m < 60 ? m + " min" : Math.floor(m / 60) + " h" + (m % 60 ? " " + (m % 60) + " min" : "");
  }
  async function loadArm() {
    try {
      const a = await call("GET", "/api/arm");
      $("armState").textContent = a.armed
        ? "armed"
        : "disarmed" +
          (!a.timed
            ? " until armed again"
            : a.left_s != null
              ? ", arms again in " + timeLeft(a.left_s)
              : ", arms again soon") +
          ": alerts are logged but not sent to the NVR";
      if (armWasArmed !== a.armed) $("armFor").value = a.rearm; // only when the state changes, so a choice being made stays
      armWasArmed = a.armed;
      if (document.activeElement != $("rearmDefault")) $("rearmDefault").value = a.rearm;
      $("armFor").hidden = !a.armed;
      $("armBtn").textContent = a.armed ? "Disarm" : "Arm";
      $("armBtn").hidden = false;
      $("armBtn").dataset.armed = a.armed ? "1" : "0";
      $("armWarn").hidden = a.armed;
    } catch (e) {}
  }
  $("armBtn").onclick = async () => {
    const arm = $("armBtn").dataset.armed != "1",
      minutes = $("armFor").value,
      how = DISARM_TIMES.find(([m]) => m == minutes)[1];
    if (
      !arm &&
      !confirm(
        "Disarm the hub " +
          (minutes == 0 ? how : "for " + how) +
          "? Alerts will be logged but not sent to the NVR, so nothing is recorded.",
      )
    )
      return;
    try {
      await call("POST", "/api/arm", "armed=" + (arm ? 1 : 0) + "&for=" + minutes);
    } catch (e) {
      alert(e.message);
    }
    loadArm();
  };
  $("rearmDefault").onchange = async () => {
    try {
      await call("POST", "/api/arm/rearm", "minutes=" + $("rearmDefault").value);
    } catch (e) {
      alert(e.message);
    }
    loadArm();
  };
  async function loadMqtt(all) {
    try {
      const m = await call("GET", "/api/mqtt");
      if (all) {
        $("mqHost").value = m.host;
        $("mqPort").value = m.port;
        $("mqUser").value = m.user;
        $("mqPw").value = "";
        $("mqPw").placeholder = m.password ? "saved" : "";
      }
      fill("mqInfo", [
        ["Connection", m.state + (m.since ? ", since " + new Date(m.since).toLocaleString() : "")],
        ["Events waiting to send", m.waiting],
        ["Events lost, too many waiting", m.lost],
        ["Topics", m.topic + "/…"],
        ["In Home Assistant", "the device " + m.hub + ", and one device per sensor"],
      ]);
    } catch (e) {}
  }
  $("saveMq").onclick = async () => {
    if (
      await send(
        $("mqMsg"),
        "/api/mqtt",
        "host=" +
          encodeURIComponent($("mqHost").value.trim()) +
          "&port=" +
          encodeURIComponent($("mqPort").value.trim()) +
          "&user=" +
          encodeURIComponent($("mqUser").value) +
          "&password=" +
          encodeURIComponent($("mqPw").value),
        "Saved. The connection shows below within a few seconds.",
      )
    )
      loadMqtt(true);
  };
  async function loadHook(all) {
    try {
      const h = await call("GET", "/api/webhook");
      if (all) {
        $("hookUrl").value = h.url;
        $("hookText").value = h.text ? "1" : "0";
        $("hookAlerts").checked = h.alerts;
        $("hookProblems").checked = h.problems;
      }
      fill("hookInfo", [
        [
          "Last message",
          h.last ? h.last + (h.lastAt ? ", " + new Date(h.lastAt).toLocaleString() : "") : "None sent yet",
        ],
        ["Waiting to send", h.waiting],
        ["Dropped, too many waiting", h.dropped],
      ]);
    } catch (e) {}
  }
  $("saveHook").onclick = async () => {
    if (
      await send(
        $("hookMsg"),
        "/api/webhook",
        "url=" +
          encodeURIComponent($("hookUrl").value.trim()) +
          "&text=" +
          $("hookText").value +
          "&alerts=" +
          ($("hookAlerts").checked ? 1 : 0) +
          "&problems=" +
          ($("hookProblems").checked ? 1 : 0),
        "Saved.",
      )
    )
      loadHook(false);
  };
  $("testHook").onclick = async () => {
    if (await send($("hookMsg"), "/api/webhook/test", "x=1", "Test sent. The result shows below within a few seconds."))
      setTimeout(() => loadHook(false), 3000);
  };
  // A full backup is a tar file made and read here, so the hub never holds more than one picture at a time.
  function tarHeader(name, size) {
    const h = new Uint8Array(512),
      put = (s, at) => {
        for (let i = 0; i < s.length; i++) h[at + i] = s.charCodeAt(i);
      };
    put(name, 0);
    put("0000644", 100);
    put("0000000", 108);
    put("0000000", 116);
    put(size.toString(8).padStart(11, "0"), 124);
    put(
      Math.floor(Date.now() / 1000)
        .toString(8)
        .padStart(11, "0"),
      136,
    );
    put("        ", 148);
    h[156] = 48;
    put("ustar", 257);
    put("00", 263);
    let sum = 0;
    for (const b of h) sum += b;
    put(sum.toString(8).padStart(6, "0"), 148);
    h[154] = 0;
    h[155] = 32;
    return h;
  }
  function tarBuild(files) {
    const parts = [];
    for (const [name, data] of files) {
      parts.push(tarHeader(name, data.length), data);
      const pad = (512 - (data.length % 512)) % 512;
      if (pad) parts.push(new Uint8Array(pad));
    }
    parts.push(new Uint8Array(1024));
    return new Blob(parts, { type: "application/x-tar" });
  }
  function tarRead(bytes) {
    let ended = false;
    const out = [],
      text = (a, b) => String.fromCharCode(...bytes.subarray(a, b)).replace(/\0[\s\S]*$/, "");
    for (let at = 0; at + 512 <= bytes.length; ) {
      if (bytes.subarray(at, at + 512).every((b) => b == 0)) {
        ended = true;
        break;
      }
      const name = text(at, at + 100),
        size = parseInt(text(at + 124, at + 136).trim(), 8);
      if (!name || isNaN(size) || at + 512 + size > bytes.length)
        throw new Error("This file is damaged, or is not a full backup from the hub.");
      const type = bytes[at + 156];
      if (type == 48 || type == 0) out.push([name, bytes.slice(at + 512, at + 512 + size)]);
      at += 512 + Math.ceil(size / 512) * 512;
    }
    if (!ended) throw new Error("This file is cut short, or is not a full backup from the hub.");
    return out;
  }
  // The hub answers 507 while its one spare buffer is in use, so those requests are tried again.
  async function fetchRetry(url, opts) {
    for (let i = 0; ; i++) {
      const r = await fetch(url, opts);
      if (r.status != 507 || i == 5) return r;
      await new Promise((z) => setTimeout(z, 1000));
    }
  }
  $("fbSave").onclick = async () => {
    const m = $("fbMsg");
    m.className = "portMsg";
    try {
      const files = [],
        add = async (path) => {
          const r = await fetchRetry("/api/file?path=" + path);
          if (r.status == 404) return;
          if (!r.ok) throw new Error("The hub could not send " + path);
          files.push([path, new Uint8Array(await r.arrayBuffer())]);
          m.textContent = "Collected " + files.length + " files";
        };
      const b = await fetch("/api/backup");
      if (!b.ok) throw new Error("The hub could not send its settings.");
      files.push(["hub-settings.txt", new Uint8Array(await b.arrayBuffer())]);
      const t = await call("GET", "/api/tree");
      if (t.clear != null) await add("clear.h264");
      if (t.clearPreview) await add("clear.jpg");
      await add("text.json");
      for (const s of t.nodes) {
        files.push([s.mac + "/.node", new Uint8Array(0)]);
        if (s.late || s.silent) await add(s.mac + "/settings.txt");
        if (s.paired) await add(s.mac + "/pairing.txt");
        for (const n of s.sensors) {
          const base = s.mac + "/" + n.input;
          files.push([base + "/.sensor", new Uint8Array(0)]);
          for (const f of n.files) {
            await add(base + "/" + f.name);
            if (f.preview) await add(base + "/" + f.name.replace(/\.h264$/, ".jpg"));
          }
          if (n.name) await add(base + "/name.txt");
          await add(base + "/text.json");
          if (n.hold || n.quiet) await add(base + "/settings.txt");
        }
      }
      const a = el("a", {
        href: URL.createObjectURL(tarBuild(files)),
        download: "hub-backup-" + new Date().toISOString().slice(0, 10) + ".tar",
      });
      document.body.append(a);
      a.click();
      a.remove();
      setTimeout(() => URL.revokeObjectURL(a.href), 10000);
      m.textContent =
        "Saved " + files.length + " files. The file holds the WiFi and hub passwords, so keep it somewhere safe.";
    } catch (e) {
      m.className = "portMsg err";
      m.textContent = e.message;
    }
  };
  $("fbLoad").onclick = async () => {
    const m = $("fbMsg"),
      f = $("fbFile").files[0];
    m.className = "portMsg";
    if (!f) {
      m.className = "portMsg err";
      m.textContent = "Choose a full backup .tar file first.";
      return;
    }
    if (
      !confirm(
        "Restore this backup? Its sensors, pictures and names are added, replacing any with the same name, then the settings are replaced and the hub restarts. It may move to another address.",
      )
    )
      return;
    try {
      const list = [];
      let settings = null;
      for (const [n, d] of tarRead(new Uint8Array(await f.arrayBuffer()))) {
        if (n == "hub-settings.txt") settings = d;
        else list.push([n, d]);
      }
      if (!settings) throw new Error("This is not a full backup from the hub.");
      // Folders first. A new picture removes its thumbnail, so thumbnails go after pictures.
      const rank = (p) => (/\/\.(node|sensor)$/.test(p) ? 0 : p.endsWith(".h264") ? 1 : p.endsWith(".jpg") ? 2 : 3);
      list.sort((a, b) => rank(a[0]) - rank(b[0]));
      const failed = [];
      for (let i = 0; i < list.length; i++) {
        const [path, data] = list[i],
          folder = /\/\.(node|sensor)$/.test(path);
        m.textContent = "Restoring " + (i + 1) + " of " + list.length + ": " + path;
        const r = await fetchRetry(
          folder ? "/api/mkdir?path=" + path.replace(/\/\.(node|sensor)$/, "") : "/api/file?path=" + path,
          { method: folder ? "POST" : "PUT", body: folder ? null : data },
        );
        if (!r.ok) {
          let j = {};
          try {
            j = await r.json();
          } catch (e) {}
          failed.push(path + ": " + (j.error || "HTTP " + r.status));
        }
      }
      m.textContent = "Restoring the settings";
      const r = await fetch("/api/restore", { method: "POST", body: settings });
      if (!r.ok)
        throw new Error("The sensors and pictures were restored, but the hub refused the settings in this backup.");
      m.textContent =
        "Restored " +
        (list.length - failed.length) +
        " of " +
        list.length +
        " items and the settings. The hub is restarting." +
        (failed.length ? " Not restored: " + failed.join("; ") : "");
      if (failed.length) m.className = "portMsg err";
      setTimeout(() => location.reload(), 15000);
    } catch (e) {
      m.className = "portMsg err";
      m.textContent = e.message;
    }
  };
  let espTimer = 0;
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
  async function loadEsp() {
    try {
      const j = await (await fetch("/api/espnow")).json();
      fill("espInfo", [
        ["Hub MAC address", j.mac],
        ["WiFi channel", j.channel],
        ["ESP-NOW key", j.key],
      ]);
      $("espUnsigned").checked = j.unsigned;
      const STATES = {
        active: "Active",
        idle: "Idle",
        silent: "Silent (no report for 3 hours)",
        never: "Not heard yet",
      };
      $("espNodes").replaceChildren(
        ...(j.nodes.length
          ? [
              headRow([
                "Name",
                "Node",
                "Sensor",
                "State",
                "Last heard",
                "Battery",
                "Signal",
                "Report no.",
                "Refused",
                "Key",
              ]),
            ]
          : []),
        ...j.nodes.map((n) =>
          el(
            "tr",
            null,
            ...[
              n.name || "-",
              n.node,
              n.input,
              STATES[n.state],
              heardAgo(n.ago),
              n.battery == null ? "-" : n.battery + "%" + (n.low ? " (low)" : ""),
              n.rssi == null ? "-" : n.rssi + " dBm" + (n.weak ? " (weak)" : ""),
              n.counter == null ? "-" : n.counter,
              n.refused ? n.refused + " (" + n.refusal + ")" : "-",
              n.paired ? "its own" : "shared",
            ].map((v) => el("td", { textContent: v })),
          ),
        ),
      );
      $("espEmpty").textContent = j.nodes.length ? "" : "No sensor nodes yet. Add one under Sensors Manager > Sensors.";
      $("espUnknown").replaceChildren(
        ...(j.unknown.length ? [headRow(["Node", "Sensor", "Last heard", "Signal", "Messages", ""])] : []),
        ...j.unknown.map((n) =>
          el(
            "tr",
            null,
            ...[n.mac, n.input, heardAgo(n.ago), n.rssi == null ? "-" : n.rssi + " dBm", n.messages].map((v) =>
              el("td", { textContent: v }),
            ),
            el(
              "td",
              null,
              el("button", {
                textContent: "Add node",
                onclick: async () => {
                  try {
                    await call("POST", "/api/mkdir?path=" + n.mac + "/" + n.input);
                    loadEsp();
                    load();
                  } catch (e) {
                    $("espNone").textContent = e.message;
                  }
                },
              }),
            ),
          ),
        ),
      );
      loadPairing();
      $("espNone").textContent = j.unknown.length
        ? "These are ignored until added under Sensors Manager > Sensors."
        : "None since the hub last started.";
    } catch (e) {}
  }
  // Nodes waiting for approval, each with its key's fingerprint to compare with the node's label.
  async function loadPairing() {
    try {
      const p = await call("GET", "/api/pairing");
      $("pairOpen").disabled = !p.ready;
      $("pairState").textContent = !p.ready
        ? "Not available on this hub."
        : (p.open ? "Open for " + p.open + " s more. " : "Closed. ") + "The hub's fingerprint is " + p.hub + ".";
      $("pairWaiting").replaceChildren(
        ...(p.waiting.length ? [headRow(["Node", "Sensors", "Fingerprint", "Last heard", "Signal", ""])] : []),
        ...p.waiting.map((n) =>
          el(
            "tr",
            null,
            ...[
              n.mac +
                (n.paired ? " (paired before, with another key)" : n.known ? " (added before, without pairing)" : ""),
              n.inputs,
              n.fingerprint,
              heardAgo(n.ago),
              n.rssi == null ? "-" : n.rssi + " dBm",
            ].map((v) => el("td", { textContent: v })),
            el(
              "td",
              null,
              el("button", {
                textContent: "Approve",
                onclick: async () => {
                  if (!confirm("Approve " + n.mac + "? Only if its label shows the fingerprint " + n.fingerprint + "."))
                    return;
                  try {
                    await call("POST", "/api/pairing/approve", "mac=" + n.mac);
                    $("pairMsg").textContent = "Paired " + n.mac + ". It starts reporting once it hears the hub again.";
                    loadEsp();
                    load();
                  } catch (e) {
                    $("pairMsg").textContent = e.message;
                  }
                },
              }),
              " ",
              el("button", {
                className: "del",
                textContent: "Reject",
                onclick: async () => {
                  try {
                    await call("POST", "/api/pairing/reject", "mac=" + n.mac);
                    loadPairing();
                  } catch (e) {
                    $("pairMsg").textContent = e.message;
                  }
                },
              }),
            ),
          ),
        ),
      );
    } catch (e) {}
  }
  $("pairOpen").onclick = async () => {
    try {
      await call("POST", "/api/pairing/open", "x=1");
      loadPairing();
    } catch (e) {
      $("pairMsg").textContent = e.message;
    }
  };
  function setEsp(on) {
    clearInterval(espTimer);
    if (on) {
      loadEsp();
      call("GET", "/api/alerts")
        .then((a) => ($("espAlarm").checked = a.alarm))
        .catch(() => {});
      espTimer = setInterval(loadEsp, 3000);
    }
  }
  $("espAlarm").onchange = (e) => {
    call("POST", "/api/alerts", "alarm=" + (e.target.checked ? 1 : 0)).catch(() => {});
  };
  $("espUnsigned").onchange = (e) => {
    call("POST", "/api/espnow", "unsigned=" + (e.target.checked ? 1 : 0)).catch(() => {});
  };
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
  async function loadEvents() {
    try {
      const list = await call("GET", "/api/events");
      $("evTable").replaceChildren(
        ...(list.length ? [headRow(["Time", "Sensor", "Event", "Details", "Battery"])] : []),
        ...list.map((e) => {
          const when = e.happened || e.received,
            what = e.event.charAt(0).toUpperCase() + e.event.slice(1); // the hub names every event
          const off = e.off_s != null ? "off for up to " + duration(e.off_s) : "";
          const details = [e.detail || "", off]
            .concat(
              e.targets.map((t) =>
                [t.distance_cm == null ? "" : (t.distance_cm / 100).toFixed(2) + " m", DIRECTIONS[t.direction] || ""]
                  .filter(Boolean)
                  .join(" "),
              ),
            )
            .filter(Boolean)
            .join(", ");
          return el(
            "tr",
            null,
            ...[
              when ? new Date(when).toLocaleString() : "Clock not set",
              e.sensor == "hub" ? "Hub" : e.name || e.sensor,
              what,
              details || "-",
              e.battery == null ? "-" : e.battery + "%",
            ].map((v) => el("td", { textContent: v })),
          );
        }),
      );
      $("evNone").textContent = list.length ? "" : "No events yet.";
    } catch (e) {}
  }
  $("fwSend").onclick = async () => {
    const m = $("fwMsg"),
      f = $("fwFile").files[0];
    m.className = "portMsg";
    if (!f) {
      m.className = "portMsg err";
      m.textContent = "Choose a firmware .bin file first.";
      return;
    }
    if (!confirm("Update the firmware? The hub restarts when it finishes, and the video stops until then.")) return;
    m.textContent = "Uploading. Keep this page open.";
    if (
      await send(
        m,
        "/api/firmware",
        f,
        "Done. The hub is restarting.",
        "PUT",
        "The upload failed. The hub keeps its current firmware.",
      )
    )
      setTimeout(() => location.reload(), 25000);
  };
  $("rsSend").onclick = async () => {
    const m = $("rsMsg"),
      f = $("rsFile").files[0];
    m.className = "portMsg";
    if (!f) {
      m.className = "portMsg err";
      m.textContent = "Choose a backup file first.";
      return;
    }
    if (!confirm("Replace the hub's settings with this backup? It will restart and may move to another address."))
      return;
    send(m, "/api/restore", await f.text(), "Restored. The hub is restarting.");
  };
  async function loadNvr() {
    try {
      const n = await (await fetch("/api/nvr")).json();
      $("nvrIp").value = n.ip;
      $("nvrWarn").hidden = !!n.ip;
      const state = (on) => (!n.ip ? "Enter the NVR's IP address" : on ? "Connected" : "Not connected");
      fill("nvrInfo", [
        ["Video", state(n.video)],
        ["Motion events", state(n.events)],
        ["Viewers connected now", n.viewers.join(", ") || "None"],
      ]);
    } catch (e) {}
  }
  $("saveNvr").onclick = async () => {
    if (await send($("nvrMsg"), "/api/nvr", "ip=" + encodeURIComponent($("nvrIp").value), "Saved.")) loadNvr();
  };
  async function loadNet() {
    try {
      const n = await (await fetch("/api/network")).json();
      $("netType").value = n.type;
      $("netMode").value = n.dhcp ? "dhcp" : "static";
      $("netIp").value = n.ip;
      $("netMask").value = n.mask;
      $("netGw").value = n.gateway;
      $("netDns").value = n.dns;
      $("netMac").value = n.mac;
      netState();
      $("netMsg").className = "portMsg";
      $("netMsg").textContent = n.dhcp
        ? ""
        : "This fixed address is used only on " + n.staticFor + ". Other WiFi networks use DHCP.";
    } catch (e) {}
  }
  $("netMode").onchange = netState;
  $("saveNet").onclick = async () => {
    const m = $("netMsg"),
      mode = $("netMode").value,
      ip = $("netIp").value;
    if (
      !confirm(
        "The hub will restart to apply this. A fixed address is used only on the WiFi network it is connected to now. Continue?",
      )
    )
      return;
    if (
      !(await send(
        m,
        "/api/network",
        "mode=" +
          mode +
          "&ip=" +
          encodeURIComponent(ip) +
          "&mask=" +
          encodeURIComponent($("netMask").value) +
          "&gateway=" +
          encodeURIComponent($("netGw").value) +
          "&dns=" +
          encodeURIComponent($("netDns").value),
      ))
    )
      return;
    const named = location.hostname.endsWith(".local");
    m.textContent =
      mode == "static" || named
        ? "Saved. The hub is restarting."
        : "Saved. The hub is restarting and will get its address from the router. Open it by name, or find the new address in the router.";
    if (mode == "static" || named)
      setTimeout(() => {
        location.href =
          location.protocol +
          "//" +
          (named ? location.hostname : ip) +
          (location.port ? ":" + location.port : "") +
          "/" +
          location.hash;
      }, 12000);
  };
  async function savePort(field, input, msg, warning) {
    const v = $(input).value,
      m = $(msg);
    if (!confirm(warning)) return;
    if (await send(m, "/api/settings", field + "=" + encodeURIComponent(v), "Saved. The hub is restarting."))
      setTimeout(() => location.reload(), 10000);
  }
  $("protect").onchange = (e) => {
    $("pwRow").hidden = !e.target.checked;
  };
  $("saveRtsp").onclick = () =>
    savePort(
      "rtsp",
      "rtspPort",
      "rtspMsg",
      "The hub will restart and the video will stop briefly. A viewer such as VLC will need the new address. Continue?",
    );
  $("saveOnvif").onclick = () =>
    savePort(
      "onvif",
      "onvifPort",
      "onvifMsg",
      "The hub will restart, and the NVR will lose this camera until its channel is set to the new port. Continue?",
    );
  $("saveWeb").onclick = async () => {
    const m = $("webMsg"),
      port = $("webPort").value,
      name = $("hubName").value,
      on = $("protect").checked;
    if (!confirm("The hub will restart to apply these settings. Continue?")) return;
    if (
      !(await send(
        m,
        "/api/settings",
        "web=" +
          encodeURIComponent(port) +
          "&name=" +
          encodeURIComponent(name) +
          "&protect=" +
          (on ? "on" : "off") +
          "&password=" +
          encodeURIComponent($("webPw").value) +
          "&hosts=" +
          encodeURIComponent($("hubHosts").value),
        "Saved. The hub is restarting.",
      ))
    )
      return;
    const host = location.hostname.endsWith(".local") ? name + ".local" : location.hostname;
    setTimeout(() => {
      location.href = location.protocol + "//" + host + ":" + port + "/" + location.hash;
    }, 10000);
  };
  function expand(f, on) {
    f.classList.toggle("open", on);
    f.nextElementSibling.classList.toggle("show", on);
  }
  function show(view) {
    if (!$("view-" + view)) view = "sensors";
    document.querySelectorAll(".view").forEach((v) => (v.hidden = v.id != "view-" + view));
    items.forEach((i) => {
      const on = i.dataset.view == view;
      i.classList.toggle("active", on);
      if (on) {
        firsts.forEach((f) => expand(f, false));
        expand(i.parentElement.previousElementSibling, true);
      }
    });
    if (view == "wifi") {
      const n = $("netFrame");
      if (!n.getAttribute("src")) n.src = "/setup";
    }
    if (view == "network") loadNet();
    if (view == "nvr") loadNvr();
    setLive(view == "live");
    setEsp(view == "espnow");
    setSys(view);
    if (view != "sensors") info(view);
  }
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
  show(location.hash.slice(1));
  fetch("/api/info")
    .then((r) => r.json())
    .then((i) => {
      $("openWarn").hidden = i.password;
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
