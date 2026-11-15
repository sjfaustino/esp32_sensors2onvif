// Two guides in dialogs: Test everything, on the Overview, which runs the whole path an alert takes and says how each
// step went; and Add a sensor, on the Nodes and pairing page, which pairs a node or sets up a device that calls a web
// address, and waits for its first report.
(() => {
  const wait = (ms) => new Promise((done) => setTimeout(done, ms));
  const eventKey = (e) => e.received + " " + e.sensor + " " + e.event;

  function dialog(title, ...body) {
    const d = el("dialog", { className: "guide" }, el("h2", { textContent: title }), ...body);
    d.onclose = () => d.remove();
    document.body.append(d);
    d.showModal();
    return d;
  }
  const closeButton = (d) => el("button", { textContent: "Close", onclick: () => d.close() });

  // A copy button that works without HTTPS, which the browser's clipboard API needs.
  function copyField(value) {
    const input = el("input", { value, readOnly: true, className: "copyField" });
    const button = el("button", {
      textContent: "Copy",
      onclick: () => {
        input.select();
        let ok = false;
        try {
          ok = document.execCommand("copy");
        } catch (e) {}
        button.textContent = ok ? "Copied" : "Select and copy it";
        setTimeout(() => (button.textContent = "Copy"), 2000);
      },
    });
    return el("div", { className: "copyRow" }, input, button);
  }

  // ---- Test everything ----

  async function testEverything() {
    const esp = await call("GET", "/api/espnow");
    if (!esp.nodes.length) {
      alert("Add a sensor first: there is nothing to test with.");
      return;
    }
    const choose = el(
      "select",
      null,
      ...esp.nodes.map((n) =>
        el("option", { value: n.node + "/" + n.input, textContent: n.name || n.node + "/" + n.input }),
      ),
    );
    const steps = el("ul", { className: "ready" }),
      start = el("button", { textContent: "Start" });
    const d = dialog(
      "Test everything",
      el(
        "p",
        { className: "note" },
        "Sends a real test alert, from the sensor you choose, along the whole path: the NVR, Notifications and Home Assistant. ",
        "It records on the NVR like a real alert, and is logged as a test.",
      ),
      el("p", null, "Test with ", choose, " ", start),
      steps,
    );
    d.append(el("p", null, closeButton(d)));
    const step = (text) => {
      const li = el("li", { textContent: "… " + text });
      steps.append(li);
      return (ok, result) =>
        (li.textContent = (ok == null ? "– " : ok ? "✓ " : "✗ ") + text + (result ? ": " + result : ""));
    };
    start.onclick = async () => {
      start.disabled = true;
      steps.replaceChildren();
      const key = choose.value,
        name = choose.selectedOptions[0].textContent;
      const before = new Set((await call("GET", "/api/events")).map(eventKey));
      const hook0 = await call("GET", "/api/webhook").catch(() => null);
      const sent = step("Test alert from " + name);
      try {
        await call("POST", "/api/test-alert?path=" + key, "x=1");
        sent(true);
      } catch (e) {
        sent(false, e.message);
        start.disabled = false;
        return;
      }
      const at = new Date();
      const nvr = step("The NVR was told");
      await wait(7000); // the hub checks delivery 5 s after an alert starts
      const [events, o] = await Promise.all([call("GET", "/api/events"), call("GET", "/api/overview")]);
      const missed = events.some((e) => !before.has(eventKey(e)) && e.kind == 5 && e.sensor == key);
      if (!o.nvr.set) nvr(null, "no NVR address is set, so the hub cannot tell (NVR Manager > NVR)");
      else if (missed || !o.nvr.listening)
        nvr(false, "the NVR is not listening; the hub keeps the alert and plays it once it does");
      else nvr(true);
      const hook = step("Notifications");
      if (!hook0 || !hook0.url) hook(null, "not set up");
      else {
        let h = hook0;
        for (let i = 0; i < 20 && (h.lastAt == hook0.lastAt || h.waiting); i++) {
          await wait(500);
          h = await call("GET", "/api/webhook").catch(() => h);
        }
        hook(h.lastAt != hook0.lastAt && /^HTTP 2/.test(h.last), h.last || "no answer");
      }
      const ha = step("Home Assistant");
      ha(
        o.mqtt == "not set up" ? null : o.mqtt == "connected",
        o.mqtt == "connected" ? "connected, so it got the test" : o.mqtt,
      );
      steps.append(
        el("li", {
          textContent:
            "Now check the NVR: it should have a recording from the hub's channel at " +
            at.toLocaleTimeString([], { hour: "2-digit", minute: "2-digit" }) +
            ", with " +
            name +
            "'s picture.",
        }),
      );
      start.disabled = false;
      hub.loadOverview();
    };
  }
  $("testAll").onclick = () => testEverything().catch((e) => alert(e.message));

  // ---- Add a sensor ----

  // A made-up, locally administered MAC address, for a device that has none to give.
  function madeUpMac() {
    const b = crypto.getRandomValues(new Uint8Array(6));
    b[0] = (b[0] & 0xfc) | 0x02;
    return [...b].map((x) => x.toString(16).padStart(2, "0").toUpperCase()).join("-");
  }

  function addSensor() {
    const body = el("div");
    const d = dialog("Add a sensor", body);
    const choice = (label, note, onclick) =>
      el(
        "p",
        null,
        el("button", { textContent: label, onclick }),
        el("span", { className: "note", textContent: " " + note }),
      );
    body.replaceChildren(
      el("p", { textContent: "What kind of sensor is it?" }),
      choice("A sensor node", "one of these nodes, which talks to the hub over its own radio", () => node(d, body)),
      choice("A device that calls a web address", "such as a Shelly, a Tasmota plug or a script", () => web(d, body)),
      el("p", null, closeButton(d)),
    );
  }

  async function node(d, body) {
    try {
      await call("POST", "/api/pairing/open", "x=1");
    } catch (e) {
      body.replaceChildren(
        el("p", { className: "portMsg err", textContent: e.message }),
        el("p", null, closeButton(d)),
      );
      return;
    }
    body.replaceChildren(
      el("p", {
        textContent:
          "Pairing is open for 2 minutes. Power the node on, or reset it while holding its forget button. " +
          "It appears on this page under Pairing with its fingerprint: check that it matches the node's, approve it, and say where each of its sensors is.",
      }),
      el("p", null, closeButton(d)),
    );
    location.hash = "espnow";
  }

  async function web(d, body) {
    const info = await call("GET", "/api/info");
    const name = el("input", { placeholder: "Back door", maxLength: 40 }),
      mac = el("input", { value: madeUpMac(), size: 20 }),
      input = el("input", { type: "number", value: 1, min: 1, max: 255, style: "width:5em" }),
      msg = el("p", { className: "portMsg" }),
      create = el("button", { textContent: "Add it" });
    body.replaceChildren(
      el("label", { className: "netRow" }, el("span", { textContent: "Where is it" }), name),
      el("label", { className: "netRow" }, el("span", { textContent: "Its MAC address" }), mac),
      el("p", {
        className: "note",
        textContent: "The device's own, if it shows one, or the made-up one here. Input 1 unless it has several.",
      }),
      el("label", { className: "netRow" }, el("span", { textContent: "Input" }), input),
      el("p", null, create, " ", closeButton(d)),
      msg,
    );
    create.onclick = async () => {
      const id = mac.value.trim().toUpperCase().replace(/:/g, "-"),
        path = id + "/" + input.value;
      let existed = false;
      try {
        existed = (await call("GET", "/api/espnow")).nodes.some((n) => n.node + "/" + n.input == path);
        if (!existed) await call("POST", "/api/mkdir?path=" + path);
        if (name.value.trim()) await call("PUT", "/api/file?path=" + path + "/name.txt", name.value.trim());
      } catch (e) {
        msg.className = "portMsg err";
        msg.textContent = e.message;
        return;
      }
      const base =
        "http://" +
        info.ip +
        (info.webPort == 80 ? "" : ":" + info.webPort) +
        "/sensor?mac=" +
        id +
        "&input=" +
        input.value +
        (info.token ? "&token=" + info.token : "");
      const heard = el("p", { className: "note", textContent: "Waiting for its first report..." });
      body.replaceChildren(
        existed
          ? el("p", {
              className: "note",
              textContent:
                "That sensor already existed, so it was kept as it was" +
                (name.value.trim() ? ", with the new name." : "."),
            })
          : "",
        el("p", { textContent: "Set the device to call this address when it detects something:" }),
        copyField(base),
        el("p", {
          className: "note",
          textContent: "and, if it can, this one when it stops (optional: the alert ends by itself):",
        }),
        copyField(base + "&state=clear"),
        heard,
        el("p", null, closeButton(d)),
      );
      for (let i = 0; i < 300 && d.open; i++) {
        await wait(2000);
        const esp = await call("GET", "/api/espnow").catch(() => null);
        const row = esp && esp.nodes.find((n) => n.node + "/" + n.input == path);
        if (row && row.ago != null) {
          heard.className = "portMsg";
          heard.textContent = "✓ Its first report arrived. It is ready; try a test alert from its page.";
          heard.append(" ", hub.sensorLink(path, "Open its page"));
          return;
        }
      }
    };
  }
  $("addSensor").onclick = addSensor;
})();
