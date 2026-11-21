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
              n.node,
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
      $("espEmpty").textContent = j.nodes.length ? "" : "No sensor nodes yet. Add one under Sensors Manager > Sensors.";
      $("nfList").replaceChildren(
        ...(j.nodeFirmware.length
          ? [headRow(["Chip", "Version", "Size", ""])]
          : [el("tr", null, el("td", { textContent: "None on the hub." }))]),
        ...j.nodeFirmware.map((f) =>
          el(
            "tr",
            null,
            ...[f.name, f.version, Math.round(f.size / 1024) + " KB"].map((v) => el("td", { textContent: v })),
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
          ),
        ),
      );
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
        ? "These are ignored until added under Sensors Manager > Sensors."
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
                  if (!confirm("Approve " + n.mac + "? Only if its label shows the fingerprint " + n.fingerprint + "."))
                    return;
                  try {
                    await call(
                      "POST",
                      "/api/pairing/approve",
                      "mac=" + n.mac + "&fingerprint=" + encodeURIComponent(n.fingerprint),
                    );
                    // Named now, so no sensor goes into service as a MAC address in every alert and notification.
                    for (let i = 1; i <= n.inputs; i++) {
                      const where = prompt(
                        "Where is " +
                          (n.inputs > 1 ? "sensor " + i + " of this node" : "this sensor") +
                          "? For example: Back door. Alerts and notifications will use this name.",
                        "",
                      );
                      if (where && where.trim())
                        await call("PUT", "/api/file?path=" + n.mac + "/" + i + "/name.txt", where.trim()).catch(
                          () => {},
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
      espTimer = setInterval(loadEsp, 3000);
    }
  }
  $("espAlarm").onchange = (e) => {
    call("POST", "/api/alerts", "alarm=" + (e.target.checked ? 1 : 0)).catch(() => {});
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
