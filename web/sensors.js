// The Sensors page: nodes, sensors, their pictures and settings. Shared helpers such as $, el and call are here too.
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
