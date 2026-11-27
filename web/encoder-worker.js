// Runs ffmpeg.wasm in a worker for web/pictures.js: loads it, encodes a PNG as one H.264 picture, decodes one, or
// takes the frames out of a moving image or a video, 2 a second as the hub plays them, as PNGs.
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
    if (m.frames) {
      const input = "in." + m.ext;
      core.FS.writeFile(input, m.frames);
      core.setTimeout(-1);
      // No wider than 1280: the page fits each into 640x360, and a phone's video would otherwise fill the memory.
      core.exec("-y", "-i", input, "-vf", "fps=2,scale='min(1280,iw)':-2", "-frames:v", String(m.most), "f%02d.png");
      const done = core.ret;
      core.reset();
      const frames = [];
      for (let i = 1; i <= m.most; i++) {
        const name = "f" + String(i).padStart(2, "0") + ".png";
        try {
          frames.push(core.FS.readFile(name));
          core.FS.unlink(name);
        } catch (e) {
          break; // no more
        }
      }
      core.FS.unlink(input);
      postMessage(
        { id: m.id, frames, ret: done },
        frames.map((f) => f.buffer),
      );
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
