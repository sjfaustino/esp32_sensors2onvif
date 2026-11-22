const $ = (i) => document.getElementById(i);
let hub = "";
let needPassword = false;
async function scan() {
  try {
    const r = await fetch("/api/wifi/scan");
    const j = await r.json();
    hub = j.name || "";
    needPassword = !!j.needPassword;
    $("pwBox").hidden = !needPassword;
    if (j.scanning) {
      setTimeout(scan, 1500);
      return;
    }
    const seen = new Set();
    $("list").replaceChildren(
      ...j.networks
        .filter((n) => n.ssid && !seen.has(n.ssid) && seen.add(n.ssid))
        .map((n) => {
          const o = document.createElement("option");
          o.value = n.ssid;
          o.textContent = n.ssid + (n.open ? tr(" (open)") : "") + "  " + n.rssi + " dBm";
          return o;
        }),
    );
    $("state").textContent = j.current
      ? tr("Main network: {main}. Backup: {backup}.", { main: j.current, backup: j.backup || tr("none") })
      : tr("No network saved yet.");
    if (j.current) $("list").value = j.current;
  } catch (e) {
    setTimeout(scan, 3000);
  }
}
$("save").onclick = async () => {
  const ssid = $("name").value || $("list").value;
  const m = $("msg");
  m.className = "";
  if (!ssid) {
    m.className = "err";
    m.textContent = tr("Choose or type a network.");
    return;
  }
  const hubpw = $("hubpw").value;
  if (needPassword && (hubpw.length < 8 || hubpw.length > 63)) {
    m.className = "err";
    m.textContent = tr("Choose a hub password of 8 to 63 characters.");
    return;
  }
  if (needPassword && hubpw != $("hubpw2").value) {
    m.className = "err";
    m.textContent = tr("The two hub passwords differ.");
    return;
  }
  try {
    const r = await fetch("/api/wifi", {
      method: "POST",
      body:
        "ssid=" +
        encodeURIComponent(ssid) +
        "&password=" +
        encodeURIComponent($("pass").value) +
        "&role=" +
        $("role").value +
        (needPassword ? "&hubpw=" + encodeURIComponent(hubpw) : ""),
    });
    const j = await r.json();
    if (!r.ok) {
      m.className = "err";
      m.textContent = hubText(j.error);
      return;
    }
    m.textContent =
      $("role").value == "backup"
        ? tr("Saved as the backup network.")
        : tr("Saved. The hub is restarting to join {network}.", { network: ssid }) +
          " " +
          (hub
            ? tr("Reconnect this device to that network, then open {address}.", { address: "http://" + hub })
            : tr("Reconnect this device to that network.")) +
          " " +
          (needPassword
            ? tr(
                "If neither saved network works, the setup network comes back in about a minute, with the hub password as its password.",
              )
            : tr("If neither saved network works, the setup network comes back in about a minute."));
    if (needPassword) {
      needPassword = false;
      $("pwBox").hidden = true;
    }
  } catch (e) {
    m.className = "err";
    m.textContent = tr("The hub did not answer. Try again.");
  }
};
scan();
