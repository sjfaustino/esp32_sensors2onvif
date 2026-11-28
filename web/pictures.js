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
// Fetches one encoder file, refuses it unless it is the published one, and hands back a local address for it.
async function checkedCoreFile(name) {
  const r = await fetch(CDN + name);
  if (!r.ok) throw new Error("HTTP " + r.status + " for " + name);
  const bytes = new Uint8Array(await r.arrayBuffer());
  if (sha256Hex(bytes) !== CORE_SHA256[name])
    throw new Error(tr("{file} is not the published file, so it was not run", { file: name }));
  return URL.createObjectURL(
    new Blob([bytes], { type: name.endsWith(".wasm") ? "application/wasm" : "text/javascript" }),
  );
}
// The worker that runs the encoder. Its code is web/encoder-worker.js, which the page carries as text.
const WORKER = document.getElementById("encoder-worker").textContent;
const RED = "#B00000",
  AMBER = "#B06000",
  GREEN = "#004000";
// The words a new picture starts with, in the page's language, as the NVR's recording shows them.
const WORDS = {
  active: tr("ACTIVE"),
  approaching: tr("APPROACHING"),
  away: tr("MOVING AWAY"),
  ltr: tr("LEFT TO RIGHT"),
  rtl: tr("RIGHT TO LEFT"),
  delayed: tr("PAST EVENT - CHECK LOGS"),
};
const NAMES = [
  ["label", tr("Name (first line)")],
  ["active", tr("Active")],
  ["approaching", tr("Approaching")],
  ["away", tr("Moving away")],
  ["ltr", tr("Left to right")],
  ["rtl", tr("Right to left")],
  ["delayed", tr("Past event (last line)")],
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
      throw new Error(tr("Could not load the encoder from the internet: {why}", { why: e.message }));
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
  if (!units[7] || !units[8] || !units[5]) throw new Error(tr("The encoder did not produce a complete picture"));
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
    if (r.ret !== 0) throw new Error(tr("The encoder failed"));
    const pic = keepPicture(r.data);
    if (pic.length <= MAX) return { pic, step };
  }
  throw new Error(tr("Even at the lowest quality this picture is over 32 KB. Try a simpler image."));
}
async function encode(colour, lines) {
  const c = el("canvas", { width: 640, height: 360 });
  paint(c, colour, lines);
  return (await encodeCanvas(c)).pic;
}
// ---- moving pictures ----
// A GIF, an animated WebP or PNG, or a video, becomes a moving picture: its frames, 2 a second, each drawn as a still
// picture is, with the same text, and encoded as one, so each reaches the NVR as today's still pictures do. A hub
// without PSRAM holds one frame, and takes the first.
const MOVING_TYPES = ["image/gif", "image/webp", "image/apng", "image/png"];
const isMoving = (f) => MOVING_TYPES.includes(f.type) || f.type.startsWith("video/");
const MOST_SOURCE = 100 * 1024 * 1024; // larger, and the browser runs out of memory taking it apart
let framesAllowed = null;
async function hubFrames() {
  if (framesAllowed === null) {
    try {
      framesAllowed = (await call("GET", "/api/info")).pictureFrames || 1;
    } catch (e) {
      return 1; // asked again next time
    }
  }
  return framesAllowed;
}
// The frames of a moving image as PNGs, at most most of them; one for a still image.
async function sourceFrames(file, most) {
  const w = await getEncoder(),
    data = new Uint8Array(await file.arrayBuffer()),
    ext = (file.name.split(".").pop() || "bin").toLowerCase().replace(/[^a-z0-9]/g, "") || "bin";
  const r = await talk(w, { frames: data, ext, most }, [data.buffer]);
  if (!r.frames.length) throw new Error(tr("The encoder could not read that file"));
  return r.frames;
}
// The units of an encoded picture, by type: SPS 7, PPS 8, IDR 5.
function unitsOf(d) {
  const units = {};
  const starts = [];
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
  return units;
}
// One file of the frames: the SPS, then each frame's PPS and IDR, as the hub reads it. Each frame keeps its own PPS, as
// each is encoded at the quality that fits it, which the PPS carries.
function joinFrames(pics) {
  const each = pics.map(unitsOf),
    parts = [each[0][7], ...each.flatMap((u) => [u[8], u[5]])];
  const out = new Uint8Array(parts.reduce((n, p) => n + p.length + 4, 0));
  let at = 0;
  for (const p of parts) {
    out[at + 3] = 1;
    out.set(p, at + 4);
    at += p.length + 4;
  }
  return out;
}
// Encodes each frame, drawn with the text, as a still picture; progress says how far it has got.
async function encodeFrames(frames, canvas, lines, progress) {
  const pics = [];
  let lowered = 0;
  for (let i = 0; i < frames.length; i++) {
    progress(i + 1, frames.length);
    const image = await createImageBitmap(new Blob([frames[i]], { type: "image/png" }));
    paintImage(canvas, image, lines);
    const r = await encodeCanvas(canvas);
    lowered = Math.max(lowered, r.step);
    pics.push(r.pic);
  }
  return { pics, pic: joinFrames(pics), step: lowered };
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
  const chooser = el("input", { type: "file", accept: "image/*,video/*" });
  const words = el("textarea", { rows: 3, cols: 30, placeholder: tr("Optional text on top, up to 3 lines") });
  const source = el("canvas", { width: 640, height: 360, style: VIEW });
  const result = el("img", { alt: tr("Encoded picture"), style: VIEW + ";display:none" });
  const note = el("p", {
    textContent: tr("Choose an image, or a GIF or a short video for a moving picture. Any shape or size is fine."),
  });
  const make = el("button", { textContent: tr("Encode and preview"), disabled: true });
  const keep = el("button", { textContent: tr("Save to the hub"), disabled: true });
  const dlg = el(
    "dialog",
    null,
    el("strong", { textContent: path }),
    el("p", null, chooser),
    el("p", null, words),
    source,
    result,
    note,
    el("p", null, make, " ", keep, " ", el("button", { textContent: tr("Close"), onclick: () => dlg.close() })),
  );
  let image = null,
    file = null,
    pic = null,
    playing = null; // the timer that plays a moving picture's preview, and its frames' addresses
  const stopPlaying = () => {
    if (!playing) return;
    clearInterval(playing.timer);
    playing.urls.forEach((u) => URL.revokeObjectURL(u));
    playing = null;
  };
  const lines = () =>
    words.value
      .split("\n")
      .map((l) => l.trim())
      .filter((l) => l)
      .slice(0, 3);
  const redraw = () => {
    pic = null;
    stopPlaying();
    keep.disabled = true;
    result.style.display = "none";
    if (image) paintImage(source, image, lines());
  };
  words.oninput = redraw;
  chooser.onchange = async () => {
    const f = chooser.files[0];
    if (!f) return;
    file = null;
    try {
      if (f.size > MOST_SOURCE) throw new Error("too large");
      // A video's first frame is drawn here only once the encoder has taken it apart, so a placeholder meanwhile.
      image = f.type.startsWith("video/") ? null : await createImageBitmap(f);
      file = isMoving(f) ? f : null;
      make.disabled = false;
      const held = file ? await hubFrames() : 1; // a hub of still pictures says so once the frames are taken out
      note.textContent =
        held > 1
          ? tr("Add text if you like, then encode. A moving image keeps up to {n} frames, 2 a second.", { n: held })
          : tr("Add text if you like, then encode.");
      if (!image) {
        const g = source.getContext("2d");
        g.fillStyle = "#000";
        g.fillRect(0, 0, 640, 360);
      }
      redraw();
    } catch (e) {
      image = null;
      make.disabled = true;
      note.textContent =
        f.size > MOST_SOURCE
          ? tr("That file is over 100 MB. Try a shorter clip.")
          : tr("This browser cannot open that file.");
    }
  };
  make.onclick = async () => {
    if (busy) return;
    busy = true;
    make.disabled = true;
    keep.disabled = true;
    try {
      note.textContent = tr("Encoding. The first time, this downloads about 30 MB.");
      stopPlaying();
      let r,
        frames = [],
        stillOnly = false; // a moving image, for a hub that keeps its first frame
      if (file) {
        // At least two, so a hub of still pictures can say a moving image will not move.
        const held = await hubFrames();
        frames = await sourceFrames(file, Math.max(2, held));
        stillOnly = held == 1 && frames.length > 1;
        frames = frames.slice(0, held);
        if (frames.length > 1)
          r = await encodeFrames(frames, source, lines(), (n, total) => {
            note.textContent = tr("Encoding frame {n} of {total}", { n, total });
          });
        else image = await createImageBitmap(new Blob([frames[0]], { type: "image/png" }));
      }
      if (!r) {
        paintImage(source, image, lines());
        r = await encodeCanvas(source);
        r.pics = [r.pic];
      }
      pic = r.pic;
      const size = (pic.length / 1024).toFixed(1) + " KB",
        count = r.pics.length;
      const how = r.step
        ? tr("{size}. Quality was lowered {n} of {steps} steps to fit 32 KB.", {
            size,
            n: r.step,
            steps: QUALITY.length - 1,
          })
        : tr("{size} at full quality.", { size });
      note.textContent = count > 1 ? tr("{n} frames, shown 2 a second.", { n: count }) + " " + how : how;
      if (stillOnly) note.textContent += " " + tr("This hub holds still pictures only, so it keeps the first frame.");
      try {
        if (result.src) URL.revokeObjectURL(result.src);
        const urls = [];
        for (const one of r.pics) urls.push(await decoded(one));
        result.src = urls[0];
        result.style.display = "block";
        if (urls.length > 1) {
          let at = 0;
          playing = {
            urls,
            timer: setInterval(() => {
              at = (at + 1) % urls.length;
              result.src = urls[at];
            }, 500),
          };
        }
      } catch (e) {
        note.textContent += " " + tr("The preview could not be shown, but the picture can still be saved.");
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
      say(tr("Saved {file}", { file: path }));
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
    if (playing) result.removeAttribute("src"); // its frames' addresses go with the timer
    stopPlaying();
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
    const refused = (e.hub || "").includes("wrong video format"); // not words: as the hub says it
    if (refused)
      throw new Error(
        tr("The hub refused this browser's video format. Please report this header: {header}", { header: header(pic) }),
      );
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
    say(tr("Getting the encoder ready. The first time, this downloads about 30 MB."));
    await getEncoder();
    for (let i = 0; i < jobsList.length; i++) {
      const [path, colour, lines] = jobsList[i];
      say(tr("Making picture {n} of {total}", { n: i + 1, total: jobsList.length }));
      await save(path, await encode(colour, lines));
    }
    await call("PUT", "/api/file?path=" + textPath, JSON.stringify(text));
    say(trCount(jobsList.length, "Saved {n} picture", "Saved {n} pictures"));
    await load();
  } catch (e) {
    say(e.message, true);
  } finally {
    busy = false;
  }
}
