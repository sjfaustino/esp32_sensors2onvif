const STATES = ["active", "approaching", "away", "ltr", "rtl"];
const FILES = STATES.flatMap((s) => [s + ".h264", s + "-delayed.h264"]);
const MAX = 32768,
  $ = (id) => document.getElementById(id);
function el(tag, props, ...kids) {
  const e = Object.assign(document.createElement(tag), props || {});
  e.append(...kids);
  return e;
}
function say(t, err) {
  $("msg").textContent = t;
  $("msg").className = err ? "err" : "";
}
async function call(method, url, body) {
  const r = await fetch(url, { method, body });
  let j = {};
  try {
    j = await r.json();
  } catch (e) {}
  if (!r.ok) throw new Error(j.error || "HTTP " + r.status);
  return j;
}
async function act(fn, done) {
  try {
    await fn();
    if (done) say(done);
    await load();
  } catch (e) {
    say(e.message, true);
  }
}
function pick(path) {
  const i = el("input", { type: "file", accept: ".h264" });
  i.onchange = () => {
    const f = i.files[0];
    if (!f) return;
    if (f.size > MAX) {
      say(f.name + " is larger than 32 KB", true);
      return;
    }
    act(async () => {
      await call("PUT", "/api/file?path=" + path, f);
      await makePreview(path, new Uint8Array(await f.arrayBuffer())).catch(() => {});
    }, "Saved " + path);
  };
  i.click();
}
function del(path, what) {
  if (confirm("Delete " + what + "?")) act(() => call("DELETE", "/api/file?path=" + path), "Deleted " + what);
}
function previewButton(path, name) {
  return el("button", {
    textContent: "\u{1F441}",
    title: "Preview",
    ariaLabel: "Preview " + name,
    onclick: () => showPicture(path, name),
  });
}
// The hub stores H.264 only, so the browser decodes the picture with the same ffmpeg.wasm that makes them.
async function showPicture(path, name) {
  const img = el("img", { alt: name, style: "display:none;width:100%;max-width:640px" }),
    status = el("p", { textContent: "Loading the picture..." });
  const dlg = el(
    "dialog",
    null,
    el("h2", { textContent: name }),
    status,
    img,
    el("p", null, el("button", { textContent: "Close", onclick: () => dlg.close() })),
  );
  dlg.onclose = () => {
    if (img.src) URL.revokeObjectURL(img.src);
    dlg.remove();
  };
  document.body.append(dlg);
  dlg.showModal();
  try {
    const r = await fetch("/api/file?path=" + path).catch(() => {
      throw new Error("Could not reach the hub.");
    });
    if (!r.ok) throw new Error("The hub could not send this picture.");
    const pic = new Uint8Array(await r.arrayBuffer());
    status.textContent = "Getting the decoder ready. The first time, this downloads about 30 MB.";
    img.src = await decoded(pic).catch(() => {
      throw new Error(
        "The decoder could not load or could not read this picture. It needs internet access the first time.",
      );
    });
    img.style.display = "block";
    status.remove();
  } catch (e) {
    status.textContent = "Could not show this picture. " + e.message;
  }
}
function fileRow(path, name, size) {
  const acts = el("td", { className: "acts" });
  acts.append(
    el("button", { textContent: size == null ? "Upload" : "Replace", onclick: () => pick(path) }),
    " ",
    el("button", { textContent: "Use an image", onclick: () => imageDialog(path) }),
  );
  if (size != null) {
    acts.prepend(previewButton(path, name), " ");
    acts.append(
      " ",
      el("a", { className: "btn", href: "/api/file?path=" + path, download: name, textContent: "Download" }),
      " ",
      el("button", { className: "del", textContent: "Delete", onclick: () => del(path, path) }),
    );
  }
  return el(
    "tr",
    null,
    el("td", { textContent: name }),
    el("td", {
      className: size == null ? "missing" : "",
      textContent: size == null ? "uses a fallback" : (size / 1024).toFixed(1) + " KB",
    }),
    acts,
  );
}
// ---- making pictures from typed text ----
// The hub has no encoder, so this browser makes each picture with ffmpeg.wasm and uploads the result.
// These settings must stay in step with tools/make_frames.py. The hub refuses anything else.
const CDN = "https://cdn.jsdelivr.net/npm/@ffmpeg/core@0.12.6/dist/umd/";
// The encoder runs with this page's login, so it is checked before it runs: a changed file is refused. These are the
// published files of version 0.12.6, the same on jsDelivr and unpkg. A new version needs new values here.
const CORE_SHA256 = {
  "ffmpeg-core.js": "a34873964b0f62aec516bac75e3aa9086ec3535d4d07f0269aa94ea748b6cb71",
  "ffmpeg-core.wasm": "2390efa7fb66e7e42dbae15427571a5ffc96b829480904c30f471f0a78967f61",
};
// The browser's own SHA-256 needs https, which the hub does not have, so it is done here.
function sha256Hex(bytes) {
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
  const h = new Uint32Array([
    0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19,
  ]);
  const tail = new Uint8Array((bytes.length % 64 < 56 ? 64 : 128) - (bytes.length % 64));
  tail[0] = 0x80;
  const bits = bytes.length * 8;
  for (let i = 0; i < 8; i++) tail[tail.length - 1 - i] = Math.floor(bits / 2 ** (8 * i)) & 0xff;
  const w = new Uint32Array(64);
  const block = (data, at) => {
    for (let i = 0; i < 16; i++)
      w[i] =
        (data[at + 4 * i] << 24) | (data[at + 4 * i + 1] << 16) | (data[at + 4 * i + 2] << 8) | data[at + 4 * i + 3];
    for (let i = 16; i < 64; i++) {
      const a = w[i - 15],
        b = w[i - 2];
      const s0 = ((a >>> 7) | (a << 25)) ^ ((a >>> 18) | (a << 14)) ^ (a >>> 3);
      const s1 = ((b >>> 17) | (b << 15)) ^ ((b >>> 19) | (b << 13)) ^ (b >>> 10);
      w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    let [a, b, c, d, e, f, g, k] = h;
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
    h[0] += a;
    h[1] += b;
    h[2] += c;
    h[3] += d;
    h[4] += e;
    h[5] += f;
    h[6] += g;
    h[7] += k;
  };
  const whole = bytes.length - (bytes.length % 64);
  for (let at = 0; at < whole; at += 64) block(bytes, at);
  const last = new Uint8Array(bytes.length - whole + tail.length);
  last.set(bytes.subarray(whole));
  last.set(tail, bytes.length - whole);
  for (let at = 0; at < last.length; at += 64) block(last, at);
  return Array.from(h, (v) => v.toString(16).padStart(8, "0")).join("");
}
// Fetches one encoder file, refuses it unless it is the published one, and hands back a local address for it.
async function checkedCoreFile(name) {
  const r = await fetch(CDN + name);
  if (!r.ok) throw new Error("HTTP " + r.status + " for " + name);
  const bytes = new Uint8Array(await r.arrayBuffer());
  if (sha256Hex(bytes) !== CORE_SHA256[name]) throw new Error(name + " is not the published file, so it was not run");
  return URL.createObjectURL(
    new Blob([bytes], { type: name.endsWith(".wasm") ? "application/wasm" : "text/javascript" }),
  );
}
const WORKER = `let core;
self.onmessage=async e=>{const m=e.data;try{
if(m.load){importScripts(m.load.js);
core=await self.createFFmpegCore({mainScriptUrlOrBlob:m.load.js+"#"+btoa(JSON.stringify({wasmURL:m.load.wasm}))});
postMessage({id:m.id});return}
if(m.decode){core.FS.writeFile("in.h264",m.decode);core.setTimeout(-1);
core.exec("-y","-i","in.h264","-frames:v","1","out.png");
const done=core.ret;core.reset();
const shown=core.FS.readFile("out.png");core.FS.unlink("in.h264");core.FS.unlink("out.png");
postMessage({id:m.id,data:shown,ret:done});return}
core.FS.writeFile("in.png",m.png);core.setTimeout(-1);
core.exec("-y","-framerate","1","-i","in.png","-frames:v","1","-c:v","libx264","-profile:v","baseline","-level","3.0","-crf",String(m.crf),
"-pix_fmt","yuv420p","-x264-params","scenecut=0:aud=0","-f","h264","out.h264");
const ret=core.ret;core.reset();
const data=core.FS.readFile("out.h264");core.FS.unlink("in.png");core.FS.unlink("out.h264");
postMessage({id:m.id,data,ret})
}catch(err){postMessage({id:m.id,error:String(err)})}};`;
const RED = "#B00000",
  AMBER = "#B06000",
  GREEN = "#004000";
const WORDS = {
  active: "ACTIVE",
  approaching: "APPROACHING",
  away: "MOVING AWAY",
  ltr: "LEFT TO RIGHT",
  rtl: "RIGHT TO LEFT",
  delayed: "PAST EVENT - CHECK LOGS",
};
const NAMES = [
  ["label", "Name (first line)"],
  ["active", "Active"],
  ["approaching", "Approaching"],
  ["away", "Moving away"],
  ["ltr", "Left to right"],
  ["rtl", "Right to left"],
  ["delayed", "Past event (last line)"],
];
let encoder = null,
  jobs = new Map(),
  nextJob = 1;
function talk(w, msg, transfer) {
  return new Promise((ok, bad) => {
    const id = nextJob++;
    jobs.set(id, { ok, bad });
    w.postMessage({ ...msg, id }, transfer || []);
  });
}
function getEncoder() {
  if (!encoder)
    encoder = (async () => {
      const w = new Worker(URL.createObjectURL(new Blob([WORKER], { type: "text/javascript" })));
      w.onmessage = (e) => {
        const j = jobs.get(e.data.id);
        jobs.delete(e.data.id);
        if (e.data.error) j.bad(new Error(e.data.error));
        else j.ok(e.data);
      };
      const [js, wasm] = await Promise.all([checkedCoreFile("ffmpeg-core.js"), checkedCoreFile("ffmpeg-core.wasm")]);
      await talk(w, { load: { js, wasm } });
      return w;
    })().catch((e) => {
      encoder = null;
      throw new Error("Could not load the encoder from the internet: " + e.message);
    });
  return encoder;
}
function drawLines(g, lines, outline) {
  g.fillStyle = "#fff";
  g.textAlign = "center";
  g.textBaseline = "top";
  g.strokeStyle = "#000";
  g.lineWidth = 6;
  g.lineJoin = "round";
  const used = lines.filter((l) => l),
    top = Math.floor((360 - 70 * used.length) / 2) + 10;
  used.forEach((l, i) => {
    let size = 48;
    g.font = size + "px Arial,sans-serif";
    const w = g.measureText(l).width;
    if (w > 600) {
      size = Math.floor((size * 600) / w);
      g.font = size + "px Arial,sans-serif";
    }
    if (outline) g.strokeText(l, 320, top + 70 * i);
    g.fillText(l, 320, top + 70 * i);
  });
}
function paint(c, colour, lines) {
  const g = c.getContext("2d");
  g.fillStyle = colour;
  g.fillRect(0, 0, 640, 360);
  drawLines(g, lines, false);
}
// Fits the whole image into 640x360, with black bars if its shape differs.
function paintImage(c, image, lines) {
  const g = c.getContext("2d");
  g.fillStyle = "#000";
  g.fillRect(0, 0, 640, 360);
  const s = Math.min(640 / image.width, 360 / image.height),
    w = image.width * s,
    h = image.height * s;
  g.imageSmoothingQuality = "high";
  g.drawImage(image, (640 - w) / 2, (360 - h) / 2, w, h);
  drawLines(g, lines, true);
}
// Keeps only SPS, PPS and IDR, in that order, as tools/make_frames.py does.
function keepPicture(d) {
  const units = {};
  let starts = [];
  for (let i = 0; i + 3 <= d.length; i++)
    if (d[i] == 0 && d[i + 1] == 0 && d[i + 2] == 1) {
      starts.push(i);
      i += 2;
    }
  starts.forEach((s, k) => {
    let end = k + 1 < starts.length ? starts[k + 1] : d.length;
    if (k + 1 < starts.length) while (end > s + 3 && d[end - 1] == 0) end--;
    units[d[s + 3] & 31] = d.subarray(s + 3, end);
  });
  if (!units[7] || !units[8] || !units[5]) throw new Error("The encoder did not produce a complete picture");
  const parts = [units[7], units[8], units[5]],
    out = new Uint8Array(parts.reduce((n, p) => n + p.length + 4, 0));
  let at = 0;
  for (const p of parts) {
    out[at + 3] = 1;
    out.set(p, at + 4);
    at += p.length + 4;
  }
  return out;
}
// Higher steps are smaller and softer. The first one that fits the hub's limit is used.
const QUALITY = [24, 28, 32, 36, 38, 42, 46, 48, 51];
async function encodeCanvas(c) {
  const w = await getEncoder();
  const blob = await new Promise((r) => c.toBlob(r, "image/png")),
    png = new Uint8Array(await blob.arrayBuffer());
  for (let step = 0; step < QUALITY.length; step++) {
    const copy = png.slice(),
      r = await talk(w, { png: copy, crf: QUALITY[step] }, [copy.buffer]);
    if (r.ret !== 0) throw new Error("The encoder failed");
    const pic = keepPicture(r.data);
    if (pic.length <= MAX) return { pic, step };
  }
  throw new Error("Even at the lowest quality this picture is over 32 KB. Try a simpler image.");
}
async function encode(colour, lines) {
  const c = el("canvas", { width: 640, height: 360 });
  paint(c, colour, lines);
  return (await encodeCanvas(c)).pic;
}
// Shows the picture exactly as it will be streamed, by decoding the encoded result.
async function decoded(pic) {
  const w = await getEncoder(),
    copy = pic.slice(),
    r = await talk(w, { decode: copy }, [copy.buffer]);
  if (r.ret !== 0) throw new Error("preview failed");
  return URL.createObjectURL(new Blob([r.data], { type: "image/png" }));
}
function imageDialog(path) {
  const VIEW = "width:100%;max-width:480px;display:block;margin:8px 0;border-radius:4px;background:#000";
  const chooser = el("input", { type: "file", accept: "image/*" });
  const words = el("textarea", { rows: 3, cols: 30, placeholder: "Optional text on top, up to 3 lines" });
  const source = el("canvas", { width: 640, height: 360, style: VIEW });
  const result = el("img", { alt: "Encoded picture", style: VIEW + ";display:none" });
  const note = el("p", { textContent: "Choose an image. Any shape or size is fine." });
  const make = el("button", { textContent: "Encode and preview", disabled: true });
  const keep = el("button", { textContent: "Save to the hub", disabled: true });
  const dlg = el(
    "dialog",
    null,
    el("strong", { textContent: path }),
    el("p", null, chooser),
    el("p", null, words),
    source,
    result,
    note,
    el("p", null, make, " ", keep, " ", el("button", { textContent: "Close", onclick: () => dlg.close() })),
  );
  let image = null,
    pic = null;
  const lines = () =>
    words.value
      .split("\n")
      .map((l) => l.trim())
      .filter((l) => l)
      .slice(0, 3);
  const redraw = () => {
    pic = null;
    keep.disabled = true;
    result.style.display = "none";
    if (image) paintImage(source, image, lines());
  };
  words.oninput = redraw;
  chooser.onchange = async () => {
    const f = chooser.files[0];
    if (!f) return;
    try {
      image = await createImageBitmap(f);
      make.disabled = false;
      note.textContent = "Add text if you like, then encode.";
      redraw();
    } catch (e) {
      image = null;
      make.disabled = true;
      note.textContent = "This browser cannot open that file.";
    }
  };
  make.onclick = async () => {
    if (busy) return;
    busy = true;
    make.disabled = true;
    keep.disabled = true;
    try {
      note.textContent = "Encoding. The first time, this downloads about 30 MB.";
      const r = await encodeCanvas(source);
      pic = r.pic;
      const size = (pic.length / 1024).toFixed(1) + " KB";
      note.textContent =
        size +
        (r.step
          ? ". Quality was lowered " + r.step + " of " + (QUALITY.length - 1) + " steps to fit 32 KB."
          : " at full quality.");
      try {
        if (result.src) URL.revokeObjectURL(result.src);
        result.src = await decoded(pic);
        result.style.display = "block";
      } catch (e) {
        note.textContent += " The preview could not be shown, but the picture can still be saved.";
      }
      keep.disabled = false;
    } catch (e) {
      note.textContent = e.message;
    } finally {
      busy = false;
      make.disabled = !image;
    }
  };
  keep.onclick = async () => {
    if (!pic || busy) return;
    busy = true;
    keep.disabled = true;
    try {
      await save(path, pic);
      dlg.close();
      say("Saved " + path);
      busy = false;
      await load();
    } catch (e) {
      note.textContent = e.message;
      keep.disabled = false;
    } finally {
      busy = false;
    }
  };
  dlg.onclose = () => {
    if (result.src) URL.revokeObjectURL(result.src);
    dlg.remove();
  };
  document.body.append(dlg);
  dlg.showModal();
}
// ONVIF clients ask for a JPEG snapshot, which the hub cannot make from H.264, so one is stored beside each picture.
async function makePreview(path, pic) {
  const url = await decoded(pic),
    img = new Image();
  try {
    await new Promise((ok, bad) => {
      img.onload = ok;
      img.onerror = bad;
      img.src = url;
    });
    const c = el("canvas", { width: 320, height: 180 });
    c.getContext("2d").drawImage(img, 0, 0, 320, 180);
    const jpeg = await new Promise((r) => c.toBlob(r, "image/jpeg", 0.75));
    await call("PUT", "/api/file?path=" + path.replace(/\.h264$/, ".jpg"), jpeg);
  } finally {
    URL.revokeObjectURL(url);
  }
}
function header(p) {
  let h = "",
    n = 4;
  while (n < p.length && !(p[n] == 0 && p[n + 1] == 0 && p[n + 2] == 0)) h += p[n++].toString(16).padStart(2, "0");
  return h;
}
async function save(path, pic) {
  try {
    const done = await call("PUT", "/api/file?path=" + path, pic);
    await makePreview(path, pic).catch(() => {});
    return done;
  } catch (e) {
    if (e.message.includes("wrong video format"))
      throw new Error("The hub refused this browser's video format. Please report this header: " + header(pic));
    throw e;
  }
}
async function readText(p) {
  try {
    const r = await fetch("/api/file?path=" + p);
    if (r.ok) return await r.json();
  } catch (e) {}
  return {};
}
let busy = false;
async function makeAll(jobsList, textPath, text) {
  if (busy) return;
  busy = true;
  try {
    say("Getting the encoder ready. The first time, this downloads about 30 MB.");
    await getEncoder();
    for (let i = 0; i < jobsList.length; i++) {
      const [path, colour, lines] = jobsList[i];
      say("Making picture " + (i + 1) + " of " + jobsList.length);
      await save(path, await encode(colour, lines));
    }
    await call("PUT", "/api/file?path=" + textPath, JSON.stringify(text));
    say("Saved " + jobsList.length + " picture" + (jobsList.length > 1 ? "s" : ""));
    await load();
  } catch (e) {
    say(e.message, true);
  } finally {
    busy = false;
  }
}
function field(v) {
  return el("input", { type: "text", value: v, maxLength: 24, size: 24 });
}
function textPanel(base, number) {
  const d = el("details", null, el("summary", { textContent: "Change the text" }));
  let built = false;
  d.ontoggle = async () => {
    if (!d.open || built) return;
    built = true;
    const t = Object.assign({}, WORDS, { label: "SENSOR " + number }, await readText(base + "/text.json")),
      f = {},
      body = el("tbody");
    for (const [k, n] of NAMES) {
      f[k] = field(t[k]);
      body.append(el("tr", null, el("td", { textContent: n }), el("td", null, f[k])));
    }
    const c = el("canvas", {
      width: 640,
      height: 360,
      style: "width:240px;display:block;margin:8px 0;border-radius:4px",
    });
    const show = () => paint(c, RED, [f.label.value, f.active.value]);
    for (const k in f) f[k].oninput = show;
    show();
    d.append(
      el("table", null, body),
      c,
      el("button", {
        textContent: "Make and save 10 pictures",
        onclick: () => {
          const v = {};
          for (const k in f) v[k] = f[k].value.trim();
          if (!v.label) {
            say("Enter a name for the first line", true);
            return;
          }
          const list = [];
          for (const s of STATES) {
            list.push([base + "/" + s + ".h264", RED, [v.label, v[s]]]);
            list.push([base + "/" + s + "-delayed.h264", AMBER, [v.label, v[s], v.delayed]]);
          }
          makeAll(list, base + "/text.json", v);
        },
      }),
    );
  };
  return d;
}
const hm = (m) => String(Math.floor(m / 60)).padStart(2, "0") + ":" + String(m % 60).padStart(2, "0");
function settingsPanel(base, n) {
  const hold = el("input", {
    type: "number",
    min: 1,
    max: 3600,
    value: n.hold || "",
    placeholder: "default",
    style: "width:6em",
  });
  const from = el("input", { type: "time", value: n.quiet ? hm(n.quiet[0]) : "" }),
    to = el("input", { type: "time", value: n.quiet ? hm(n.quiet[1]) : "" });
  const minutes = (v) => (v ? Number(v.slice(0, 2)) * 60 + Number(v.slice(3, 5)) : "");
  return el(
    "details",
    null,
    el("summary", { textContent: "Alert settings" + (n.hold || n.quiet ? " (changed)" : "") }),
    el("p", null, "An alert lasts ", hold, " seconds. Empty uses the hub's default."),
    el(
      "p",
      null,
      "Quiet hours from ",
      from,
      " to ",
      to,
      ". During quiet hours alerts are logged but not sent to the NVR or to Notifications. Leave both empty for none.",
    ),
    el("button", {
      textContent: "Save alert settings",
      onclick: () =>
        act(
          () =>
            call(
              "POST",
              "/api/sensor-settings?path=" + base,
              "hold=" + hold.value + "&from=" + minutes(from.value) + "&to=" + minutes(to.value),
            ),
          "Saved the alert settings",
        ),
    }),
  );
}
function nodePanel(s) {
  const late = el("input", {
    type: "number",
    min: 1,
    max: 86400,
    value: s.late || "",
    placeholder: "30",
    style: "width:7em",
  });
  const silent = el("input", {
    type: "number",
    min: 1,
    max: 10080,
    value: s.silent ? Math.round(s.silent / 60) : "",
    placeholder: "180",
    style: "width:7em",
  });
  const seconds = () => (silent.value ? String(Math.round(silent.value * 60)) : "");
  return el(
    "details",
    null,
    el("summary", { textContent: "Node settings" + (s.late || s.silent ? " (changed)" : "") }),
    el(
      "p",
      null,
      "An alert this node reports as having happened more than ",
      late,
      ' seconds ago is a past event: it shows the sensor\'s past-event picture, or the built-in "PAST EVENT, CHECK LOGS", and gets its own NVR record. Its true time is in the Events list. Empty uses 30 seconds. Set it to how long this node can take to deliver an alert in real time, including retries.',
    ),
    el(
      "p",
      null,
      "Report this node silent after ",
      silent,
      " minutes without a message. A disabled or stolen sensor is noticed only then, so keep it short: a little over the time between the node's regular reports. Empty uses 180 minutes.",
    ),
    el("button", {
      textContent: "Save node settings",
      onclick: () =>
        act(
          () => call("POST", "/api/node-settings?path=" + s.mac, "late=" + late.value + "&silent=" + seconds()),
          "Saved the node settings",
        ),
    }),
  );
}
function clearPanel() {
  const d = el("details", null, el("summary", { textContent: "Change the text" }));
  let built = false;
  d.ontoggle = async () => {
    if (!d.open || built) return;
    built = true;
    const t = Object.assign({ clear: "ALL CLEAR" }, await readText("text.json")),
      f = field(t.clear);
    d.append(
      " ",
      f,
      " ",
      el("button", {
        textContent: "Make and save",
        onclick: () => {
          const v = f.value.trim();
          if (!v) {
            say("Enter the text to show", true);
            return;
          }
          makeAll([["clear.h264", GREEN, [v]]], "text.json", { clear: v });
        },
      }),
    );
  };
  return d;
}
function render(t) {
  $("usage").textContent = t.total
    ? (t.used / 1024).toFixed(0) +
      " KB of " +
      (t.total / 1024).toFixed(0) +
      " KB used. " +
      (t.kept / 1024).toFixed(0) +
      " KB of what is left is kept for the event history, so pictures cannot use it."
    : "";
  $("fill").style.width = t.total ? (100 * t.used) / t.total + "%" : "0";
  const c = $("clear");
  c.textContent = "";
  c.append(
    "clear.h264 " + (t.clear == null ? "(built-in) " : (t.clear / 1024).toFixed(1) + " KB "),
    el("button", { textContent: t.clear == null ? "Upload" : "Replace", onclick: () => pick("clear.h264") }),
    " ",
    el("button", { textContent: "Use an image", onclick: () => imageDialog("clear.h264") }),
  );
  if (t.clear != null)
    c.append(
      " ",
      previewButton("clear.h264", "clear.h264"),
      " ",
      el("button", { className: "del", textContent: "Delete", onclick: () => del("clear.h264", "clear.h264") }),
    );
  c.append(clearPanel());
  const tree = $("tree");
  tree.textContent = "";
  if (!t.nodes.length)
    tree.append(el("p", { className: "missing", textContent: "No nodes yet. Add one by its MAC address." }));
  for (const s of t.nodes) {
    const num = el("input", { type: "text", placeholder: "sensor no.", size: 9 });
    const card = el(
      "div",
      { className: "card" },
      el(
        "div",
        { className: "head" },
        el("h2", { textContent: s.mac }),
        el(
          "span",
          null,
          num,
          " ",
          el("button", {
            textContent: "Add sensor",
            onclick: () => {
              if (!/^[1-9][0-9]{0,2}$/.test(num.value) || +num.value > 255) {
                say("Sensor number must be 1 to 255", true);
                return;
              }
              act(() => call("POST", "/api/mkdir?path=" + s.mac + "/" + num.value), "Added sensor " + num.value);
            },
          }),
          " ",
          el("button", {
            className: "del",
            textContent: "Delete node",
            onclick: () => del(s.mac, "node " + s.mac + " and all its pictures"),
          }),
        ),
      ),
      nodePanel(s),
    );
    for (const n of s.sensors) {
      const sizes = Object.fromEntries(n.files.map((f) => [f.name, f.size]));
      const base = s.mac + "/" + n.input;
      const nameField = el("input", {
        type: "text",
        value: n.name || "",
        maxLength: 24,
        size: 18,
        placeholder: "Name",
      });
      card.append(
        el(
          "div",
          { className: "sensor" },
          el(
            "div",
            { className: "head" },
            el(
              "span",
              null,
              el("strong", { textContent: "Sensor " + n.input }),
              " ",
              nameField,
              " ",
              el("button", {
                textContent: "Save name",
                onclick: () =>
                  act(() => call("PUT", "/api/file?path=" + base + "/name.txt", nameField.value), "Saved the name"),
              }),
            ),
            el("button", {
              className: "del",
              textContent: "Delete sensor",
              onclick: () => del(base, "sensor " + base + " and its pictures"),
            }),
          ),
          el("table", null, el("tbody", null, ...FILES.map((f) => fileRow(base + "/" + f, f, sizes[f])))),
          textPanel(base, n.input),
          settingsPanel(base, n),
        ),
      );
    }
    tree.append(card);
  }
}
async function load() {
  try {
    render(await call("GET", "/api/tree"));
  } catch (e) {
    say(e.message, true);
  }
}
$("makeThumbs").onclick = () =>
  act(async () => {
    const t = await call("GET", "/api/tree"),
      list = [];
    if (t.clear != null && !t.clearPreview) list.push("clear.h264");
    for (const s of t.nodes)
      for (const n of s.sensors)
        for (const f of n.files) if (!f.preview) list.push(s.mac + "/" + n.input + "/" + f.name);
    for (let i = 0; i < list.length; i++) {
      say("Making thumbnail " + (i + 1) + " of " + list.length + ". The first time, this downloads about 30 MB.");
      const r = await fetch("/api/file?path=" + list[i]);
      if (!r.ok) throw new Error("The hub could not send " + list[i]);
      await makePreview(list[i], new Uint8Array(await r.arrayBuffer()));
    }
  }, "Thumbnails are up to date");
$("addNode").onclick = () => {
  const m = $("mac").value.trim().toUpperCase().replace(/:/g, "-");
  if (!/^([0-9A-F]{2}-){5}[0-9A-F]{2}$/.test(m)) {
    say("Enter a MAC address like AA:BB:CC:DD:EE:01", true);
    return;
  }
  act(() => call("POST", "/api/mkdir?path=" + m), "Added node " + m).then(() => ($("mac").value = ""));
};
load();
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
            r.status == 507
              ? "Both video connections are in use, or the hub is sending another file. Trying again."
              : "The hub could not send the picture.",
          );
        const seq = Number(r.headers.get("X-Frame-Seq")),
          data = new Uint8Array(await r.arrayBuffer());
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
      wait = 5000;
      m.className = "portMsg err";
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
  }
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
        if (s.late) await add(s.mac + "/settings.txt");
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
          ? [headRow(["Name", "Node", "Sensor", "State", "Last heard", "Battery", "Signal", "Report no.", "Refused"])]
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
      $("espNone").textContent = j.unknown.length
        ? "These are ignored until added under Sensors Manager > Sensors."
        : "None since the hub last started.";
    } catch (e) {}
  }
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
          encodeURIComponent($("webPw").value),
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
    })
    .catch(() => {});
  fetch("/api/nvr")
    .then((r) => r.json())
    .then((n) => {
      $("nvrWarn").hidden = !!n.ip;
    })
    .catch(() => {});
})();
