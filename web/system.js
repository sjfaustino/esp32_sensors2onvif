// The system pages: Time, Maintenance (the restart schedule, restarts, new keys, a reset, firmware, backups) and
// Device Info.
(() => {
  const { fill, send, repeat } = window.hub;
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
  $("newKeys").onclick = () => {
    if (
      confirm(
        "Make new keys? Every sensor node stops being heard until it is set up or paired again, and HTTP sensors need the new token. The hub restarts.",
      )
    )
      sysAction("/api/newkeys", "confirm=yes", "New keys are being made. The hub is restarting.", () =>
        location.reload(),
      );
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
      await add("plan.jpg"); // the floor plan, and where each sensor is on it
      await add("plan.json");
      for (const s of t.nodes) {
        files.push([s.mac + "/.node", new Uint8Array(0)]);
        if (s.late || s.silent || s.reportEvery || s.repeat) await add(s.mac + "/settings.txt"); // any of its settings
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
          if (n.hold || n.quiet || n.interior) await add(base + "/settings.txt");
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
  (window.viewHooks = window.viewHooks || []).push((view) => {
    if (view == "maintenance") loadSchedule();
    if (view == "time") {
      loadTime();
      repeat(tickTime, 1000);
    }
    if (view == "device") {
      loadDevice();
      repeat(loadDevice, 5000);
    }
  });
})();
