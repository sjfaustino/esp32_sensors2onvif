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
      $("tmNow").value = t.synced ? t.local : tr("Not set yet");
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
    m.textContent = tr("Read from this PC. Press Save to set the hub to this time.");
    window.hub.markDirty($("saveTime"));
  };
  $("saveTime").onclick = async () => {
    const m = $("tmMsg"),
      manual = $("tmMode").value == "manual",
      v = $("tmManual").value;
    m.className = "portMsg";
    if (manual && !v) {
      m.className = "portMsg err";
      m.textContent = tr("Enter the date and time, or use Read from this PC.");
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
    if (await send(m, "/api/time", body, tr("Saved."))) tickTime();
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
      m.textContent = tr("Enter a time.");
      return;
    }
    const minute = Number(v.slice(0, 2)) * 60 + Number(v.slice(3, 5));
    send(
      m,
      "/api/schedule",
      "on=" + ($("rbOn").checked ? 1 : 0) + "&day=" + $("rbDay").value + "&minute=" + minute,
      tr("Saved."),
    );
  };
  async function sysAction(url, body, done, after) {
    if ((await send($("sysMsg"), url, body, done)) && after) setTimeout(after, 12000);
  }
  $("rebootNow").onclick = async () => {
    if (await ask(tr("The video stops for a few seconds."), { title: tr("Restart the hub now?"), ok: tr("Restart") }))
      sysAction("/api/reboot", "", tr("The hub is restarting."), () => location.reload());
  };
  $("newKeys").onclick = async () => {
    if (
      await ask(
        tr(
          "Every sensor node stops being heard until it is set up or paired again, and HTTP sensors need the new token. The hub restarts.",
        ),
        { title: tr("Make new keys?"), ok: tr("Make new keys"), danger: true },
      )
    )
      sysAction("/api/newkeys", "confirm=yes", tr("New keys are being made. The hub is restarting."), () =>
        location.reload(),
      );
  };
  $("resetAll").onclick = async () => {
    if (
      await ask(
        tr(
          "The saved Wi-Fi networks, ports, name, web password, fixed address and time settings are erased. The hub restarts and may move to another address.",
        ),
        { title: tr("Reset the settings?"), ok: tr("Reset settings"), danger: true },
      )
    )
      sysAction(
        "/api/reset",
        "confirm=yes",
        tr("Settings erased. The hub is restarting. If it cannot join a network, it opens its setup network."),
      );
  };
  // The last week of the hub's health, an hour to a bar: [[start, lowKb, rssi, upMinutes, nvrMinutes], ...].
  let historyShown = "";
  function showHistory(hours) {
    const key = JSON.stringify(hours);
    if (key == historyShown) return; // refreshed every 5 s; drawn again only when it changed
    historyShown = key;
    const box = $("devHistory");
    if (!hours.length) {
      box.replaceChildren(
        el("p", { className: "note", textContent: tr("Nothing yet: the hub keeps it once its clock is set.") }),
      );
      return;
    }
    const last = hours[hours.length - 1][0],
      first = last - 167 * 3600;
    const ns = "http://www.w3.org/2000/svg",
      W = 320,
      H = 70,
      L = 34;
    const day = (t) => new Date(t * 1000).toLocaleDateString([], { weekday: "short" });
    // One chart: each hour's bar from the bottom to its value, scaled between lo and hi; marks are the grid's values,
    // and warn, when given, a value below which a bar is drawn in the warning colour.
    const chart = (title, value, lo, hi, marks, label, warn) => {
      const svg = document.createElementNS(ns, "svg");
      svg.setAttribute("viewBox", "0 0 " + W + " " + H);
      svg.setAttribute("class", "chart");
      svg.setAttribute("role", "img");
      svg.setAttribute("aria-label", title);
      const add = (tag, attrs, text) => {
        const e = document.createElementNS(ns, tag);
        for (const k in attrs) e.setAttribute(k, attrs[k]);
        if (text != null) e.textContent = text;
        svg.append(e);
        return e;
      };
      const y = (v) => 6 + (1 - (Math.min(hi, Math.max(lo, v)) - lo) / (hi - lo)) * (H - 20),
        w = (W - L - 4) / 168;
      for (const m of marks) {
        add("line", { x1: L, x2: W - 4, y1: y(m), y2: y(m), class: "grid" });
        add("text", { x: L - 3, y: y(m) + 3, "text-anchor": "end" }, label(m));
      }
      for (const h of hours) {
        const v = value(h);
        if (v == null || h[0] < first) continue;
        const x = L + ((h[0] - first) / 3600) * w;
        const bar = add("rect", {
          x: x.toFixed(1),
          y: y(v).toFixed(1),
          width: Math.max(1, w - 0.4).toFixed(2),
          height: Math.max(0.5, y(lo) - y(v)).toFixed(1),
          class: warn != null && v < warn ? "bar warn" : "bar",
        });
        const tip = document.createElementNS(ns, "title");
        bar.append(tip);
        tip.textContent =
          new Date(h[0] * 1000).toLocaleString([], { weekday: "short", hour: "2-digit", minute: "2-digit" }) +
          ": " +
          label(v);
      }
      for (let t = Math.ceil(first / 86400) * 86400; t <= last; t += 86400)
        add("text", { x: (L + ((t - first) / 3600) * w).toFixed(1), y: H - 2 }, day(t));
      return el("figure", { className: "healthChart" }, el("figcaption", { textContent: title }), svg);
    };
    const top = Math.max(100, ...hours.map((h) => h[1]));
    box.replaceChildren(
      chart(
        tr("Least free memory"),
        (h) => h[1],
        0,
        Math.ceil(top / 50) * 50,
        [0, Math.ceil(top / 100) * 50],
        (v) => v + " KB",
        40,
      ),
      chart(
        tr("Weakest Wi-Fi signal"),
        (h) => (h[2] < 0 ? h[2] : null),
        -100,
        -30,
        [-90, -70, -50],
        (v) => v + " dBm",
        -80,
      ),
      chart(
        tr("NVR listening"),
        (h) => (h[3] ? Math.round((100 * h[4]) / h[3]) : null),
        0,
        100,
        [0, 50, 100],
        (v) => v + "%",
        100,
      ),
    );
  }
  async function loadDevice() {
    try {
      const j = await (await fetch("/api/device")).json();
      showHistory(j.history || []);
      fill(
        "devInfo",
        j.rows.map(([k, v]) => [hubText(k), hubText(v)]),
      );
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
          textContent: tr("Download crash report"),
        }),
        " ",
        el("button", {
          className: "del",
          textContent: tr("Delete crash report"),
          onclick: async () => {
            if (
              await ask(tr("This cannot be undone."), {
                title: tr("Delete the crash report?"),
                ok: tr("Delete"),
                danger: true,
              })
            )
              act(() => call("POST", "/api/crash/delete", "x=1"), tr("Deleted the crash report")).then(loadDevice);
          },
        }),
        el("br"),
        tr(
          "For diagnosis with the firmware it came from. It holds a copy of the hub's memory, which can include passwords.",
        ),
      );
    } catch (e) {}
  }
  // The hub's label: filled from what the hub says of itself, then printed alone (see .hubLabel in index.css).
  $("labelPrint").onclick = async () => {
    try {
      const [i, p] = await Promise.all([call("GET", "/api/info"), call("GET", "/api/pairing").catch(() => ({}))]);
      const at = i.webPort == 80 ? "" : ":" + i.webPort;
      $("lbName").textContent = i.name.replace(/\.local$/, "");
      $("lbPage").textContent = tr("Page: {address} or {name}", {
        address: "http://" + i.ip + at + "/",
        name: "http://" + i.name + at + "/",
      });
      $("lbSetup").textContent = tr(
        "Setup network {network}: opens when the hub cannot join its Wi-Fi, with the hub password; on a hub with no settings, press its BOOT button first.",
        { network: i.setupName },
      );
      $("lbFp").textContent = p.hub ? tr("Fingerprint for pairing nodes: {fingerprint}", { fingerprint: p.hub }) : "";
      $("lbSerial").textContent = tr("Serial number {serial}", { serial: i.serial });
      // A phone scans the address, as some do not find a name ending in .local.
      $("lbQr").replaceChildren(qrCode.svg("http://" + i.ip + at + "/") || "");
      $("hubLabel").hidden = false;
      print();
    } catch (e) {
      notice(e.message, { err: true });
    }
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
        throw new Error(tr("This file is damaged, or is not a full backup from the hub."));
      const type = bytes[at + 156];
      if (type == 48 || type == 0) out.push([name, bytes.slice(at + 512, at + 512 + size)]);
      at += 512 + Math.ceil(size / 512) * 512;
    }
    if (!ended) throw new Error(tr("This file is cut short, or is not a full backup from the hub."));
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
  // A backup is sealed with a key worked out here from the passphrase at the top of the card, so the file holds no
  // readable password or key, and the slow part runs in this browser, never on the hub, whose loop must keep taking
  // alerts. Downloading asks for the passphrase twice, as a mistyped one would leave a backup nobody can open.
  function passphrase(m, twice) {
    const p = $("bkPass").value;
    if (p.length < 8 || (twice && p != $("bkPass2").value)) {
      m.className = "portMsg err";
      m.textContent =
        p.length < 8
          ? tr("Type a passphrase of 8 characters or more, at the top of Backup and restore.")
          : tr("The two passphrases differ.");
      return null;
    }
    return p;
  }
  // PBKDF2-HMAC-SHA256, written out because a page served over plain HTTP gets none of the browser's own crypto.
  const ROUNDS = 600000; // as OWASP advises; the hub's cam_backup.h
  const K = new Uint32Array([
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98,
    0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
    0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8,
    0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819,
    0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
    0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7,
    0xc67178f2,
  ]);
  const IV = new Uint32Array([
    0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19,
  ]);
  // One block: w holds its 16 words, and h the state, which this updates.
  function compress(h, w) {
    for (let i = 16; i < 64; i++) {
      const x = w[i - 15],
        y = w[i - 2];
      const s0 = ((x >>> 7) | (x << 25)) ^ ((x >>> 18) | (x << 14)) ^ (x >>> 3),
        s1 = ((y >>> 17) | (y << 15)) ^ ((y >>> 19) | (y << 13)) ^ (y >>> 10);
      w[i] = (w[i - 16] + s0 + w[i - 7] + s1) | 0;
    }
    let a = h[0],
      b = h[1],
      c = h[2],
      d = h[3],
      e = h[4],
      f = h[5],
      g = h[6],
      k = h[7];
    for (let i = 0; i < 64; i++) {
      const t1 =
        (k +
          (((e >>> 6) | (e << 26)) ^ ((e >>> 11) | (e << 21)) ^ ((e >>> 25) | (e << 7))) +
          ((e & f) ^ (~e & g)) +
          K[i] +
          w[i]) |
        0;
      const t2 =
        ((((a >>> 2) | (a << 30)) ^ ((a >>> 13) | (a << 19)) ^ ((a >>> 22) | (a << 10))) +
          ((a & b) ^ (a & c) ^ (b & c))) |
        0;
      k = g;
      g = f;
      f = e;
      e = (d + t1) | 0;
      d = c;
      c = b;
      b = a;
      a = (t1 + t2) | 0;
    }
    h[0] = (h[0] + a) | 0;
    h[1] = (h[1] + b) | 0;
    h[2] = (h[2] + c) | 0;
    h[3] = (h[3] + d) | 0;
    h[4] = (h[4] + e) | 0;
    h[5] = (h[5] + f) | 0;
    h[6] = (h[6] + g) | 0;
    h[7] = (h[7] + k) | 0;
  }
  // SHA-256 of data, going on from state after before bytes, a whole number of blocks.
  function sha(state, data, before) {
    const h = Uint32Array.from(state),
      w = new Uint32Array(64),
      n = Math.ceil((data.length + 9) / 64) * 64,
      m = new Uint8Array(n),
      bits = (before + data.length) * 8;
    m.set(data);
    m[data.length] = 0x80;
    m[n - 4] = bits >>> 24;
    m[n - 3] = (bits >>> 16) & 255;
    m[n - 2] = (bits >>> 8) & 255;
    m[n - 1] = bits & 255;
    for (let o = 0; o < n; o += 64) {
      for (let i = 0; i < 16; i++)
        w[i] = (m[o + 4 * i] << 24) | (m[o + 4 * i + 1] << 16) | (m[o + 4 * i + 2] << 8) | m[o + 4 * i + 3];
      compress(h, w);
    }
    return h;
  }
  const bytesOf = (h) =>
    new Uint8Array(Array.from(h).flatMap((v) => [v >>> 24, (v >>> 16) & 255, (v >>> 8) & 255, v & 255]));
  const hex = (b) => Array.from(b, (v) => v.toString(16).padStart(2, "0")).join("");
  // The 32-byte key, with progress(fraction) now and then, giving the page time to show it.
  async function backupKey(pass, salt, rounds, progress) {
    let key = new TextEncoder().encode(pass);
    if (key.length > 64) key = bytesOf(sha(IV, key, 0));
    const padded = (x) => {
      const b = new Uint8Array(64).fill(x);
      key.forEach((v, i) => (b[i] ^= v));
      return b;
    };
    const inner = blockState(padded(0x36)), // the states after the key's block, so each round hashes just its 32 bytes
      outer = blockState(padded(0x5c));
    const first = new Uint8Array(salt.length + 4);
    first.set(salt);
    first[salt.length + 3] = 1;
    let u = sha(outer, bytesOf(sha(inner, first, 64)), 64);
    const t = Uint32Array.from(u),
      w = new Uint32Array(64),
      hi = new Uint32Array(8),
      ho = new Uint32Array(8);
    for (let r = 1; r < rounds; r++) {
      hi.set(inner);
      for (let i = 0; i < 8; i++) w[i] = u[i];
      w.fill(0, 8, 16);
      w[8] = 0x80000000;
      w[15] = 768; // the 64-byte key block and these 32 bytes, in bits
      compress(hi, w);
      ho.set(outer);
      for (let i = 0; i < 8; i++) w[i] = hi[i];
      w.fill(0, 8, 16);
      w[8] = 0x80000000;
      w[15] = 768;
      compress(ho, w);
      u = ho;
      for (let i = 0; i < 8; i++) t[i] ^= ho[i];
      if (r % 25000 == 0) {
        if (progress) progress(r / rounds);
        await new Promise((z) => setTimeout(z));
      }
    }
    return bytesOf(t);
  }
  function blockState(block) {
    const h = Uint32Array.from(IV),
      w = new Uint32Array(64);
    for (let i = 0; i < 16; i++)
      w[i] = (block[4 * i] << 24) | (block[4 * i + 1] << 16) | (block[4 * i + 2] << 8) | block[4 * i + 3];
    compress(h, w);
    return h;
  }
  window.backupKey = backupKey; // for the page tests, which check it against known values
  const sealing = (m) => (f) => (m.textContent = tr("Sealing with your passphrase: {n}%", { n: Math.round(f * 100) }));
  async function sealedSettings(p, m) {
    const salt = crypto.getRandomValues(new Uint8Array(16)),
      key = await backupKey(p, salt, ROUNDS, sealing(m));
    const r = await fetch("/api/backup", {
      method: "POST",
      body: "key=" + hex(key) + "&salt=" + hex(salt) + "&rounds=" + ROUNDS,
    });
    if (!r.ok) throw new Error(hubSaid(await r.json().catch(() => ({}))) || tr("The hub could not send its settings."));
    return new Uint8Array(await r.arrayBuffer());
  }
  // A sealed backup names its rounds and salt on its first line; the key from them goes with it, a plain one alone.
  async function restoreSettings(body, m) {
    const text = typeof body == "string" ? body : new TextDecoder().decode(body.slice(0, 200)),
      head = text.match(/^sensorcam-backup sealed 2 pbkdf2-sha256 (\d+) ([0-9a-f]{32})\n/),
      headers = {};
    if (isSealed(text)) {
      if (!head || +head[1] > 10000000) throw new Error(tr("This backup is damaged, or was made by a newer hub."));
      const salt = new Uint8Array(head[2].match(/../g).map((x) => parseInt(x, 16)));
      headers["X-Backup-Key"] = hex(await backupKey($("bkPass").value, salt, +head[1], sealing(m)));
    }
    return fetch("/api/restore", { method: "POST", body, headers });
  }
  const isSealed = (text) => text.startsWith("sensorcam-backup sealed ");
  function saveFile(data, name) {
    const a = el("a", { href: URL.createObjectURL(new Blob([data])), download: name });
    document.body.append(a);
    a.click();
    a.remove();
    setTimeout(() => URL.revokeObjectURL(a.href), 10000);
  }
  $("bkSave").onclick = async () => {
    const m = $("rsMsg");
    m.className = "portMsg";
    const p = passphrase(m, true);
    if (!p) return;
    try {
      saveFile(await sealedSettings(p, m), "hub-settings.txt");
      m.textContent = tr("Saved, sealed with your passphrase. Keep the passphrase apart from the file.");
    } catch (e) {
      m.className = "portMsg err";
      m.textContent = e.message;
    }
  };
  $("fbSave").onclick = async () => {
    const m = $("fbMsg");
    m.className = "portMsg";
    const p = passphrase(m, true);
    if (!p) return;
    try {
      const files = [],
        add = async (path) => {
          const r = await fetchRetry("/api/file?path=" + path);
          if (r.status == 404) return;
          if (!r.ok) throw new Error(tr("The hub could not send {file}", { file: path }));
          files.push([path, new Uint8Array(await r.arrayBuffer())]);
          m.textContent = tr("Collected {n} files", { n: files.length });
        };
      files.push(["hub-settings.txt", await sealedSettings(p, m)]); // with each paired node's key, so pairing.txt stays out
      const t = await call("GET", "/api/tree");
      if (t.clear != null) await add("clear.h264");
      if (t.clearPreview) await add("clear.jpg");
      await add("text.json");
      await add("plan.jpg"); // the floor plan, and where each sensor is on it
      await add("plan.json");
      for (const s of t.nodes) {
        files.push([s.mac + "/.node", new Uint8Array(0)]);
        await add(s.mac + "/node.txt"); // its settings and hardware, and what it said; there is none until something is
        await add(s.mac + "/battery.txt"); // its battery readings, for the forecast
        await add(s.mac + "/checked.txt"); // when its first hour's checks passed
        for (const n of s.sensors) {
          const base = s.mac + "/" + n.input;
          files.push([base + "/.sensor", new Uint8Array(0)]);
          for (const f of n.files) {
            await add(base + "/" + f.name);
            if (f.preview) await add(base + "/" + f.name.replace(/\.h264$/, ".jpg"));
          }
          await add(base + "/text.json");
          await add(base + "/sensor.txt"); // its name and settings, if it has any
        }
      }
      saveFile(tarBuild(files), "hub-backup-" + new Date().toISOString().slice(0, 10) + ".tar");
      m.textContent = tr("Saved {n} files, the settings and node keys sealed with your passphrase.", {
        n: files.length,
      });
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
      m.textContent = tr("Choose a full backup .tar file first.");
      return;
    }
    if (
      !(await ask(
        tr(
          "Its sensors, pictures and names are added, replacing any with the same name, then the settings are replaced and the hub restarts. It may move to another address.",
        ),
        { title: tr("Restore this backup?"), ok: tr("Restore") },
      ))
    )
      return;
    try {
      const list = [];
      let settings = null;
      for (const [n, d] of tarRead(new Uint8Array(await f.arrayBuffer()))) {
        if (n == "hub-settings.txt") settings = d;
        else list.push([n, d]);
      }
      if (!settings) throw new Error(tr("This is not a full backup from the hub."));
      if (isSealed(new TextDecoder().decode(settings.slice(0, 40))) && !passphrase(m, false)) return;
      // Folders first. A new picture removes its thumbnail, so thumbnails go after pictures.
      const rank = (p) => (/\/\.(node|sensor)$/.test(p) ? 0 : p.endsWith(".h264") ? 1 : p.endsWith(".jpg") ? 2 : 3);
      list.sort((a, b) => rank(a[0]) - rank(b[0]));
      const failed = [];
      for (let i = 0; i < list.length; i++) {
        const [path, data] = list[i],
          folder = /\/\.(node|sensor)$/.test(path);
        m.textContent = tr("Restoring {n} of {total}: {file}", { n: i + 1, total: list.length, file: path });
        const r = await fetchRetry(
          folder ? "/api/mkdir?path=" + path.replace(/\/\.(node|sensor)$/, "") : "/api/file?path=" + path,
          { method: folder ? "POST" : "PUT", body: folder ? null : data },
        );
        if (!r.ok) {
          let j = {};
          try {
            j = await r.json();
          } catch (e) {}
          failed.push(path + ": " + (hubSaid(j) || "HTTP " + r.status));
        }
      }
      m.textContent = tr("Restoring the settings");
      const r = await restoreSettings(settings, m);
      if (!r.ok)
        throw new Error(
          tr("The sensors and pictures were restored, but not the settings: {why}.", {
            why: hubSaid(await r.json().catch(() => ({}))) || tr("the hub refused them"),
          }),
        );
      m.textContent =
        tr("Restored {n} of {total} items and the settings. The hub is restarting.", {
          n: list.length - failed.length,
          total: list.length,
        }) + (failed.length ? " " + tr("Not restored: {list}", { list: failed.join("; ") }) : "");
      if (failed.length) m.className = "portMsg err";
      setTimeout(() => location.reload(), 15000);
    } catch (e) {
      m.className = "portMsg err";
      m.textContent = e.message;
    }
  };
  // A newer release, as GitHub lists it: this browser asks, as the hub does not reach the internet, at most once a day,
  // and keeps the answer on this device. The file for this hub's board is linked, to drop in the zone below; GitHub
  // lets a page read the list of releases, not download their files, so that takes the browser's own download.
  const REPO = "sjfaustino/esp32_sensors2onvif";
  const versionOf = (v) => (String(v).match(/^v?(\d+)\.(\d+)\.(\d+)/) || []).slice(1).map(Number);
  const isNewer = (a, b) => {
    for (let i = 0; i < 3; i++) if (a[i] != b[i]) return a[i] > b[i];
    return false;
  };
  async function latestRelease() {
    try {
      const kept = JSON.parse(localStorage.getItem("release") || "null");
      if (kept && Date.now() - kept.at < 86400000) return kept.release;
    } catch (e) {}
    const r = await fetch("https://api.github.com/repos/" + REPO + "/releases/latest");
    if (!r.ok) return null;
    const j = await r.json();
    const release = { tag: j.tag_name, url: j.html_url, files: j.assets.map((a) => [a.name, a.browser_download_url]) };
    try {
      localStorage.setItem("release", JSON.stringify({ at: Date.now(), release }));
    } catch (e) {}
    return release;
  }
  async function checkRelease() {
    const box = $("fwNew");
    try {
      const [latest, info] = await Promise.all([latestRelease(), call("GET", "/api/info")]);
      const runs = versionOf(info.version);
      if (!latest || runs.length != 3 || !isNewer(versionOf(latest.tag), runs)) {
        box.hidden = true;
        return;
      }
      const name = "sensors2onvif-" + latest.tag + "-" + info.board + ".bin",
        file = latest.files.find(([n]) => n == name);
      box.replaceChildren(
        el("b", {
          textContent: tr("Version {version} is out; this hub runs {runs}.", {
            version: latest.tag,
            runs: info.version,
          }),
        }),
        " ",
        el("a", { href: latest.url, target: "_blank", rel: "noopener", textContent: tr("What is new") }),
        el(
          "p",
          null,
          ...(file
            ? trParts("Download {file}, then drop it below.", { file: el("a", { href: file[1], textContent: name }) })
            : [tr("Its release has no file for this hub's board.")]),
        ),
      );
      box.hidden = false;
    } catch (e) {} // a browser without the internet: nothing to say
  }
  $("fwSend").onclick = async () => {
    const m = $("fwMsg"),
      f = $("fwFile").files[0];
    m.className = "portMsg";
    if (!f) {
      m.className = "portMsg err";
      m.textContent = tr("Choose a firmware .bin file first.");
      return;
    }
    if (
      !(await ask(tr("The hub restarts when it finishes, and the video stops until then."), {
        title: tr("Update the firmware?"),
        ok: tr("Update"),
      }))
    )
      return;
    // Sent with XMLHttpRequest, which, unlike fetch, tells how much of the upload has gone.
    const bar = $("fwBar"),
      pct = $("fwPct");
    const shown = (part) => {
      bar.style.width = Math.round(part * 100) + "%";
      pct.textContent =
        part < 1
          ? tr("Uploading: {n}%. Keep this page open.", { n: Math.round(part * 100) })
          : tr("Checking the signature");
    };
    $("fwProg").hidden = false;
    $("fwSend").disabled = true;
    m.textContent = "";
    shown(0);
    const result = await new Promise((done) => {
      const x = new XMLHttpRequest();
      x.open("PUT", "/api/firmware");
      x.upload.onprogress = (e) => e.lengthComputable && shown(e.loaded / e.total);
      x.onload = () => {
        let j = {};
        try {
          j = JSON.parse(x.responseText);
        } catch (e) {}
        done({ ok: x.status >= 200 && x.status < 300, error: j.error });
      };
      x.onerror = () => done({ ok: false });
      x.send(f);
    });
    $("fwProg").hidden = true;
    $("fwSend").disabled = false;
    if (result.ok) {
      m.textContent = tr("Done. The hub is restarting; this page reloads when it is back.");
      setTimeout(() => location.reload(), 25000);
    } else {
      m.className = "portMsg err";
      const why = hubText(result.error);
      m.textContent = why
        ? why.charAt(0).toUpperCase() + why.slice(1) + "."
        : tr("The upload failed. The hub keeps its current firmware.");
    }
  };
  $("rsSend").onclick = async () => {
    const m = $("rsMsg"),
      f = $("rsFile").files[0];
    m.className = "portMsg";
    if (!f) {
      m.className = "portMsg err";
      m.textContent = tr("Choose a backup file first.");
      return;
    }
    if (
      !(await ask(tr("The hub restarts and may move to another address."), {
        title: tr("Replace the hub's settings with this backup?"),
        ok: tr("Restore"),
      }))
    )
      return;
    const text = await f.text();
    if (isSealed(text) && !passphrase(m, false)) return;
    try {
      const r = await restoreSettings(text, m),
        j = await r.json().catch(() => ({}));
      m.className = r.ok ? "portMsg" : "portMsg err";
      m.textContent = r.ok
        ? tr("Restored. The hub is restarting.")
        : (hubSaid(j) || tr("The hub refused this backup")) + ".";
    } catch (e) {
      m.className = "portMsg err";
      m.textContent = e instanceof TypeError ? tr("The hub did not answer. Try again.") : e.message; // fetch fails as a TypeError
    }
  };
  (window.viewHooks = window.viewHooks || []).push((view) => {
    if (view == "maintenance") {
      loadSchedule();
      checkRelease();
    }
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
