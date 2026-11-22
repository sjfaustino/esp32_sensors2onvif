// The Nodes and pairing page: the nodes and their signal, pairing, and the nodes' firmware.
(() => {
  const { batteryLeft, fill, headRow, heardAgo, send, sensorLink, signal } = window.hub;
  let espTimer = 0;
  async function loadEsp() {
    try {
      const j = await (await fetch("/api/espnow")).json();
      fill("espInfo", [
        ["Hub MAC address", j.mac],
        ["WiFi channel", j.channel],
        ["ESP-NOW key", j.key],
      ]);
      const STATES = {
        active: "Active",
        idle: "Idle",
        silent: "Silent",
        never: "Not heard yet",
      };
      $("espNodes").replaceChildren(
        ...(j.nodes.length
          ? [
              headRow([
                "Name",
                "Node",
                "Sensor",
                "State",
                "Last heard",
                "Battery",
                "Signal",
                "Report no.",
                "Refused",
                "Key",
                "Sensor type",
                "Firmware",
                "Restarts",
                "Last alert took",
              ]),
            ]
          : []),
        ...j.nodes.map((n) =>
          el(
            "tr",
            null,
            ...[
              sensorLink(n.node + "/" + n.input, n.name || "(no name)"),
              el("a", { href: "#node:" + n.node, textContent: n.node }),
              n.input,
              STATES[n.state] +
                (n.state == "silent" ? " (no report for " + Math.round(n.silentAfter / 60) + " minutes)" : ""),
              heardAgo(n.ago),
              (n.battery == null ? "-" : n.battery + "%" + (n.low ? " (low)" : "")) +
                (n.saving ? ", saving it" : "") +
                (n.daysLeft == null ? "" : ", " + batteryLeft(n.daysLeft)),
              signal(n.rssi) + (n.weak ? ", weak on average" : ""),
              n.counter == null ? "-" : n.counter,
              n.refused ? n.refused + " (" + n.refusal + ")" : "-",
              (n.paired ? "its own" : "shared") + (n.sealed ? ", sealed" : ""),
              n.sensor || "-",
              (n.version || "-") + (n.chip ? " (" + n.chip + ")" : "") + (n.update ? ", " + n.update : ""),
              n.restarts == null ? "-" : n.restarts + ", last: " + n.restart + (n.restartFault ? " (a fault)" : ""),
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
          "No sensor nodes yet",
          "Allow pairing and start a node in pairing mode, or add a device that calls a web address.",
          el("button", { className: "primary", textContent: "Allow pairing", onclick: () => $("pairOpen").click() }),
          el("button", { textContent: "Add a sensor", onclick: () => $("addSensor").click() }),
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
        const counted = on + " of " + nodes.length + " nodes run it";
        if (!f.pilot) return counted + ". Offered to every node of this chip.";
        const pilot = nodes.find((n) => n.node == f.pilot) || {};
        return (
          counted +
          ". " +
          (pilot.update == "update undone"
            ? nameOf(f.pilot) + " had it undone, so it is not offered to the others."
            : !f.pilotRuns
              ? "Tried on " + nameOf(f.pilot) + " first; the others get it a day after it runs it."
              : f.pilotLeft
                ? nameOf(f.pilot) + " runs it; the others get it in " + Math.ceil(f.pilotLeft / 3600) + " h."
                : nameOf(f.pilot) + " ran it for a day, so the others get it too.")
        );
      };
      const key = JSON.stringify([j.nodeFirmware, j.nodes.map((n) => [n.node, n.chip, n.version, n.update, n.name])]);
      if ($("nfList").dataset.key != key) {
        $("nfList").dataset.key = key;
        $("nfList").replaceChildren(
          ...(j.nodeFirmware.length
            ? [headRow(["Chip", "Version", "Size", "Nodes", "Try it first on", ""])]
            : [el("tr", null, el("td", { textContent: "None on the hub." }))]),
          ...j.nodeFirmware.map((f) => {
            const nodes = nodesOf(f);
            const pilot = el(
              "select",
              {
                ariaLabel: "The node to try it on first",
                onchange: async () => {
                  try {
                    await call("POST", "/api/node-firmware/pilot", "chip=" + f.chip + "&mac=" + pilot.value);
                    notice(pilot.value ? "It is tried on " + nameOf(pilot.value) + " first" : "Offered to every node");
                    loadEsp();
                  } catch (e) {
                    $("nfMsg").textContent = e.message;
                  }
                },
              },
              el("option", { value: "", textContent: "No node: every node at once" }),
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
                  textContent: "Delete",
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
        ...(j.unknown.length ? [headRow(["Node", "Sensor", "Last heard", "Signal", "Messages", ""])] : []),
        ...j.unknown.map((n) =>
          el(
            "tr",
            null,
            ...[n.mac, n.input, heardAgo(n.ago), signal(n.rssi), n.messages].map((v) => el("td", { textContent: v })),
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
      loadPairing();
      $("espNone").textContent = j.unknown.length
        ? "These are ignored until added under Sensors > Sensors."
        : "None since the hub last started.";
    } catch (e) {}
  }
  // Nodes waiting for approval, each with its key's fingerprint to compare with the node's label.
  async function loadPairing() {
    try {
      const p = await call("GET", "/api/pairing");
      $("pairOpen").disabled = !p.ready;
      $("pairState").textContent = !p.ready
        ? "Not available on this hub."
        : (p.open ? "Open for " + p.open + " s more. " : "Closed. ") + "The hub's fingerprint is " + p.hub + ".";
      $("pairWaiting").replaceChildren(
        ...(p.waiting.length ? [headRow(["Node", "Sensors", "Fingerprint", "Last heard", "Signal", ""])] : []),
        ...p.waiting.map((n) =>
          el(
            "tr",
            null,
            ...[
              n.mac +
                (n.paired ? " (paired before, with another key)" : n.known ? " (added before, without pairing)" : ""),
              n.inputs,
              n.fingerprint,
              heardAgo(n.ago),
              signal(n.rssi),
            ].map((v) => el("td", { textContent: v })),
            el(
              "td",
              null,
              el("button", {
                textContent: "Approve",
                onclick: async () => {
                  if (
                    !(await ask("Only if its label shows the fingerprint " + n.fingerprint + ".", {
                      title: "Approve " + n.mac + "?",
                      ok: "Approve",
                    }))
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
                      const where = await ask("For example: Back door. Alerts and notifications will use this name.", {
                        title: "Where is " + (n.inputs > 1 ? "sensor " + i + " of this node" : "this sensor") + "?",
                        input: "",
                        ok: "Save name",
                        cancel: "Skip",
                      });
                      if (where && where.trim())
                        await call(
                          "POST",
                          "/api/sensor-settings?path=" + n.mac + "/" + i,
                          "name=" + encodeURIComponent(where.trim()),
                        ).catch((e) =>
                          notice("The name was not saved: " + e.message + ". Name it on the Sensors page.", { err: true }),
                        );
                    }
                    $("pairMsg").textContent = "Paired " + n.mac + ". It starts reporting once it hears the hub again.";
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
                textContent: "Reject",
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
      .then(() => notice(on ? "Node problems now raise the alarm" : "Node problems no longer raise the alarm"))
      .catch((err) => {
        box.checked = !on;
        notice("Not changed: " + err.message, { err: true });
      });
  };
  $("nfSend").onclick = async () => {
    const m = $("nfMsg"),
      f = $("nfFile").files[0];
    m.className = "portMsg";
    if (!f) {
      m.className = "portMsg err";
      m.textContent = "Choose a node's firmware .bin file first.";
      return;
    }
    m.textContent = "Uploading. Keep this page open.";
    if (
      await send(
        m,
        "/api/node-firmware",
        f,
        "Done. The nodes of that chip update after their next report.",
        "PUT",
        "The upload failed.",
      )
    )
      loadEsp();
  };
  (window.viewHooks = window.viewHooks || []).push((view) => {
    setEsp(view == "espnow");
  });
})();
