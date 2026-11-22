// The network and NVR pages: Wi-Fi, the address, the ONVIF, HTTP and RTSP ports, the NVR, and the live video.
(() => {
  const { fill, send } = window.hub;
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
      $("hubHosts").value = i.hosts || "";
      $("webPw").placeholder = i.password ? "Leave empty to keep it" : "8 to 63 characters";
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
            r.status == 507 ? "The hub is short of memory. Trying again." : "The hub could not send the picture.",
          );
        const seq = Number(r.headers.get("X-Frame-Seq"));
        // The hub stops sending a picture that is replaced meanwhile, so the next one can be asked for.
        const data = new Uint8Array(
          await r.arrayBuffer().catch(() => {
            const e = new Error("The picture changed while it was being sent. Trying again.");
            e.soon = true;
            throw e;
          }),
        );
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
        img.classList.remove("off");
        liveSeq = seq;
        m.className = "portMsg";
        m.textContent = "";
      }
    } catch (e) {
      wait = e.soon ? 500 : 5000;
      m.className = e.soon ? "portMsg" : "portMsg err";
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
      !(await ask("A fixed address is used only on the Wi-Fi network it is connected to now.", {
        title: "Restart the hub to use this address?",
        ok: "Save and restart",
      }))
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
    if (!(await ask(warning, { title: "Restart the hub to use this port?", ok: "Save and restart" }))) return;
    const j = await send(m, "/api/settings", field + "=" + encodeURIComponent(v), (j) =>
      j.restart ? "Saved. The hub is restarting." : "Saved.",
    );
    if (j && j.restart) setTimeout(() => location.reload(), 10000);
  }
  $("saveRtsp").onclick = () =>
    savePort("rtsp", "rtspPort", "rtspMsg", "The video stops briefly. A viewer such as VLC will need the new address.");
  $("saveOnvif").onclick = () =>
    savePort(
      "onvif",
      "onvifPort",
      "onvifMsg",
      "The NVR will lose this camera until its channel is set to the new port.",
    );
  $("saveWeb").onclick = async () => {
    const m = $("webMsg"),
      port = $("webPort").value,
      name = $("hubName").value;
    if (
      !(await ask("A new port or name restarts the hub; a new password or other names apply at once.", {
        title: "Save these settings?",
        ok: "Save",
      }))
    )
      return;
    const j = await send(
      m,
      "/api/settings",
      "web=" +
        encodeURIComponent(port) +
        "&name=" +
        encodeURIComponent(name) +
        "&password=" +
        encodeURIComponent($("webPw").value) +
        "&hosts=" +
        encodeURIComponent($("hubHosts").value),
      (j) => (j.restart ? "Saved. The hub is restarting." : "Saved. A new password applies now: log in again with it."),
    );
    if (!j || !j.restart) return;
    const host = location.hostname.endsWith(".local") ? name + ".local" : location.hostname;
    setTimeout(() => {
      location.href = location.protocol + "//" + host + ":" + port + "/" + location.hash;
    }, 10000);
  };
  (window.viewHooks = window.viewHooks || []).push((view) => {
    if (view == "wifi") {
      const n = $("netFrame");
      if (!n.getAttribute("src")) n.src = "/setup";
    }
    if (view == "network") loadNet();
    if (view == "nvr") loadNvr();
    setLive(view == "live");
    if (view != "sensors") info(view);
  });
})();
