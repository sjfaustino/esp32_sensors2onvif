// The Nodes and pairing page: the nodes and their signal, pairing, and the nodes' firmware.
(() => {
  const { batteryLeft, fill, headRow, heardAgo, send, sensorLink, signal } = window.hub;
  let espTimer = 0;
  async function loadEsp() {
    try {
      const j = await (await fetch("/api/espnow")).json();
      fill("espInfo", [
        [tr("Hub MAC address"), j.mac],
        [tr("WiFi channel"), j.channel],
        [tr("ESP-NOW key"), j.key],
      ]);
      const STATES = {
        active: tr("Active"),
        idle: tr("Idle"),
        silent: tr("Silent"),
        never: tr("Not heard yet"),
      };
      $("espNodes").replaceChildren(
        ...(j.nodes.length
          ? [
              headRow([
                tr("Name"),
                tr("Node"),
                tr("Sensor"),
                tr("State"),
                tr("Last heard"),
                tr("Battery"),
                tr("Signal"),
                tr("Report no."),
                tr("Refused"),
                tr("Key"),
                tr("Sensor type"),
                tr("Firmware"),
                tr("Restarts"),
                tr("Last alert took"),
              ]),
            ]
          : []),
        ...j.nodes.map((n) =>
          el(
            "tr",
            null,
            ...[
              sensorLink(n.node + "/" + n.input, n.name || tr("(no name)")),
              el("a", { href: "#node:" + n.node, textContent: n.node }),
              n.input,
              STATES[n.state] +
                (n.state == "silent"
                  ? " " + tr("(no report for {n} minutes)", { n: Math.round(n.silentAfter / 60) })
                  : ""),
              heardAgo(n.ago),
              (n.battery == null ? "-" : n.battery + "%" + (n.low ? tr(" (low)") : "")) +
                (n.saving ? tr(", saving it") : "") +
                (n.daysLeft == null ? "" : ", " + batteryLeft(n.daysLeft)),
              signal(n.rssi) + (n.weak ? tr(", weak on average") : ""),
              n.counter == null ? "-" : n.counter,
              n.refused ? n.refused + " (" + hubText(n.refusal) + ")" : "-",
              (n.paired ? tr("its own") : tr("shared")) + (n.sealed ? tr(", sealed") : ""),
              n.sensor || "-",
              (n.version || "-") + (n.chip ? " (" + n.chip + ")" : "") + (n.update ? ", " + hubText(n.update) : ""),
              n.restarts == null
                ? "-"
                : tr("{n}, last: {why}", { n: n.restarts, why: hubText(n.restart) }) +
                  (n.restartFault ? tr(" (a fault)") : ""),
              n.responseMs == null ? "-" : n.responseMs + " ms",
            ].map((v) => el("td", null, v)),
          ),
        ),
      );
      if (j.nodes.length) $("espEmpty").replaceChildren();
      else if (!$("espEmpty").childElementCount)
        // refreshed every 3 s; the buttons stay put
        window.hub.empty(
          $("espEmpty"),
          tr("No sensor nodes yet"),
          tr("Allow pairing and start a node in pairing mode, or add a device that calls a web address."),
          el("button", {
            className: "primary",
            textContent: tr("Allow pairing"),
            onclick: () => $("pairOpen").click(),
          }),
          el("button", { textContent: tr("Add a sensor"), onclick: () => $("addSensor").click() }),
        );
      // Each file: how many nodes of its chip run it, and whether it is tried on one node first. Redrawn only when that
      // changes, so a choice being made is not cut short by the refresh.
      const nodesOf = (f) => {
        const seen = new Map();
        for (const n of j.nodes) if (n.chip == f.name && !seen.has(n.node)) seen.set(n.node, n);
        return [...seen.values()];
      };
      const nameOf = (mac) =>
        j.nodes
          .filter((n) => n.node == mac)
          .map((n) => n.name)
          .filter(Boolean)
          .join(", ") || mac;
      const rollout = (f, nodes) => {
        const on = nodes.filter((n) => n.version == f.version).length;
        const counted = tr("{on} of {all} nodes run it.", { on, all: nodes.length });
        if (!f.pilot) return counted + " " + tr("Offered to every node of this chip.");
        const pilot = nodes.find((n) => n.node == f.pilot) || {},
          name = nameOf(f.pilot);
        return (
          counted +
          " " +
          (pilot.update == "update undone" // not words: as the hub says it
            ? tr("{name} had it undone, so it is not offered to the others.", { name })
            : !f.pilotRuns
              ? tr("Tried on {name} first; the others get it a day after it runs it.", { name })
              : f.pilotLeft
                ? tr("{name} runs it; the others get it in {n} h.", { name, n: Math.ceil(f.pilotLeft / 3600) })
                : tr("{name} ran it for a day, so the others get it too.", { name }))
        );
      };
      const key = JSON.stringify([j.nodeFirmware, j.nodes.map((n) => [n.node, n.chip, n.version, n.update, n.name])]);
      if ($("nfList").dataset.key != key) {
        $("nfList").dataset.key = key;
        $("nfList").replaceChildren(
          ...(j.nodeFirmware.length
            ? [headRow([tr("Chip"), tr("Version"), tr("Size"), tr("Nodes"), tr("Try it first on"), ""])]
            : [el("tr", null, el("td", { textContent: tr("None on the hub.") }))]),
          ...j.nodeFirmware.map((f) => {
            const nodes = nodesOf(f);
            const pilot = el(
              "select",
              {
                ariaLabel: tr("The node to try it on first"),
                onchange: async () => {
                  try {
                    await call("POST", "/api/node-firmware/pilot", "chip=" + f.chip + "&mac=" + pilot.value);
                    notice(
                      pilot.value
                        ? tr("It is tried on {name} first", { name: nameOf(pilot.value) })
                        : tr("Offered to every node"),
                    );
                    loadEsp();
                  } catch (e) {
                    $("nfMsg").textContent = e.message;
                  }
                },
              },
              el("option", { value: "", textContent: tr("No node: every node at once") }),
              ...nodes.map((n) =>
                el("option", { value: n.node, textContent: nameOf(n.node), selected: n.node == f.pilot }),
              ),
            );
            return el(
              "tr",
              null,
              ...[f.name, f.version, Math.round(f.size / 1024) + " KB", rollout(f, nodes)].map((v) =>
                el("td", { textContent: v }),
              ),
              el("td", null, pilot),
              el(
                "td",
                null,
                el("button", {
                  textContent: tr("Delete"),
                  onclick: async () => {
                    try {
                      await call("POST", "/api/node-firmware/delete", "chip=" + f.chip);
                      loadEsp();
                    } catch (e) {
                      $("nfMsg").textContent = e.message;
                    }
                  },
                }),
              ),
            );
          }),
        );
      }
      $("espUnknown").replaceChildren(
        ...(j.unknown.length
          ? [headRow([tr("Node"), tr("Sensor"), tr("Last heard"), tr("Signal"), tr("Messages"), ""])]
          : []),
        ...j.unknown.map((n) =>
          el(
            "tr",
            null,
            ...[n.mac, n.input, heardAgo(n.ago), signal(n.rssi), n.messages].map((v) => el("td", { textContent: v })),
            el(
              "td",
              null,
              el("button", {
                textContent: tr("Add node"),
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
        ? tr("These are ignored until added under Sensors > Sensors.")
        : tr("None since the hub last started.");
    } catch (e) {}
  }
  // Nodes waiting for approval, each with its key's fingerprint to compare with the node's label.
  async function loadPairing() {
    try {
      const p = await call("GET", "/api/pairing");
      $("pairOpen").disabled = !p.ready;
      $("pairState").textContent = !p.ready
        ? tr("Not available on this hub.")
        : (p.open ? tr("Open for {n} s more.", { n: p.open }) : tr("Closed.")) +
          " " +
          tr("The hub's fingerprint is {fingerprint}.", { fingerprint: p.hub });
      $("pairWaiting").replaceChildren(
        ...(p.waiting.length
          ? [headRow([tr("Node"), tr("Sensors"), tr("Fingerprint"), tr("Last heard"), tr("Signal"), ""])]
          : []),
        ...p.waiting.map((n) =>
          el(
            "tr",
            null,
            ...[
              n.mac +
                (n.paired
                  ? tr(" (paired before, with another key)")
                  : n.known
                    ? tr(" (added before, without pairing)")
                    : ""),
              n.inputs,
              n.fingerprint,
              heardAgo(n.ago),
              signal(n.rssi),
            ].map((v) => el("td", { textContent: v })),
            el(
              "td",
              null,
              el("button", {
                textContent: tr("Approve"),
                onclick: async () => {
                  if (
                    !(await ask(
                      tr("Only if its label shows the fingerprint {fingerprint}.", { fingerprint: n.fingerprint }),
                      {
                        title: tr("Approve {mac}?", { mac: n.mac }),
                        ok: tr("Approve"),
                      },
                    ))
                  )
                    return;
                  try {
                    await call(
                      "POST",
                      "/api/pairing/approve",
                      "mac=" + n.mac + "&fingerprint=" + encodeURIComponent(n.fingerprint),
                    );
                    // Named now, so no sensor goes into service as a MAC address in every alert and notification.
                    for (let i = 1; i <= n.inputs; i++) {
                      const where = await ask(
                        tr("For example: Back door. Alerts and notifications will use this name."),
                        {
                          title:
                            n.inputs > 1
                              ? tr("Where is sensor {n} of this node?", { n: i })
                              : tr("Where is this sensor?"),
                          input: "",
                          ok: tr("Save name"),
                          cancel: tr("Skip"),
                        },
                      );
                      if (where && where.trim())
                        await call(
                          "POST",
                          "/api/sensor-settings?path=" + n.mac + "/" + i,
                          "name=" + encodeURIComponent(where.trim()),
                        ).catch((e) =>
                          notice(
                            tr("The name was not saved: {why}. Name it on the Sensors page.", { why: e.message }),
                            {
                              err: true,
                            },
                          ),
                        );
                    }
                    $("pairMsg").textContent = tr("Paired {mac}. It starts reporting once it hears the hub again.", {
                      mac: n.mac,
                    });
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
                textContent: tr("Reject"),
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
      espTimer = setInterval(() => document.hidden || loadEsp(), 3000);
    }
  }
  // Refused, the switch goes back to what the hub has, and says why: it must never show an alarm the hub will not raise.
  $("espAlarm").onchange = (e) => {
    const box = e.target,
      on = box.checked;
    call("POST", "/api/alerts", "alarm=" + (on ? 1 : 0))
      .then(() => notice(on ? tr("Node problems now raise the alarm") : tr("Node problems no longer raise the alarm")))
      .catch((err) => {
        box.checked = !on;
        notice(tr("Not changed: {why}", { why: err.message }), { err: true });
      });
  };
  $("nfSend").onclick = async () => {
    const m = $("nfMsg"),
      f = $("nfFile").files[0];
    m.className = "portMsg";
    if (!f) {
      m.className = "portMsg err";
      m.textContent = tr("Choose a node's firmware .bin file first.");
      return;
    }
    m.textContent = tr("Uploading. Keep this page open.");
    if (
      await send(
        m,
        "/api/node-firmware",
        f,
        tr("Done. The nodes of that chip update after their next report."),
        "PUT",
        tr("The upload failed."),
      )
    )
      loadEsp();
  };
  (window.viewHooks = window.viewHooks || []).push((view) => {
    setEsp(view == "espnow");
  });
})();
