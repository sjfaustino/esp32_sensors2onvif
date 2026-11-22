// "Add a node" on the Nodes and pairing page: a node's setting up, a step at a time, each ticked as the hub sees it done,
// from what the hub already knows: pairing, the hardware the node runs, its sensors' names, the floor plan and its alerts.
(() => {
  const { repeat } = window.hub;
  let chosen = "", // the node being set up: its MAC folder, or "" for a new one
    plan = null, // the floor plan, read once a visit
    events = []; // read only while a walk past the chosen node is still to come
  const tick = (ok) =>
    el("span", { className: "chip " + (ok ? "ok" : "none"), textContent: ok ? tr("Done") : tr("To do") });
  const button = (text, onclick) => el("button", { textContent: text, onclick });
  function step(n, ok, title, text, ...actions) {
    return el(
      "li",
      { className: "guideStep" + (ok ? " done" : "") },
      el("div", { className: "guideHead" }, el("b", { textContent: n + ". " + title }), tick(ok)),
      ok ? "" : el("p", { className: "note" }, text),
      ok || !actions.length ? "" : el("div", { className: "acts" }, ...actions),
    );
  }
  // What each step needs, for a node already paired or for one still to come.
  function steps(node, pairing, plan, events) {
    const waiting = pairing.waiting || [],
      paired = !!(node && node.paired),
      heard = paired || waiting.length > 0;
    const hwDone = node && node.hwSet && node.hwSet == node.hwRuns,
      named = node && node.sensors.length && node.sensors.every((s) => s.name),
      onPlan = node && node.sensors.every((s) => plan[node.mac + "/" + s.input]),
      alerted = node && events.some((e) => e.sensor.startsWith(node.mac + "/") && e.kind == 0);
    return [
      step(
        1,
        heard,
        tr("Flash it"),
        tr(
          "Write the node's firmware to it over USB: run python tools/flasher.py in the node project and choose its board, which also prints its label with its fingerprint. Or build it with PlatformIO, as the node's README says.",
        ),
      ),
      step(
        2,
        heard,
        tr("Let it pair"),
        tr(
          "Allow pairing, then reset the node, or power it up for the first time. It flashes yellow while it looks for the hub.",
        ),
        button(tr("Allow pairing for 2 minutes"), () => $("pairOpen").click()),
      ),
      step(
        3,
        paired,
        tr("Approve it"),
        waiting.length
          ? tr(
              "The hub has it: compare the fingerprint below with the one on the node's label or screen, and approve it under Pairing if they match. It then flashes green three times.",
            )
          : tr("Once the node says hello, it is listed under Pairing below with its fingerprint."),
      ),
      step(
        4,
        paired && hwDone,
        tr("Choose its sensor and pins"),
        node && !node.hwRuns
          ? tr(
              "This node's firmware is from before hardware settings: update it first, and it can take them from the hub.",
            )
          : node && node.hwSet
            ? tr("Saved: the node takes them after its next report and restarts into them.")
            : tr("On the Sensors page, under the node's Hardware, choose its sensor and the pins you wired."),
        button(tr("Open the Sensors page"), () => (location.hash = "sensors")),
      ),
      step(
        5,
        paired && named,
        tr("Name its sensors"),
        tr(
          "On the Sensors page, name each of its sensors by where it is, such as Back door: alerts and notifications use the name.",
        ),
        button(tr("Open the Sensors page"), () => (location.hash = "sensors")),
      ),
      step(
        6,
        paired && (onPlan || !Object.keys(plan).length), // without a floor plan, nothing to do
        tr("Put it on the floor plan"),
        tr("Choose its sensors on the Floor plan page and tap where they are."),
        button(tr("Open the floor plan"), () => (location.hash = "plan")),
      ),
      step(
        7,
        paired && alerted,
        tr("Walk past it"),
        tr(
          "Mount the node, press its reset button, and walk past it from where an intruder would come: its light flashes 3, 2 or 1 times for a strong, fair or weak signal, and its alert shows here.",
        ),
        node &&
          button(tr("Start a walk test on it"), () =>
            call("POST", "/api/walk-test?path=" + node.mac, "go=1").catch(() => {}),
          ),
      ),
    ];
  }
  window.hub.checkStep = step; // the node page's first hour ticks its checks the same way
  async function load() {
    try {
      if (!plan) plan = await fetch("/api/file?path=plan.json").then((r) => (r.ok ? r.json() : {}));
      const [tree, pairing] = await Promise.all([call("GET", "/api/tree"), call("GET", "/api/pairing")]);
      const walked = (n) => events.some((e) => e.sensor.startsWith(n.mac + "/") && e.kind == 0);
      const pending = tree.nodes.find((n) => n.mac == chosen && n.paired && !walked(n));
      if (pending) events = await call("GET", "/api/events");
      const nodes = tree.nodes.filter((n) => n.paired);
      const label = (n) =>
        (n.sensors
          .map((s) => s.name)
          .filter(Boolean)
          .join(", ") || n.mac) +
        " (" +
        n.mac +
        ")";
      const done = (n) => steps(n, pairing, plan, events).every((s) => s.classList.contains("done"));
      if (chosen && !nodes.some((n) => n.mac == chosen)) chosen = "";
      if (chosen === "" && !$("guideNode").dataset.picked) chosen = (nodes.find((n) => !done(n)) || {}).mac || "";
      $("guideNode").replaceChildren(
        el("option", { value: "", textContent: tr("A new node") }),
        ...nodes.map((n) =>
          el("option", { value: n.mac, textContent: done(n) ? tr("{node}: set up", { node: label(n) }) : label(n) }),
        ),
      );
      $("guideNode").value = chosen;
      const node = nodes.find((n) => n.mac == chosen);
      $("guideSteps").replaceChildren(...steps(node, pairing, plan, events).slice(0, node ? 7 : 3));
    } catch (e) {}
  }
  $("guideNode").onchange = () => {
    chosen = $("guideNode").value;
    $("guideNode").dataset.picked = "1";
    load();
  };
  (window.viewHooks = window.viewHooks || []).push((view) => {
    if (view != "espnow") return;
    plan = null;
    load();
    repeat(load, 10000);
  });
})();
