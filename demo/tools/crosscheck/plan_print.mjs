// The JavaScript half of port_numbers.py: demo/port.js as the page runs it
// (ScrimInstance.SetFloatParameter, asked() and Plan), fed the same host
// values as plan_print.cpp and printing the same fields in the same order.
// Node imports port.js directly: it touches no DOM.
import { createInterface } from 'node:readline';
import { Plan, PT, ScrimInstance } from '../../port.js';

const put = (v) => ` ${v === 0 ? '0' : String(v)}`;
const lines = createInterface({ input: process.stdin });
const out = [];
for await (const line of lines) {
  const words = line.trim().split(/\s+/);
  if (words.length < PT.ABOUT_FIRST + 1) continue;
  const run = new ScrimInstance(null, null, null);
  for (let i = 0; i < PT.ABOUT_FIRST; i += 1) run.SetFloatParameter(i, Number(words[i]));
  const width = Number.parseInt(words[PT.ABOUT_FIRST], 10);
  const s = Plan(run.asked(), width, 0, 0.0);
  const a = s.asked;
  let text = `${a.linear ? 1 : 0}`;
  for (const v of [a.opacity, a.front, a.fill, a.back, a.wash, a.spill, a.audience, a.open, ...a.thread, a.meshMm]) text += put(v);
  text += ` ${a.drawMesh ? 1 : 0}`;
  text += put(a.widthM);
  for (const v of [...s.reflect, s.frontGain, s.back, s.wash, s.spillGain, s.magnification, s.apparent, s.invApparent, s.radiusPx]) text += put(v);
  text += ` ${s.radiusCapped ? 1 : 0}`;
  for (const v of [s.pitchPx, ...s.freq]) text += put(v);
  text += ` ${s.freqHi[0]} ${s.freqHi[1]}`;
  for (const v of [...s.freqLo, ...s.openAxis, s.aperture]) text += put(v);
  out.push(text);
}
process.stdout.write(`${out.join('\n')}\n`);
