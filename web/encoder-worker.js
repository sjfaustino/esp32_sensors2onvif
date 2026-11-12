// Runs ffmpeg.wasm in a worker for web/pictures.js: loads it, encodes a PNG as one H.264 picture, or decodes one.
let core;
self.onmessage = async (e) => {
  const m = e.data;
  try {
    if (m.load) {
      importScripts(m.load.js);
      core = await self.createFFmpegCore({
        mainScriptUrlOrBlob: m.load.js + "#" + btoa(JSON.stringify({ wasmURL: m.load.wasm })),
      });
      postMessage({ id: m.id });
      return;
    }
    if (m.decode) {
      core.FS.writeFile("in.h264", m.decode);
      core.setTimeout(-1);
      core.exec("-y", "-i", "in.h264", "-frames:v", "1", "out.png");
      const done = core.ret;
      core.reset();
      const shown = core.FS.readFile("out.png");
      core.FS.unlink("in.h264");
      core.FS.unlink("out.png");
      postMessage({ id: m.id, data: shown, ret: done });
      return;
    }
    core.FS.writeFile("in.png", m.png);
    core.setTimeout(-1);
    core.exec(
      "-y",
      "-framerate",
      "1",
      "-i",
      "in.png",
      "-frames:v",
      "1",
      "-c:v",
      "libx264",
      "-profile:v",
      "baseline",
      "-level",
      "3.0",
      "-crf",
      String(m.crf),
      "-pix_fmt",
      "yuv420p",
      "-x264-params",
      "scenecut=0:aud=0",
      "-f",
      "h264",
      "out.h264",
    );
    const ret = core.ret;
    core.reset();
    const data = core.FS.readFile("out.h264");
    core.FS.unlink("in.png");
    core.FS.unlink("out.h264");
    postMessage({ id: m.id, data, ret });
  } catch (err) {
    postMessage({ id: m.id, error: String(err) });
  }
};
