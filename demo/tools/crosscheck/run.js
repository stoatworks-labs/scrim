// The page's half of the crosscheck: fetch the cases from crosscheck.py's
// server, run each through window.scrimDemo.begin/frame -- the page's own two
// clips at each frame's time, a fresh instance, the cues applied frame by
// frame -- and post back, for every frame, the two inputs as the page handed
// them to the shader (RGBA8, top row first) and the picture it drew, with the
// page's table of controls and any uniform the page set that the program
// lacks or the program has that nothing set. crosscheck.py pipes the inputs
// through smtest and compares; nothing is judged here.
const post = (path, body) => fetch(path, { method: 'POST', body });

async function main() {
  for (let i = 0; i < 400 && !window.scrimDemo; i += 1) await new Promise((r) => setTimeout(r, 50));
  if (!window.scrimDemo) throw new Error('window.scrimDemo never appeared: the page did not start');
  // Stop the page's own loop drawing between our frames (it would not change
  // our pixels, which are read back inside one task, but it costs time).
  document.querySelector('.btn--play')?.click();
  const job = await (await fetch('/xc/job.json')).json();
  await post('/xc/table', JSON.stringify(window.scrimDemo.table));
  const errors = [];
  const refused = [];
  for (const c of job.cases) {
    const run = window.scrimDemo.begin({
      settings: c.settings,
      width: c.width,
      height: c.height,
      srcWidth: c.srcWidth,
      srcHeight: c.srcHeight,
      dest: c.dest,
      src: c.src,
    });
    for (let n = 0; n < c.times.length; n += 1) {
      const out = run.frame(c.times[n], c.cues ? c.cues[n] : {}, { srcTime: c.srcTimes[n], inputs: c.inputs });
      if (out.error) errors.push(`${c.name} frame ${n}: GL error ${out.error}`);
      if (!out.ok) refused.push(`${c.name} frame ${n}`);
      await post(`/xc/out/${c.name}/${n}`, out.pixels);
      if (c.inputs) {
        await post(`/xc/dest/${c.name}/${n}`, out.destPixels);
        await post(`/xc/src/${c.name}/${n}`, out.srcPixels);
      }
    }
    run.end();
  }
  const { missing, unset } = window.scrimDemo.uniforms();
  await post('/xc/missing', JSON.stringify({ missing, unset, errors, refused }));
  await post('/xc/done', 'ok');
}

main().catch((error) => post('/xc/error', String(error?.stack ?? error)));
