// The Notifications and Home Assistant pages.
(() => {
  const { fill, send, repeat } = window.hub;
  async function loadMqtt(all) {
    try {
      const m = await call("GET", "/api/mqtt");
      if (all) {
        $("mqHost").value = m.host;
        $("mqPort").value = m.port;
        $("mqUser").value = m.user;
        $("mqPw").value = "";
        $("mqPw").placeholder = m.password ? tr("saved") : "";
      }
      fill("mqInfo", [
        [
          tr("Connection"),
          hubText(m.state) + (m.since ? ", " + tr("since {time}", { time: new Date(m.since).toLocaleString() }) : ""),
        ],
        [tr("Events waiting to send"), m.waiting],
        [tr("Events lost, too many waiting"), m.lost],
        [tr("Topics"), m.topic + "/…"],
        [
          tr("Disarming from Home Assistant"),
          m.user
            ? tr("Allowed, as the hub logs in to the broker")
            : tr("Refused: give the broker a login, so not anyone on the network can disarm the hub"),
        ],
        [tr("In Home Assistant"), tr("the device {hub}, and one device per sensor", { hub: m.hub })],
      ]);
    } catch (e) {}
  }
  $("saveMq").onclick = async () => {
    if (
      await send(
        $("mqMsg"),
        "/api/mqtt",
        "host=" +
          encodeURIComponent($("mqHost").value.trim()) +
          "&port=" +
          encodeURIComponent($("mqPort").value.trim()) +
          "&user=" +
          encodeURIComponent($("mqUser").value) +
          "&password=" +
          encodeURIComponent($("mqPw").value),
        tr("Saved. The connection shows below within a few seconds."),
      )
    )
      loadMqtt(true);
  };
  // "07:30" as minutes after midnight; empty is -1, none.
  const dailyMinute = (v) => {
    const m = /^(\d\d):(\d\d)/.exec(v);
    return m ? Number(m[1]) * 60 + Number(m[2]) : -1;
  };
  const hhmm = (m) => String(Math.floor(m / 60)).padStart(2, "0") + ":" + String(m % 60).padStart(2, "0");
  // What each kind of server wants as its address and token.
  const HOOK_HELP = {
    ntfy: [
      "http://192.168.1.10/alarm",
      tr(
        "The address of your ntfy server, ending with the topic to send to. Subscribe to that topic in the ntfy app on your phone. Alerts arrive as urgent, problems as high priority, and the daily check and weekly summary quietly.",
      ),
      tr("Access token, if the topic needs one"),
    ],
    gotify: [
      "http://192.168.1.10/message",
      tr(
        "The address of your Gotify server, ending with /message, and an application's token from its Apps page. Alerts arrive with the highest priority.",
      ),
      tr("Application token"),
    ],
    json: [
      "http://192.168.1.10:8123/api/webhook/hub",
      tr(
        "Each message is posted as JSON, with the event, the sensor, its name and camera, the battery and the time. In Home Assistant, use a webhook trigger in an automation.",
      ),
      "",
    ],
    text: ["http://192.168.1.10:1880/hub", tr("Each message is posted as one line of plain text."), ""],
  };
  let hookTokenSaved = false;
  function showHookFormat() {
    const [example, help, token] = HOOK_HELP[$("hookFormat").value];
    $("hookUrl").placeholder = example;
    $("hookHelp").textContent = help;
    $("hookTokenRow").hidden = !token;
    $("hookToken").placeholder = hookTokenSaved ? tr("saved; leave blank to keep it") : token;
    $("hookForgetRow").hidden = !token || !hookTokenSaved;
  }
  $("hookFormat").onchange = showHookFormat;
  async function loadHook(all) {
    try {
      const h = await call("GET", "/api/webhook");
      hookTokenSaved = h.token;
      if (all) {
        $("hookUrl").value = h.url;
        $("hookFormat").value = h.url ? h.format : "ntfy";
        $("hookToken").value = "";
        $("hookForget").checked = false;
        $("hookAlerts").checked = h.alerts;
        $("hookLang").value = h.language || "en";
        $("hookProblems").checked = h.problems;
        $("hookDaily").value = h.daily >= 0 ? hhmm(h.daily) : "";
        $("hookWeekDay").value = String(h.weekday);
        $("hookWeekTime").value = hhmm(h.weekminute);
      }
      showHookFormat();
      fill("hookInfo", [
        [
          tr("Last message"),
          h.last ? hubText(h.last) + (h.lastAt ? ", " + new Date(h.lastAt).toLocaleString() : "") : tr("None sent yet"),
        ],
        [tr("Waiting to send"), h.waiting],
        [tr("Dropped, too many waiting"), h.dropped],
      ]);
    } catch (e) {}
  }
  $("saveHook").onclick = async () => {
    if (
      await send(
        $("hookMsg"),
        "/api/webhook",
        "url=" +
          encodeURIComponent($("hookUrl").value.trim()) +
          "&format=" +
          $("hookFormat").value +
          "&token=" +
          encodeURIComponent($("hookToken").value.trim()) +
          "&forget=" +
          ($("hookForget").checked ? 1 : 0) +
          "&weekday=" +
          $("hookWeekDay").value +
          "&weekminute=" +
          Math.max(0, dailyMinute($("hookWeekTime").value)) +
          "&alerts=" +
          ($("hookAlerts").checked ? 1 : 0) +
          "&problems=" +
          ($("hookProblems").checked ? 1 : 0) +
          "&language=" +
          $("hookLang").value +
          "&daily=" +
          dailyMinute($("hookDaily").value),
        tr("Saved."),
      )
    )
      loadHook(true);
  };
  $("testHook").onclick = async () => {
    if (
      await send(
        $("hookMsg"),
        "/api/webhook/test",
        "x=1",
        tr("Test sent. The result shows below within a few seconds."),
      )
    )
      setTimeout(() => loadHook(false), 3000);
  };
  $("weeklyHook").onclick = async () => {
    if (
      await send(
        $("hookMsg"),
        "/api/webhook/weekly",
        "x=1",
        tr("Summary sent. The result shows below within a few seconds."),
      )
    )
      setTimeout(() => loadHook(false), 3000);
  };
  (window.viewHooks = window.viewHooks || []).push((view) => {
    if (view == "notify") {
      loadHook(true);
      repeat(() => loadHook(false), 3000);
    }
    if (view == "ha") {
      loadMqtt(true);
      repeat(() => loadMqtt(false), 3000);
    }
  });
})();
