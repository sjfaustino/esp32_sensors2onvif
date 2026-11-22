// The floor plan, Alarm > Floor plan: a picture of the property with each sensor where it is, lit while it fires. The
// picture is plan.jpg on the hub, made small enough here to fit the hub's 32 KB picture buffer; where each sensor is,
// plan.json, as {"AA-BB-CC-DD-EE-01/1": [x, y]} in thousandths of the picture's width and height. The walk test draws
// the same plan. Shown by signal instead, each spot is coloured by how well the hub hears its node, which shows where the
// radio struggles, and where a new node might go.
const plan = (() => {
  const LIMIT = 30 * 1024; // under the hub's 32 KB, with room to spare
  let image = null, // an object URL of plan.jpg, or null when there is none
    spots = {},
    loaded = false,
    timer = 0,
    placing = "",
    bySignal = false, // each spot coloured by its signal, instead of by what the sensor is doing
    known = null; // the sensors the hub has, once read, so places of deleted ones are dropped

  async function load() {
    try {
      const r = await fetch("/api/file?path=plan.json");
      spots = r.ok ? await r.json() : {};
    } catch (e) {
      spots = {};
    }
    try {
      const r = await fetch("/api/file?path=plan.jpg");
      if (image) URL.revokeObjectURL(image);
      image = r.ok ? URL.createObjectURL(await r.blob()) : null;
    } catch (e) {
      image = null;
    }
    loaded = true;
  }

  const keyOf = (n) => n.node + "/" + n.input;
  const labelOf = (n) => n.name || keyOf(n);

  // Draws the plan in box, with a spot for each sensor placed on it. stateOf gives a spot's colour: "alert" (red, firing
  // now), "problem" (amber), "never" (grey, not heard yet), "fired" (blue, seen in a walk test), "waiting" (outlined) or
  // "ok" (green); or by signal, "strong", "good", "fair" or "weak". onClick, when given, gets where the picture was
  // clicked, in thousandths; textOf, each spot's words, its sensor's name unless given.
  async function draw(box, sensors, stateOf, onClick, textOf) {
    if (!box) return;
    if (!loaded) await load();
    if (!image) {
      box.replaceChildren(
        el("p", { className: "note", textContent: tr("No floor plan yet. Add one under Alarm > Floor plan.") }),
      );
      return;
    }
    let wrap = box.firstElementChild;
    if (!wrap || wrap.dataset.src != image) {
      const img = el("img", { src: image, alt: tr("Floor plan"), className: "planImg", draggable: false });
      wrap = el("div", { className: "plan" }, img);
      wrap.dataset.src = image;
      box.replaceChildren(wrap);
    }
    const img = wrap.firstElementChild;
    img.onclick = onClick
      ? (ev) => {
          const r = img.getBoundingClientRect();
          onClick(
            Math.round(((ev.clientX - r.left) / r.width) * 1000),
            Math.round(((ev.clientY - r.top) / r.height) * 1000),
          );
        }
      : null;
    img.style.cursor = onClick ? "crosshair" : "";
    wrap.querySelectorAll(".spot").forEach((s) => s.remove());
    for (const n of sensors) {
      const p = spots[keyOf(n)];
      if (!p) continue;
      wrap.append(
        el("a", {
          className: "spot " + stateOf(n),
          href: "#sensor:" + keyOf(n),
          style: "left:" + p[0] / 10 + "%;top:" + p[1] / 10 + "%",
          title: textOf ? labelOf(n) + ": " + textOf(n) : labelOf(n),
          textContent: textOf ? labelOf(n) + " " + textOf(n) : labelOf(n),
        }),
      );
    }
  }

  // The state of a sensor as the plan page shows it.
  function stateOf(n) {
    return n.state == "active"
      ? "alert"
      : n.state == "never"
        ? "never"
        : n.state == "silent" || n.low || n.weak || n.refused
          ? "problem"
          : "ok";
  }

  // Its node's signal, in the steps the node's light and the pages use; "never" until it is first heard. A node weak on
  // average counts as weak, whatever its last report.
  function signalOf(n) {
    if (n.rssi == null) return "never";
    if (n.weak) return "weak";
    return n.rssi >= -67 ? "strong" : n.rssi >= -75 ? "good" : n.rssi >= -80 ? "fair" : "weak";
  }
  const signalText = (n) =>
    n.rssi == null
      ? tr("not heard yet")
      : n.rssi +
        " dBm" +
        (n.weak ? tr(", weak on average") : "") +
        (n.refused ? ", " + tr("{n} refused", { n: n.refused }) : "");

  // A picture of any size and kind, as a JPEG small enough for the hub: smaller and coarser until it fits.
  async function shrink(file) {
    const bitmap = await createImageBitmap(file);
    for (let width = Math.min(1200, bitmap.width); width >= 300; width = Math.round(width * 0.8)) {
      const c = el("canvas", { width, height: Math.round((bitmap.height * width) / bitmap.width) });
      const g = c.getContext("2d");
      g.fillStyle = "#fff"; // a transparent drawing stays readable
      g.fillRect(0, 0, c.width, c.height);
      g.drawImage(bitmap, 0, 0, c.width, c.height);
      for (const q of [0.8, 0.65, 0.5, 0.4]) {
        const blob = await new Promise((done) => c.toBlob(done, "image/jpeg", q));
        if (blob && blob.size <= LIMIT) return blob;
      }
    }
    throw new Error(tr("That picture is too detailed to fit the hub's 32 KB. Try a simpler drawing, or crop it."));
  }

  const msg = (text, err) => {
    $("planMsg").className = "portMsg" + (err ? " err" : "");
    $("planMsg").textContent = text;
  };

  async function refresh() {
    try {
      const esp = await call("GET", "/api/espnow");
      known = new Set(esp.nodes.map(keyOf));
      const choose = $("planSensor"),
        before = choose.value;
      choose.replaceChildren(
        el("option", { value: "", textContent: tr("Choose a sensor to place") }),
        ...esp.nodes.map((n) =>
          el("option", {
            value: keyOf(n),
            textContent: labelOf(n) + (spots[keyOf(n)] ? "" : tr(" (not on the plan yet)")),
          }),
        ),
      );
      choose.value = before;
      $("planRemove").disabled = !choose.value || !spots[choose.value];
      await draw($("planBox"), esp.nodes, bySignal ? signalOf : stateOf, place, bySignal ? signalText : null);
      $("planKey").hidden = !image;
      $("planKey").replaceChildren(
        ...(bySignal
          ? [
              ["strong", tr("Strong")],
              ["good", tr("Good")],
              ["fair", tr("Fair: may miss a message now and then")],
              ["weak", tr("Weak: move the node or the hub")],
              ["never", tr("Not heard yet")],
            ]
          : [
              ["ok", tr("Quiet")],
              ["alert", tr("Firing now")],
              ["problem", tr("A problem: silent, low battery, weak signal or refused messages")],
              ["never", tr("Not heard yet")],
            ]
        ).map(([kind, words]) => el("span", null, el("span", { className: "spot key " + kind }), words)),
      );
      const missing = esp.nodes.filter((n) => !spots[keyOf(n)]).length;
      $("planHint").textContent = !image
        ? ""
        : placing
          ? tr("Now tap where {sensor} is on the plan.", { sensor: placing })
          : missing
            ? trCount(
                missing,
                "{n} sensor is not on the plan yet: choose one above, then tap where it is.",
                "{n} sensors are not on the plan yet: choose one above, then tap where it is.",
              )
            : tr("Every sensor is on the plan. Tap a spot to open that sensor's page.");
    } catch (e) {}
  }

  // Without the places of sensors deleted since, which would otherwise fill the hub's 2 KB for plan.json in time.
  async function savePlaces() {
    if (known) for (const key in spots) if (!known.has(key)) delete spots[key];
    await call("PUT", "/api/file?path=plan.json", JSON.stringify(spots));
  }

  async function place(x, y) {
    const key = $("planSensor").value;
    if (!key) {
      msg(tr("Choose a sensor above first."), true);
      return;
    }
    spots[key] = [x, y];
    try {
      await savePlaces();
      msg(tr("Placed."));
      placing = "";
    } catch (e) {
      msg(e.message, true);
    }
    refresh();
  }

  $("planSensor").onchange = () => {
    const o = $("planSensor").selectedOptions[0];
    placing = $("planSensor").value ? o.textContent.replace(tr(" (not on the plan yet)"), "") : "";
    refresh();
  };
  $("planShow").onchange = () => {
    bySignal = $("planShow").value == "signal";
    refresh();
  };
  $("planRemove").onclick = async () => {
    delete spots[$("planSensor").value];
    try {
      await savePlaces();
      msg(tr("Taken off the plan."));
    } catch (e) {
      msg(e.message, true);
    }
    refresh();
  };
  $("planUpload").onclick = () => {
    const input = el("input", { type: "file", accept: "image/*" });
    input.onchange = async () => {
      if (!input.files[0]) return;
      msg(tr("Making it small enough for the hub..."));
      try {
        const blob = await shrink(input.files[0]);
        await call("PUT", "/api/file?path=plan.jpg", blob);
        loaded = false;
        msg(tr("Saved ({size} KB). Now place each sensor on it.", { size: Math.round(blob.size / 1024) }));
      } catch (e) {
        msg(e.message, true);
      }
      refresh();
    };
    input.click();
  };
  $("planDelete").onclick = async () => {
    if (
      !(await ask(tr("Every sensor's place on it goes too."), {
        title: tr("Delete the floor plan?"),
        ok: tr("Delete"),
        danger: true,
      }))
    )
      return;
    try {
      // Either may be missing, such as places kept from a picture deleted before; the other still goes.
      const gone = await Promise.allSettled([
        call("DELETE", "/api/file?path=plan.jpg"),
        call("DELETE", "/api/file?path=plan.json"),
      ]);
      if (gone.every((g) => g.status == "rejected")) throw gone[0].reason;
      spots = {};
      loaded = false;
      msg(tr("Deleted."));
    } catch (e) {
      msg(e.message, true);
    }
    refresh();
  };

  (window.viewHooks = window.viewHooks || []).push((view) => {
    clearInterval(timer);
    if (view != "plan") return;
    loaded = false; // read again, in case another device changed it
    refresh();
    timer = setInterval(() => document.hidden || refresh(), 2000);
  });
  return { draw, stateOf };
})();
