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
        textContent: opts.ok || "OK",
      }),
      cancel = el("button", { id: "askCancel", textContent: opts.cancel || "Cancel" });
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
          textContent: "Undo",
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
// what: what is deleted, as a question's title names it; more: what goes with it.
async function del(path, what, more) {
  if (await ask(more || "This cannot be undone.", { title: "Delete " + what + "?", ok: "Delete", danger: true }))
    act(() => call("DELETE", "/api/file?path=" + path), "Deleted " + what);
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
      el("button", { id: "askCancel", textContent: "Cancel", onclick: () => dlg.close() }),
    ),
  );
  dlg.onclose = () => dlg.remove();
  document.body.append(dlg);
  dlg.showModal();
}
// What each picture is shown for, in words, by the state its file is named after.
const STATE_WORDS = {
  active: "Seen",
  approaching: "Coming closer",
  away: "Going away",
  ltr: "Left to right",
  rtl: "Right to left",
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
    words = (delayed ? "Past event: " : "Live alert: ") + STATE_WORDS[state].toLowerCase();
  const thumb =
    shown && shown.preview
      ? el("img", {
          className: "thumb" + (same ? " same" : ""),
          alt: "",
          src: "/api/file?path=" + (own ? path : base + "/" + first).replace(/\.h264$/, ".jpg"),
        })
      : el("span", {
          className: "thumb" + (delayed ? " past" : "") + (same ? " same" : ""),
          textContent: shown ? "YOUR PICTURE" : delayed ? "PAST EVENT" : "ALERT",
        });
  const what = own ? "Your picture" : same ? "Same as Seen" : "Built-in",
    detail = own ? (own.size / 1024).toFixed(1) + " KB" : same ? "" : delayed ? "“PAST EVENT, CHECK LOGS”" : "“ALERT”";
  const acts = [
    [own ? "Upload another .h264 file" : "Upload a .h264 file", () => pick(path)],
    ["Make it from an image", () => imageDialog(path)],
  ];
  if (own)
    acts.push(
      ["Show it", () => showPicture(path, words)],
      [
        "Download it",
        () => el("a", { href: "/api/file?path=" + path, download: base.replace("/", "-") + "-" + name }).click(),
      ],
      [
        "Delete it",
        () =>
          del(
            path,
            "this picture",
            name == first
              ? "The built-in picture is shown in its place."
              : "The Seen picture, or the built-in one, is shown in its place.",
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
      textContent: "Change",
      ariaLabel: "Change the picture for " + words,
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
    el("span", { className: "slotHead", textContent: "Shown for a live alert" }),
    el("span", { className: "slotHead", textContent: "Shown for a past event" }),
  );
  for (const s of STATES) {
    grid.append(el("span", { className: "slotName", textContent: STATE_WORDS[s] }));
    for (const delayed of [false, true]) {
      const one = slot(base, s, delayed, files);
      one.querySelector(".slotText").dataset.when = delayed ? "Past event" : "Live alert";
      grid.append(one);
    }
  }
  return grid;
}
// The hub stores H.264 only, so the browser decodes the picture with the same ffmpeg.wasm that makes them.
async function showPicture(path, name) {
  const img = el("img", { alt: name, className: "pictureShown", hidden: true }),
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
    img.hidden = false;
    status.remove();
  } catch (e) {
    status.textContent = "Could not show this picture. " + e.message;
  }
}
function field(v) {
  return el("input", { type: "text", value: v, maxLength: 24, size: 24 });
}
function textPanel(base, number) {
  const d = el("details", null, el("summary", { textContent: "Change the words" }));
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
    className: "short",
  });
  const from = el("input", { type: "time", value: n.quiet ? hm(n.quiet[0]) : "" }),
    to = el("input", { type: "time", value: n.quiet ? hm(n.quiet[1]) : "" });
  const minutes = (v) => (v ? Number(v.slice(0, 2)) * 60 + Number(v.slice(3, 5)) : "");
  const zone = el(
    "select",
    null,
    el("option", { value: "perimeter", textContent: "perimeter: watched whenever the hub is armed" }),
    el("option", { value: "interior", textContent: "interior: not watched while the hub is armed home" }),
  );
  zone.value = n.interior ? "interior" : "perimeter";
  const camera = el("input", { maxLength: 32, value: n.camera || "", placeholder: "such as Camera 3, garden" });
  return el(
    "details",
    null,
    el("summary", {
      textContent: "Alert settings" + (n.hold || n.quiet || n.interior || n.camera ? " (changed)" : ""),
    }),
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
    el(
      "p",
      null,
      "Zone: ",
      zone,
      ". Mark the sensors inside the house as interior, so the hub can be armed home at night.",
    ),
    el(
      "p",
      null,
      "Camera to look at: ",
      camera,
      ". The NVR camera that covers this sensor, named in its notifications and on the Events page, so you know which recording to watch.",
    ),
    el(
      "p",
      null,
      el("button", {
        textContent: "Send test alert",
        onclick: async () => {
          if (
            await ask(
              "It goes to the NVR, Notifications and Home Assistant, whatever the alarm is set to, and is logged as a test.",
              { title: "Send a test alert from this sensor?", ok: "Send test alert" },
            )
          )
            act(
              () => call("POST", "/api/test-alert?path=" + base, "x=1"),
              "Sent a test alert. Check that the NVR recorded it.",
            );
        },
      }),
    ),
    el("button", {
      textContent: "Save alert settings",
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
    textContent: s.walkLeft ? " Running, " + Math.ceil(s.walkLeft / 60) + " min left." : "",
  });
  return el(
    "details",
    null,
    el("summary", {
      textContent: "Node settings" + (s.late || s.silent || s.reportEvery || s.repeat ? " (changed)" : ""),
    }),
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
      " minutes without a message. A disabled or stolen sensor is noticed only then, so keep it short: a little over the time between the node's regular reports. " +
        (s.every
          ? "The node says it reports every " +
            Math.round(s.every / 60) +
            " minutes, so empty uses " +
            Math.round(s.autoSilent / 60) +
            " minutes, and follows the node if that changes."
          : "Empty uses 180 minutes."),
    ),
    el(
      "p",
      null,
      "The node reports that it is alive every ",
      every,
      " minutes, and every ",
      repeat,
      " seconds while its sensor stays on. Empty leaves each to the node's own setting. These reach the node with the hub's next answer to it, so after its next report. Less often saves its battery, but a stolen node is noticed later. The repeat is kept below each sensor's alert time, so the alert does not lapse between repeats.",
    ),
    el("button", {
      textContent: "Save node settings",
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
          "Saved the node settings",
        ),
    }),
    el(
      "p",
      null,
      "Walk test: for the next 10 minutes, the node blinks its LED after each alert the hub confirms, 3 times for a strong signal, 2 for a fair one and 1 for a weak one, so you can walk the area and see what it covers. It starts with the first alert the node sends after this. ",
      el("button", {
        textContent: "Start a walk test",
        onclick: () =>
          act(() => call("POST", "/api/walk-test?path=" + s.mac, "go=1"), "The walk test is on for 10 minutes"),
      }),
      walk,
    ),
  );
}
function clearPanel() {
  const d = el("details", null, el("summary", { textContent: "Change the words" }));
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
    ? "Picture storage: " +
      (t.used / 1024).toFixed(0) +
      " KB of " +
      (t.total / 1024).toFixed(0) +
      " KB used. " +
      (t.kept / 1024).toFixed(0) +
      " KB of what is left is kept for the event history, so pictures cannot use it."
    : "";
  $("fill").style.width = t.total ? (100 * t.used) / t.total + "%" : "0";
  // The picture shown when no alert is on, for every sensor.
  const own = t.clear != null,
    clearActs = [
      [own ? "Upload another .h264 file" : "Upload a .h264 file", () => pick("clear.h264")],
      ["Make it from an image", () => imageDialog("clear.h264")],
    ];
  if (own)
    clearActs.push(
      ["Show it", () => showPicture("clear.h264", "All clear")],
      ["Delete it", () => del("clear.h264", "this picture", "The built-in one is shown in its place."), true],
    );
  $("clear").replaceChildren(
    el(
      "div",
      { className: "slot" },
      own && t.clearPreview
        ? el("img", { className: "thumb", alt: "", src: "/api/file?path=clear.jpg" })
        : el("span", { className: "thumb clear", textContent: own ? "YOUR PICTURE" : "ALL CLEAR" }),
      el(
        "div",
        { className: "slotText" },
        el("div", { textContent: "Shown when no alert is on" }),
        el("div", {
          className: "slotDetail",
          textContent: own ? "Your picture, " + (t.clear / 1024).toFixed(1) + " KB" : "Built-in “ALL CLEAR”",
        }),
        el("code", { className: "fname", textContent: "clear.h264" }),
      ),
      el("button", {
        textContent: "Change",
        ariaLabel: "Change the all clear picture",
        onclick: () => choose("Shown when no alert is on", "clear.h264", clearActs),
      }),
    ),
    clearPanel(),
  );
  const tree = $("tree");
  tree.textContent = "";
  if (!t.nodes.length)
    window.hub.empty(
      tree,
      "No nodes yet",
      "Pair a node, which adds its sensors for you, or add one by its MAC address above.",
      el("a", { className: "btn", href: "#espnow", textContent: "Pair a node" }),
    );
  for (const s of t.nodes) {
    const num = el("input", { type: "text", placeholder: "sensor no.", size: 9, ariaLabel: "Sensor number" });
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
          el("h2", { className: "nodeName", textContent: names.length ? names.join(", ") : "Node " + s.mac }),
          names.length ? el("div", { className: "mac", textContent: s.mac }) : "",
        ),
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
              // The hub makes a folder that is already there without a word, as a restore needs, so this says it here.
              if (s.sensors.some((n) => n.input == +num.value)) {
                say("Sensor " + num.value + " already exists on " + s.mac, true);
                return;
              }
              act(() => call("POST", "/api/mkdir?path=" + s.mac + "/" + num.value), "Added sensor " + num.value);
            },
          }),
          " ",
          el("button", {
            className: "del",
            textContent: "Delete node",
            onclick: () =>
              del(
                s.mac,
                "the node " + (names.length ? names.join(", ") : s.mac),
                "Its sensors, their pictures and settings go too. Their past alerts stay in the event history.",
              ),
          }),
        ),
      ),
      nodePanel(s),
    );
    for (const n of s.sensors) {
      const files = Object.fromEntries(n.files.map((f) => [f.name, f]));
      const base = s.mac + "/" + n.input;
      const nameField = el("input", {
        type: "text",
        value: n.name || "",
        maxLength: 24,
        size: 18,
        placeholder: "Name, such as Back door",
        ariaLabel: "Name of sensor " + n.input,
      });
      card.append(
        el(
          "div",
          { className: "sensor" },
          el(
            "div",
            { className: "sensorHead" },
            el("span", { className: "sensorNo", textContent: "Sensor " + n.input }),
            nameField,
            el("button", {
              textContent: "Save name",
              onclick: () =>
                act(() => call("PUT", "/api/file?path=" + base + "/name.txt", nameField.value), "Saved the name"),
            }),
            el("span", { className: "grow" }),
            el("button", {
              className: "del",
              textContent: "Delete sensor",
              onclick: () =>
                del(
                  base,
                  "the sensor " + (n.name || base),
                  "Its pictures and settings go too. Its past alerts stay in the event history.",
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
  act(async () => {
    if ((await call("GET", "/api/tree")).nodes.some((n) => n.mac == m))
      throw new Error("Node " + m + " already exists");
    await call("POST", "/api/mkdir?path=" + m);
    $("mac").value = "";
  }, "Added node " + m);
};
load();
