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
          [tr("Video address"), "rtsp://" + i.ip + ":" + i.rtspPort + "/stream"],
          [tr("Video streams open"), i.streams],
        ]);
      $("hubName").value = i.name.replace(/\.local$/, "");
      $("hubHosts").value = i.hosts || "";
      $("webPw").placeholder = i.password ? tr("Leave empty to keep it") : tr("8 to 63 characters");
      if (view == "onvif")
        fill("onvifInfo", [
          [tr("Login"), i.onvifLogin ? tr("Required: user admin and the hub password") : tr("None")],
          [tr("Device address"), "http://" + i.ip + ":" + i.onvifPort + "/onvif/device_service"],
          [tr("Event subscribers"), i.subscribers],
          [tr("Serial number"), i.serial],
          [tr("Requests not supported"), i.unsupported.join(", ") || tr("None")],
        ]);
      if (view == "http")
        fill("httpInfo", [
          [tr("Address"), "http://" + i.ip + at + "/"],
          [tr("Name"), "http://" + i.name + at + "/"],
          [tr("Sensor token"), tr("add {token} to /sensor requests", { token: "&token=" + i.token })],
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
        throw new Error(tr("Could not reach the hub. Trying again."));
      });
    try {
      const j = await (await get("/api/live")).json();
      if (run != liveRun) return;
      $("liveFile").textContent = j.file;
      if (j.seq !== liveSeq) {
        if (liveSeq === null)
          m.textContent = tr("Getting the decoder ready. The first time, this downloads about 30 MB.");
        const r = await get("/api/live/frame");
        if (!r.ok)
          throw new Error(
            r.status == 507
              ? tr("The hub is short of memory. Trying again.")
              : tr("The hub could not send the picture."),
          );
        const seq = Number(r.headers.get("X-Frame-Seq"));
        // The hub stops sending a picture that is replaced meanwhile, so the next one can be asked for.
        const data = new Uint8Array(
          await r.arrayBuffer().catch(() => {
            const e = new Error(tr("The picture changed while it was being sent. Trying again."));
            e.soon = true;
            throw e;
          }),
        );
        let url;
        try {
          url = await decoded(data);
        } catch (e) {
          throw new Error(
            tr("Could not show the picture. The decoder needs internet access the first time.") +
              " " +
              (e.message || ""),
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
      m.textContent = e.message || tr("Could not show the picture.");
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
      const state = (on) => (!n.ip ? tr("Enter the NVR's IP address") : on ? tr("Connected") : tr("Not connected"));
      fill("nvrInfo", [
        [tr("Video"), state(n.video)],
        [tr("Motion events"), state(n.events)],
        [tr("Viewers connected now"), n.viewers.join(", ") || tr("None")],
      ]);
    } catch (e) {}
  }
  $("saveNvr").onclick = async () => {
    if (await send($("nvrMsg"), "/api/nvr", "ip=" + encodeURIComponent($("nvrIp").value), tr("Saved."))) loadNvr();
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
        : tr("This fixed address is used only on {network}. Other WiFi networks use DHCP.", { network: n.staticFor });
    } catch (e) {}
  }
  $("netMode").onchange = netState;
  $("saveNet").onclick = async () => {
    const m = $("netMsg"),
      mode = $("netMode").value,
      ip = $("netIp").value;
    if (
      !(await ask(tr("A fixed address is used only on the Wi-Fi network it is connected to now."), {
        title: tr("Restart the hub to use this address?"),
        ok: tr("Save and restart"),
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
        ? tr("Saved. The hub is restarting.")
        : tr(
            "Saved. The hub is restarting and will get its address from the router. Open it by name, or find the new address in the router.",
          );
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
    if (!(await ask(warning, { title: tr("Restart the hub to use this port?"), ok: tr("Save and restart") }))) return;
    const j = await send(m, "/api/settings", field + "=" + encodeURIComponent(v), (j) =>
      j.restart ? tr("Saved. The hub is restarting.") : tr("Saved."),
    );
    if (j && j.restart) setTimeout(() => location.reload(), 10000);
  }
  $("saveRtsp").onclick = () =>
    savePort(
      "rtsp",
      "rtspPort",
      "rtspMsg",
      tr("The video stops briefly. A viewer such as VLC will need the new address."),
    );
  $("saveOnvif").onclick = () =>
    savePort(
      "onvif",
      "onvifPort",
      "onvifMsg",
      tr("The NVR will lose this camera until its channel is set to the new port."),
    );
  $("saveWeb").onclick = async () => {
    const m = $("webMsg"),
      port = $("webPort").value,
      name = $("hubName").value;
    if (
      !(await ask(tr("A new port or name restarts the hub; a new password or other names apply at once."), {
        title: tr("Save these settings?"),
        ok: tr("Save"),
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
      (j) =>
        j.restart
          ? tr("Saved. The hub is restarting.")
          : tr("Saved. A new password applies now: log in again with it."),
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
