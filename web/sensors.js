// The Sensors page: nodes, sensors, their pictures and settings. Shared helpers such as $, el and call are here too.
const STATES = ["active", "approaching", "away", "ltr", "rtl"];
const MAX = 32768,
  $ = (id) => document.getElementById(id);
function el(tag, props, ...kids) {
  const e = Object.assign(document.createElement(tag), props || {});
  e.append(...kids);
  return e;
}
// A question asked in the page, in place of the browser's confirm, prompt and alert boxes, which cannot be styled and
// look like an error. Resolves true when confirmed, or with the text typed when opts.input is given (null if not).
// opts: title, ok (the confirming button's words), cancel, danger (a red button, for what cannot be undone), input (the
// starting text, which asks for a line of text), max (its length), only (one button, to acknowledge).
function ask(text, opts) {
  opts = opts || {};
  return new Promise((resolve) => {
    const typed =
      opts.input == null
        ? null
        : el("input", { type: "text", value: opts.input, maxLength: opts.max || 24, ariaLabel: text });
    const ok = el("button", {
        id: "askOk",
        className: opts.danger ? "danger" : "primary",
        textContent: opts.ok || tr("OK"),
      }),
      cancel = el("button", { id: "askCancel", textContent: opts.cancel || tr("Cancel") });
    const dlg = el(
      "dialog",
      { id: "ask", className: "ask" },
      opts.title ? el("h2", { textContent: opts.title }) : "",
      el("p", { id: "askText", textContent: text }),
      typed || "",
      el("div", { className: "askActs" }, opts.only ? "" : cancel, ok),
    );
    let answer = typed ? null : false;
    ok.onclick = () => {
      answer = typed ? typed.value : true;
      dlg.close();
    };
    cancel.onclick = () => dlg.close();
    if (typed) typed.onkeydown = (e) => e.key == "Enter" && ok.click();
    dlg.onclose = () => {
      dlg.remove();
      resolve(answer);
    };
    document.body.append(dlg);
    dlg.showModal();
    // Enter on a dangerous question cancels, so a slip of the hand deletes nothing.
    (typed || (opts.danger ? cancel : ok)).focus();
  });
}
const tell = (text, title) => ask(text, { only: true, title });
// A short notice at the foot of the page that goes by itself: a change saved, or a refusal with the hub's reason.
// opts.undo, a function, adds an Undo button, and the notice stays longer to give time for it.
let noticeTimer = 0;
function notice(text, opts) {
  opts = opts || {};
  const t = $("toast");
  if (!t) return;
  clearTimeout(noticeTimer);
  t.className = "toast" + (opts.err ? " err" : "");
  t.replaceChildren(
    el("span", { textContent: text }),
    opts.undo
      ? el("button", {
          textContent: tr("Undo"),
          onclick: () => {
            t.hidden = true;
            opts.undo();
          },
        })
      : "",
  );
  t.hidden = false;
  noticeTimer = setTimeout(() => (t.hidden = true), opts.undo || opts.err ? 8000 : 4000);
}
// The Sensors page's messages: an error stays on the page, next to what it is about; anything else is a notice.
function say(t, err) {
  $("msg").textContent = err ? t : "";
  $("msg").className = err ? "err" : "";
  if (!err) notice(t);
}
async function call(method, url, body) {
  const r = await fetch(url, { method, body });
  let j = {};
  try {
    j = await r.json();
  } catch (e) {}
  if (!r.ok) throw Object.assign(new Error(hubSaid(j) || "HTTP " + r.status), { hub: j.error || "" }); // as said
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
      say(tr("{file} is larger than 32 KB", { file: f.name }), true);
      return;
    }
    act(
      async () => {
        await call("PUT", "/api/file?path=" + path, f);
        await makePreview(path, new Uint8Array(await f.arrayBuffer())).catch(() => {});
      },
      tr("Saved {file}", { file: path }),
    );
  };
  i.click();
}
// what: what is deleted, as a question's title names it; more: what goes with it.
async function del(path, what, more) {
  if (
    await ask(more || tr("This cannot be undone."), {
      title: tr("Delete {what}?", { what }),
      ok: tr("Delete"),
      danger: true,
    })
  )
    act(() => call("DELETE", "/api/file?path=" + path), tr("Deleted {what}", { what }));
}
// A choice of things to do, asked in the page: each choice is [words, what it does, true for a red button].
function choose(title, text, choices) {
  const dlg = el(
    "dialog",
    { id: "ask", className: "ask" },
    el("h2", { textContent: title }),
    text ? el("p", { textContent: text }) : "",
    el(
      "div",
      { className: "askActs column" },
      ...choices.map(([words, fn, danger]) =>
        el("button", {
          className: danger ? "del" : "",
          textContent: words,
          onclick: () => {
            dlg.close();
            fn();
          },
        }),
      ),
      el("button", { id: "askCancel", textContent: tr("Cancel"), onclick: () => dlg.close() }),
    ),
  );
  dlg.onclose = () => dlg.remove();
  document.body.append(dlg);
  dlg.showModal();
}
// What each picture is shown for, in words, by the state its file is named after.
const STATE_WORDS = {
  active: tr("Seen"),
  approaching: tr("Coming closer"),
  away: tr("Going away"),
  ltr: tr("Left to right"),
  rtl: tr("Right to left"),
};
// One picture: a small preview of it, or of what is shown in its place, and a button with what can be done to it.
// files maps each file of the sensor's folder to its size and whether it has a preview.
function slot(base, state, delayed, files) {
  const name = state + (delayed ? "-delayed" : "") + ".h264",
    path = base + "/" + name,
    first = delayed ? "active-delayed.h264" : "active.h264",
    own = files[name],
    same = !own && name != first && files[first],
    shown = own || same,
    words = delayed
      ? tr("Past event: {state}", { state: STATE_WORDS[state].toLowerCase() })
      : tr("Live alert: {state}", { state: STATE_WORDS[state].toLowerCase() });
  const thumb =
    shown && shown.preview
      ? el("img", {
          className: "thumb" + (same ? " same" : ""),
          alt: "",
          src: "/api/file?path=" + (own ? path : base + "/" + first).replace(/\.h264$/, ".jpg"),
        })
      : el("span", {
          className: "thumb" + (delayed ? " past" : "") + (same ? " same" : ""),
          textContent: shown ? tr("YOUR PICTURE") : delayed ? "PAST EVENT" : "ALERT", // not words: the built-in picture's
        });
  const what = own ? tr("Your picture") : same ? tr("Same as Seen") : tr("Built-in"),
    detail = own ? (own.size / 1024).toFixed(1) + " KB" : same ? "" : delayed ? "“PAST EVENT, CHECK LOGS”" : "“ALERT”"; // not words
  const acts = [
    [own ? tr("Upload another .h264 file") : tr("Upload a .h264 file"), () => pick(path)],
    [tr("Make it from an image"), () => imageDialog(path)],
  ];
  if (own)
    acts.push(
      [tr("Show it"), () => showPicture(path, words)],
      [
        tr("Download it"),
        () => el("a", { href: "/api/file?path=" + path, download: base.replace("/", "-") + "-" + name }).click(),
      ],
      [
        tr("Delete it"),
        () =>
          del(
            path,
            tr("this picture"),
            name == first
              ? tr("The built-in picture is shown in its place.")
              : tr("The Seen picture, or the built-in one, is shown in its place."),
          ),
        true,
      ],
    );
  return el(
    "div",
    { className: "slot" },
    thumb,
    el(
      "div",
      { className: "slotText" },
      el("div", { textContent: what }),
      detail ? el("div", { className: "slotDetail", textContent: detail }) : "",
      el("code", { className: "fname", textContent: name }),
    ),
    el("button", {
      textContent: tr("Change"),
      ariaLabel: tr("Change the picture for {what}", { what: words }),
      onclick: () => choose(words, name, acts),
    }),
  );
}
// A sensor's ten pictures: a row for each way it can see someone, live and past.
function slotGrid(base, files) {
  const grid = el(
    "div",
    { className: "slots" },
    el("span", { className: "gap" }),
    el("span", { className: "slotHead", textContent: tr("Shown for a live alert") }),
    el("span", { className: "slotHead", textContent: tr("Shown for a past event") }),
  );
  for (const s of STATES) {
    grid.append(el("span", { className: "slotName", textContent: STATE_WORDS[s] }));
    for (const delayed of [false, true]) {
      const one = slot(base, s, delayed, files);
      one.querySelector(".slotText").dataset.when = delayed ? tr("Past event") : tr("Live alert");
      grid.append(one);
    }
  }
  return grid;
}
// The hub stores H.264 only, so the browser decodes the picture with the same ffmpeg.wasm that makes them.
async function showPicture(path, name) {
  const img = el("img", { alt: name, className: "pictureShown", hidden: true }),
    status = el("p", { textContent: tr("Loading the picture...") });
  const dlg = el(
    "dialog",
    null,
    el("h2", { textContent: name }),
    status,
    img,
    el("p", null, el("button", { textContent: tr("Close"), onclick: () => dlg.close() })),
  );
  dlg.onclose = () => {
    if (img.src) URL.revokeObjectURL(img.src);
    dlg.remove();
  };
  document.body.append(dlg);
  dlg.showModal();
  try {
    const r = await fetch("/api/file?path=" + path).catch(() => {
      throw new Error(tr("Could not reach the hub."));
    });
    if (!r.ok) throw new Error(tr("The hub could not send this picture."));
    const pic = new Uint8Array(await r.arrayBuffer());
    status.textContent = tr("Getting the decoder ready. The first time, this downloads about 30 MB.");
    img.src = await decoded(pic).catch(() => {
      throw new Error(
        tr("The decoder could not load or could not read this picture. It needs internet access the first time."),
      );
    });
    img.hidden = false;
    status.remove();
  } catch (e) {
    status.textContent = tr("Could not show this picture. {why}", { why: e.message });
  }
}
function field(v) {
  return el("input", { type: "text", value: v, maxLength: 24, size: 24 });
}
function textPanel(base, number) {
  const d = el("details", null, el("summary", { textContent: tr("Change the words") }));
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
      className: "wordsCanvas",
    });
    const show = () => paint(c, RED, [f.label.value, f.active.value]);
    for (const k in f) f[k].oninput = show;
    show();
    d.append(
      el("table", null, body),
      c,
      el("button", {
        textContent: tr("Make and save 10 pictures"),
        onclick: () => {
          const v = {};
          for (const k in f) v[k] = f[k].value.trim();
          if (!v.label) {
            say(tr("Enter a name for the first line"), true);
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
    placeholder: tr("default"),
    className: "short",
  });
  const from = el("input", { type: "time", value: n.quiet ? hm(n.quiet[0]) : "" }),
    to = el("input", { type: "time", value: n.quiet ? hm(n.quiet[1]) : "" });
  const minutes = (v) => (v ? Number(v.slice(0, 2)) * 60 + Number(v.slice(3, 5)) : "");
  const zone = el(
    "select",
    null,
    el("option", { value: "perimeter", textContent: tr("perimeter: watched whenever the hub is armed") }),
    el("option", { value: "interior", textContent: tr("interior: not watched while the hub is armed home") }),
  );
  zone.value = n.interior ? "interior" : "perimeter";
  const camera = el("input", { maxLength: 32, value: n.camera || "", placeholder: tr("such as Camera 3, garden") });
  return el(
    "details",
    null,
    el("summary", {
      textContent: tr("Alert settings") + (n.hold || n.quiet || n.interior || n.camera ? tr(" (changed)") : ""),
    }),
    el("p", null, ...trParts("An alert lasts {hold} seconds. Empty uses the hub's default.", { hold })),
    el(
      "p",
      null,
      ...trParts(
        "Quiet hours from {from} to {to}. During quiet hours alerts are logged but not sent to the NVR or to Notifications. Leave both empty for none.",
        { from, to },
      ),
    ),
    el(
      "p",
      null,
      ...trParts(
        "Zone: {zone}. Mark the sensors inside the house as interior, so the hub can be armed home at night.",
        { zone },
      ),
    ),
    el(
      "p",
      null,
      ...trParts(
        "Camera to look at: {camera}. The NVR camera that covers this sensor, named in its notifications and on the Events page, so you know which recording to watch.",
        { camera },
      ),
    ),
    el(
      "p",
      null,
      el("button", {
        textContent: tr("Send test alert"),
        onclick: async () => {
          if (
            await ask(
              tr(
                "It goes to the NVR, Notifications and Home Assistant, whatever the alarm is set to, and is logged as a test.",
              ),
              { title: tr("Send a test alert from this sensor?"), ok: tr("Send test alert") },
            )
          )
            act(
              () => call("POST", "/api/test-alert?path=" + base, "x=1"),
              tr("Sent a test alert. Check that the NVR recorded it."),
            );
        },
      }),
    ),
    el("button", {
      textContent: tr("Save alert settings"),
      onclick: () =>
        act(
          () =>
            call(
              "POST",
              "/api/sensor-settings?path=" + base,
              "hold=" +
                hold.value +
                "&from=" +
                minutes(from.value) +
                "&to=" +
                minutes(to.value) +
                "&zone=" +
                zone.value +
                "&camera=" +
                encodeURIComponent(camera.value.trim()),
            ),
          tr("Saved the alert settings"),
        ),
    }),
  );
}
// A node's hardware: its sensor, and where it, a tamper switch and the battery are wired. The pins offered are those the
// node's chip can use (s2oHardwareValid in the hub's lib/s2o_proto); the node fetches the settings after its next report.
const SENSOR_KINDS = [
  [3, tr("RCWL-0516 microwave radar")],
  [4, tr("HC-SR501 passive infrared")],
  [5, tr("LD2410 24 GHz radar")],
  [1, tr("Other, its output goes high while it detects")],
  [2, tr("Other, its output goes low while it detects")],
];
const CELLS = [
  [0, trc("battery", "None")],
  [1, tr("Lithium-ion or lithium-polymer cell")],
  [2, tr("Lithium iron phosphate (LiFePO4) cell")],
];
function chipPins(chip) {
  const range = (a, b) => Array.from({ length: b - a + 1 }, (_, i) => a + i);
  return chip == 13
    ? { wake: range(0, 7), battery: range(0, 6) }
    : chip == 5
      ? { wake: [0, 1, 3, 4, 5], battery: range(0, 4) }
      : chip == 9
        ? { wake: range(1, 21), battery: range(1, 10) }
        : chip == 0
          ? { wake: [2, 4, 12, 13, 14, 15, 25, 26, 27, 32, 33, 34, 35, 36, 37, 38, 39], battery: range(32, 39) }
          : { wake: range(0, 48), battery: range(0, 48) };
}
function hardwarePanel(s) {
  const parse = (t) => {
    const v = (t || "").split(",").map(Number);
    return v.length == 6
      ? { number: v[0], sensor: v[1], sensorPin: v[2], tamperPin: v[3], batteryPin: v[4], cell: v[5] }
      : null;
  };
  const set = parse(s.hwSet),
    runs = parse(s.hwRuns),
    shown = set || runs,
    pins = chipPins(s.chip);
  const choice = (options, value, label) =>
    el(
      "select",
      { ariaLabel: label },
      ...options.map(([v, text]) => el("option", { value: v, textContent: text, selected: value == v })),
    );
  const pinChoice = (list, value, label, none) =>
    choice(
      (none ? [["", tr("None")]] : []).concat(list.map((p) => [p, "GPIO " + p])),
      value == 255 ? "" : value,
      label,
    );
  const sensor = choice(SENSOR_KINDS, shown ? shown.sensor : 3, tr("Sensor")),
    sensorPin = pinChoice(pins.wake, shown ? shown.sensorPin : pins.wake[2], tr("The sensor's pin"), false),
    tamperPin = pinChoice(pins.wake, shown ? shown.tamperPin : 255, tr("The tamper switch's pin"), true),
    batteryPin = pinChoice(pins.battery, shown ? shown.batteryPin : 255, tr("The battery's pin"), true),
    cell = choice(CELLS, shown ? shown.cell : 0, tr("The battery"));
  const state = !runs
    ? tr("This node's firmware is from before hardware settings: update it, and it can take them from here.")
    : !set
      ? tr("Set on the node itself; save here to set it from the hub.")
      : set.number == runs.number
        ? tr("The node runs these settings.")
        : runs.number > set.number
          ? tr("The node runs newer settings than these.")
          : tr("Saved; the node takes them after its next report.");
  return el(
    "details",
    null,
    el("summary", {
      textContent:
        tr("Hardware") +
        (!runs ? tr(" (firmware too old)") : set && set.number != runs.number ? tr(" (waiting for the node)") : ""),
    }),
    el("p", { className: "note" }, state),
    el("label", { className: "netRow" }, el("span", { textContent: tr("Sensor") }), sensor),
    el("label", { className: "netRow" }, el("span", { textContent: tr("Sensor's pin") }), sensorPin),
    el("label", { className: "netRow" }, el("span", { textContent: tr("Tamper switch's pin") }), tamperPin),
    el("label", { className: "netRow" }, el("span", { textContent: tr("Battery's pin") }), batteryPin),
    el("label", { className: "netRow" }, el("span", { textContent: tr("Battery") }), cell),
    el(
      "p",
      { className: "note" },
      tr(
        "The pins listed are those this node's chip can use: ones that can wake it for the sensor and the tamper switch, ones that can read a voltage for the battery, through two equal resistors. The node checks them again before it takes them.",
      ),
    ),
    el("button", {
      textContent: tr("Save hardware"),
      disabled: !runs,
      onclick: () =>
        act(
          () =>
            call(
              "POST",
              "/api/node-hardware?path=" + s.mac,
              "sensor=" +
                sensor.value +
                "&sensorPin=" +
                sensorPin.value +
                "&tamperPin=" +
                tamperPin.value +
                "&batteryPin=" +
                batteryPin.value +
                "&cell=" +
                cell.value,
            ),
          tr("Saved the hardware; the node takes it after its next report"),
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
    className: "mid",
  });
  const silent = el("input", {
    type: "number",
    min: 1,
    max: 10080,
    value: s.silent ? Math.round(s.silent / 60) : "",
    placeholder: String(Math.round((s.autoSilent || 10800) / 60)),
    className: "mid",
  });
  const every = el("input", {
    type: "number",
    min: 1,
    max: 1440,
    value: s.reportEvery ? Math.round(s.reportEvery / 60) : "",
    placeholder: s.every && !s.reportEvery ? String(Math.round(s.every / 60)) : "",
    className: "mid",
  });
  const repeat = el("input", { type: "number", min: 1, max: 60, value: s.repeat || "", className: "tiny" });
  const seconds = (input) => (input.value ? String(Math.round(input.value * 60)) : "");
  const walk = el("span", {
    textContent: s.walkLeft ? " " + tr("Running, {n} min left.", { n: Math.ceil(s.walkLeft / 60) }) : "",
  });
  return el(
    "details",
    null,
    el("summary", {
      textContent: tr("Node settings") + (s.late || s.silent || s.reportEvery || s.repeat ? tr(" (changed)") : ""),
    }),
    el(
      "p",
      null,
      ...trParts(
        'An alert this node reports as having happened more than {late} seconds ago is a past event: it shows the sensor\'s past-event picture, or the built-in "PAST EVENT, CHECK LOGS", and gets its own NVR record. Its true time is in the Events list. Empty uses 30 seconds. Set it to how long this node can take to deliver an alert in real time, including retries.',
        { late },
      ),
    ),
    el(
      "p",
      null,
      ...trParts(
        "Report this node silent after {silent} minutes without a message. A disabled or stolen sensor is noticed only then, so keep it short: a little over the time between the node's regular reports.",
        { silent },
      ),
      " " +
        (s.every
          ? tr(
              "The node says it reports every {every} minutes, so empty uses {silent} minutes, and follows the node if that changes.",
              { every: Math.round(s.every / 60), silent: Math.round(s.autoSilent / 60) },
            )
          : tr("Empty uses 180 minutes.")),
    ),
    el(
      "p",
      null,
      ...trParts(
        "The node reports that it is alive every {every} minutes, and every {repeat} seconds while its sensor stays on. Empty leaves each to the node's own setting. These reach the node with the hub's next answer to it, so after its next report. Less often saves its battery, but a stolen node is noticed later. The repeat is kept below each sensor's alert time, so the alert does not lapse between repeats.",
        { every, repeat },
      ),
    ),
    el("button", {
      textContent: tr("Save node settings"),
      onclick: () =>
        act(
          () =>
            call(
              "POST",
              "/api/node-settings?path=" + s.mac,
              "late=" +
                late.value +
                "&silent=" +
                seconds(silent) +
                "&every=" +
                seconds(every) +
                "&repeat=" +
                repeat.value,
            ),
          tr("Saved the node settings"),
        ),
    }),
    el(
      "p",
      null,
      tr(
        "Walk test: for the next 10 minutes, the node blinks its LED after each alert the hub confirms, 3 times for a strong signal, 2 for a fair one and 1 for a weak one, so you can walk the area and see what it covers. It starts with the first alert the node sends after this.",
      ) + " ",
      el("button", {
        textContent: tr("Start a walk test"),
        onclick: () =>
          act(() => call("POST", "/api/walk-test?path=" + s.mac, "go=1"), tr("The walk test is on for 10 minutes")),
      }),
      walk,
    ),
  );
}
function clearPanel() {
  const d = el("details", null, el("summary", { textContent: tr("Change the words") }));
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
        textContent: tr("Make and save"),
        onclick: () => {
          const v = f.value.trim();
          if (!v) {
            say(tr("Enter the text to show"), true);
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
    ? tr(
        "Picture storage: {used} KB of {total} KB used. {kept} KB of what is left is kept for the event history, so pictures cannot use it.",
        { used: (t.used / 1024).toFixed(0), total: (t.total / 1024).toFixed(0), kept: (t.kept / 1024).toFixed(0) },
      )
    : "";
  $("fill").style.width = t.total ? (100 * t.used) / t.total + "%" : "0";
  // The picture shown when no alert is on, for every sensor.
  const own = t.clear != null,
    clearActs = [
      [own ? tr("Upload another .h264 file") : tr("Upload a .h264 file"), () => pick("clear.h264")],
      [tr("Make it from an image"), () => imageDialog("clear.h264")],
    ];
  if (own)
    clearActs.push(
      [tr("Show it"), () => showPicture("clear.h264", tr("All clear"))],
      [
        tr("Delete it"),
        () => del("clear.h264", tr("this picture"), tr("The built-in one is shown in its place.")),
        true,
      ],
    );
  $("clear").replaceChildren(
    el(
      "div",
      { className: "slot" },
      own && t.clearPreview
        ? el("img", { className: "thumb", alt: "", src: "/api/file?path=clear.jpg" })
        : el("span", { className: "thumb clear", textContent: own ? tr("YOUR PICTURE") : "ALL CLEAR" }), // not words
      el(
        "div",
        { className: "slotText" },
        el("div", { textContent: tr("Shown when no alert is on") }),
        el("div", {
          className: "slotDetail",
          textContent: own
            ? tr("Your picture, {size} KB", { size: (t.clear / 1024).toFixed(1) })
            : tr("Built-in “ALL CLEAR”"),
        }),
        el("code", { className: "fname", textContent: "clear.h264" }),
      ),
      el("button", {
        textContent: tr("Change"),
        ariaLabel: tr("Change the all clear picture"),
        onclick: () => choose(tr("Shown when no alert is on"), "clear.h264", clearActs),
      }),
    ),
    clearPanel(),
  );
  const tree = $("tree");
  tree.textContent = "";
  if (!t.nodes.length)
    window.hub.empty(
      tree,
      tr("No nodes yet"),
      tr("Pair a node, which adds its sensors for you, or add one by its MAC address above."),
      el("a", { className: "btn", href: "#espnow", textContent: tr("Pair a node") }),
    );
  for (const s of t.nodes) {
    const num = el("input", { type: "text", placeholder: tr("sensor no."), size: 9, ariaLabel: tr("Sensor number") });
    // A node has no name of its own, so it goes by its sensors' names, with its address below.
    const names = s.sensors.map((n) => n.name).filter((n) => n);
    const card = el(
      "section",
      { className: "card" },
      el(
        "div",
        { className: "head" },
        el(
          "div",
          null,
          el(
            "h2",
            { className: "nodeName" },
            el("a", {
              href: "#node:" + s.mac,
              textContent: names.length ? names.join(", ") : tr("Node {mac}", { mac: s.mac }),
            }),
          ),
          names.length ? el("div", { className: "mac", textContent: s.mac }) : "",
        ),
        el(
          "span",
          null,
          num,
          " ",
          el("button", {
            textContent: tr("Add sensor"),
            onclick: () => {
              if (!/^[1-9][0-9]{0,2}$/.test(num.value) || +num.value > 255) {
                say(tr("Sensor number must be 1 to 255"), true);
                return;
              }
              // The hub makes a folder that is already there without a word, as a restore needs, so this says it here.
              if (s.sensors.some((n) => n.input == +num.value)) {
                say(tr("Sensor {n} already exists on {mac}", { n: num.value, mac: s.mac }), true);
                return;
              }
              act(
                () => call("POST", "/api/mkdir?path=" + s.mac + "/" + num.value),
                tr("Added sensor {n}", { n: num.value }),
              );
            },
          }),
          " ",
          el("button", {
            className: "del",
            textContent: tr("Delete node"),
            onclick: () =>
              del(
                s.mac,
                tr("the node {name}", { name: names.length ? names.join(", ") : s.mac }),
                tr("Its sensors, their pictures and settings go too. Their past alerts stay in the event history."),
              ),
          }),
        ),
      ),
      nodePanel(s),
      hardwarePanel(s),
    );
    for (const n of s.sensors) {
      const files = Object.fromEntries(n.files.map((f) => [f.name, f]));
      const base = s.mac + "/" + n.input;
      const nameField = el("input", {
        type: "text",
        value: n.name || "",
        maxLength: 24,
        size: 18,
        placeholder: tr("Name, such as Back door"),
        ariaLabel: tr("Name of sensor {n}", { n: n.input }),
      });
      card.append(
        el(
          "div",
          { className: "sensor" },
          el(
            "div",
            { className: "sensorHead" },
            el("span", { className: "sensorNo", textContent: tr("Sensor {n}", { n: n.input }) }),
            nameField,
            el("button", {
              textContent: tr("Save name"),
              onclick: () =>
                act(
                  () =>
                    call("POST", "/api/sensor-settings?path=" + base, "name=" + encodeURIComponent(nameField.value)),
                  tr("Saved the name"),
                ),
            }),
            el("span", { className: "grow" }),
            el("button", {
              className: "del",
              textContent: tr("Delete sensor"),
              onclick: () =>
                del(
                  base,
                  tr("the sensor {name}", { name: n.name || base }),
                  tr("Its pictures and settings go too. Its past alerts stay in the event history."),
                ),
            }),
          ),
          slotGrid(base, files),
          textPanel(base, n.input),
          settingsPanel(base, n),
        ),
      );
    }
    tree.append(card);
  }
}
$("showNames").onchange = () => $("view-sensors").classList.toggle("showNames", $("showNames").checked);
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
      say(
        tr("Making thumbnail {n} of {total}. The first time, this downloads about 30 MB.", {
          n: i + 1,
          total: list.length,
        }),
      );
      const r = await fetch("/api/file?path=" + list[i]);
      if (!r.ok) throw new Error(tr("The hub could not send {file}", { file: list[i] }));
      await makePreview(list[i], new Uint8Array(await r.arrayBuffer()));
    }
  }, tr("Thumbnails are up to date"));
$("addNode").onclick = () => {
  const m = $("mac").value.trim().toUpperCase().replace(/:/g, "-");
  if (!/^([0-9A-F]{2}-){5}[0-9A-F]{2}$/.test(m)) {
    say(tr("Enter a MAC address like AA:BB:CC:DD:EE:01"), true);
    return;
  }
  act(
    async () => {
      if ((await call("GET", "/api/tree")).nodes.some((n) => n.mac == m))
        throw new Error(tr("Node {mac} already exists", { mac: m }));
      await call("POST", "/api/mkdir?path=" + m);
      $("mac").value = "";
    },
    tr("Added node {mac}", { mac: m }),
  );
};
// Read each time the page shows, so a node paired or a sensor added elsewhere meanwhile, as on another device, is there.
(window.viewHooks = window.viewHooks || []).push((view) => {
  if (view == "sensors") load();
});
