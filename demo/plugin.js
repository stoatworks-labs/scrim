/**
 * SW Scrim (SM01, an FFGL mixer), in a browser.
 *
 * A theatre gauze: this layer is what the projector sends onto the gauze
 * (Src), the layer below is the scene behind it (Dest), and what the audience
 * sees is the light the threads reflect plus the light that comes through the
 * holes.
 *
 * What runs here, and what is a port:
 *
 * - **The shaders are the plugin's.** `shaders.js` is spliced from
 *   source/Shaders.cpp by demo/tools/check_shaders.py --write, unedited, and
 *   assembled as Scrim.cpp's InitGL assembles them (port.js). The kit's port()
 *   changes the version line and adds precision qualifiers; nothing else.
 *   tools/verify.sh fails if a character drifts.
 * - **The CPU half is a port**, in port.js: Controls.cpp, Model.cpp's Plan (in
 *   double, as the C++; a JavaScript number is a double) and Scrim.cpp's
 *   constructor, asked() and ProcessOpenGL. demo/tools/crosscheck/ compares
 *   this page's frames with `smtest --pipe --pipe-src` on the same two inputs,
 *   frame for frame, and port_numbers.py the port's numbers with the C++'s;
 *   past those, only a reader checks it.
 *
 * The two inputs are the page's, not the plugin's: the kit's generated clips
 * (one renderer each), a black layer, or a picture of the visitor's as the
 * scene behind.
 */
import { mountDemo } from './vendor/demo.js';
import { PassBuffer } from './vendor/gl.js';
import { SOURCES, SourceRenderer } from './vendor/sources.js';
import {
  PT, SCRIM_TABLE, ScrimDefaults, ScrimInstance, compileScreen, kMixerId, kMixerName,
  LightFromParam, ParamForLight, FillFromParam, SpillFromParam, ParamForSpill, AudienceFromParam, ParamForAudience,
  MeshFromParam, ParamForMesh, WidthFromParam, ParamForWidth,
} from './port.js';

const f32 = Math.fround;
const clamp = (value, lo, hi) => Math.min(hi, Math.max(lo, value));

//===========================================================================
// The page's inputs. Not the plugin's, which is handed whatever two layers
// the operator gives it.
//===========================================================================

const BLACK = 'black';
const KIT_CLIPS = ['scene', 'grid', 'spot', 'bars', 'bars100', 'ramp', 'detail', 'alpha'];
// Src, the projection: the kit's clips and a black layer (the projector sending nothing).
const PROJECTIONS = [
  ...KIT_CLIPS.map((id) => {
    const clip = SOURCES.find((s) => s.id === id);
    return { id, name: clip.name, hint: clip.hint };
  }),
  { id: BLACK, name: 'Black (the projector sends nothing)', hint: 'An opaque black layer: nothing on the gauze but the Wash, and nothing spilling onto the set.' },
];
const PROJECTION_DEFAULT = 'grid';
// Dest, the scene behind: the kit's clip picker, with a black layer added (a dark set).
const SCENES = ['scene', ...KIT_CLIPS.filter((id) => id !== 'scene'), BLACK];

/** An opaque black layer at a size: smtest's `black` card. */
class BlackLayer {
  constructor(gl) {
    this.gl = gl;
    this.buffer = new PassBuffer(gl, { filter: 'linear' });
    this.size = '';
  }

  render(width, height) {
    const key = `${width}x${height}`;
    if (this.size !== key) {
      this.buffer.ensure(width, height, this.gl.RGBA8);
      this.buffer.clearTo(0, 0, 0, 1);
      this.gl.bindFramebuffer(this.gl.FRAMEBUFFER, null);
      this.size = key;
    }
    return { texture: this.buffer.texture, width, height, fbo: this.buffer.fbo };
  }
}

/** One input: a kit clip or the black layer, at a size and a time. */
class Layer {
  constructor(gl, quad) {
    this.clips = new SourceRenderer(gl, quad);
    this.black = new BlackLayer(gl);
  }

  render(id, width, height, time) {
    if (id === BLACK) return this.black.render(width, height);
    const clip = SOURCES.find((s) => s.id === id) ?? SOURCES[0];
    const out = this.clips.render(clip, width, height, time);
    return { ...out, fbo: this.clips.buffer.fbo };
  }
}

//===========================================================================
// For demo/tools/crosscheck/ and headless checks.
//===========================================================================

/** RGBA8 out of a framebuffer, top row first. */
function readFrame(gl, fbo, width, height) {
  gl.bindFramebuffer(gl.FRAMEBUFFER, fbo);
  const row = width * 4;
  const read = new Uint8Array(row * height);
  gl.pixelStorei(gl.PACK_ALIGNMENT, 1);
  gl.readPixels(0, 0, width, height, gl.RGBA, gl.UNSIGNED_BYTE, read);
  const pixels = new Uint8Array(row * height);
  for (let y = 0; y < height; y += 1) pixels.set(read.subarray(y * row, (y + 1) * row), (height - 1 - y) * row);
  gl.bindFramebuffer(gl.FRAMEBUFFER, null);
  return pixels;
}

/** The uniforms the program declares that nothing has set: each would sit at 0. */
function unsetUniforms(gl, program) {
  const set = new Set([...program.locations].filter(([, loc]) => loc !== null).map(([name]) => name));
  const out = [];
  const count = gl.getProgramParameter(program.program, gl.ACTIVE_UNIFORMS);
  for (let i = 0; i < count; i += 1) {
    const name = gl.getActiveUniform(program.program, i).name;
    if (!set.has(name) && !set.has(name.replace(/\[0\]$/, ''))) out.push(name);
  }
  return out;
}

function installHook(gl, quad, program) {
  window.scrimDemo = {
    table: SCRIM_TABLE.map((row) => ({ name: row.name, kind: row.kind, group: row.group, value: ScrimDefaults()[row.slot] })),
    clips: PROJECTIONS.map((p) => p.id),
    defaults: { projection: PROJECTION_DEFAULT, scene: SCENES[0] },

    /**
     * A FRESH instance (smtest's is one too) with the settings by the
     * plugin's display name, in host values. Each frame() renders the two
     * clips at their times, reads both back as the host would hand them on
     * (RGBA8, top row first), runs ProcessOpenGL into the canvas and reads
     * that back: one task, so the page's own frames cannot interleave.
     */
    begin({ settings = {}, width, height, srcWidth, srcHeight, dest, src }) {
      const run = new ScrimInstance(gl, quad, program);
      const destLayer = new Layer(gl, quad);
      const srcLayer = new Layer(gl, quad);
      const sw = srcWidth ?? width;
      const sh = srcHeight ?? height;
      const apply = (values) => {
        for (const [name, value] of Object.entries(values)) {
          const info = SCRIM_TABLE.find((row) => row.name === name);
          if (!info) throw new Error(`no parameter called '${name}'`);
          const number = Number(value);
          if (!Number.isFinite(number)) throw new Error(`'${value}' is not a number for ${name}`);
          run.SetFloatParameter(info.slot, number);
        }
      };
      apply(settings);
      return {
        frame(time, changes = {}, { srcTime = time, inputs = true } = {}) {
          apply(changes);
          const d = destLayer.render(dest, width, height, time);
          const s = srcLayer.render(src, sw, sh, srcTime);
          gl.canvas.width = width;
          gl.canvas.height = height;
          gl.bindFramebuffer(gl.FRAMEBUFFER, null);
          gl.viewport(0, 0, width, height);
          gl.disable(gl.BLEND);
          gl.clearColor(0, 0, 0, 0);
          gl.clear(gl.COLOR_BUFFER_BIT);
          const ok = run.ProcessOpenGL(d, s, width, height, null);
          const pixels = readFrame(gl, null, width, height);
          return {
            ok,
            pixels,
            destPixels: inputs ? readFrame(gl, d.fbo, width, height) : null,
            srcPixels: inputs ? readFrame(gl, s.fbo, sw, sh) : null,
            error: gl.getError(),
          };
        },
        end() {
          destLayer.clips.dispose();
          srcLayer.clips.dispose();
          destLayer.black.buffer.dispose();
          srcLayer.black.buffer.dispose();
        },
      };
    },

    uniforms() {
      return { missing: [...program.missing], unset: unsetUniforms(gl, program) };
    },
  };
}

//===========================================================================
// The parameters: the plugin's own, in its declaration's order and groups,
// with its names and defaults. Hints from the README and Controls.h.
//===========================================================================

const V = (v) => f32(v);
const light = (v) => LightFromParam(V(v)).toFixed(2);
const ARENA_INDEX0 = 'Resolume Arena does not show a mixer’s first parameter (measured on genlock, wipe, relay, lenticular, pepper and penumbra, not yet on SW Scrim), so in Arena this stays on. A browser does not hide it.';

const SPEC = [
  ['linearLight', PT.LINEAR_LIGHT, null, `On: the light is summed in linear light: both clips and the Thread colour are decoded from sRGB, lit, summed and encoded again. Off does the same arithmetic on the gamma-encoded values, which is brighter for the same settings. Parameter 0: ${ARENA_INDEX0}`],
  ['opacity', PT.OPACITY, (v) => `×${clamp(V(v), 0, 1).toFixed(2)}`, 'The projector’s level, 0 to 1: it scales the projection on the gauze and its spill on the set alike. In Resolume Arena a mixer parameter named Opacity is bound to the layer’s opacity fader (measured on genlock, wipe, relay and penumbra, not yet on SW Scrim), so the projection layer’s fader is the projector. Here it is a slider.'],
  ['frontLight', PT.FRONT_LIGHT, light, '0 to 16, as 16 p²: the projector at full, in units of what a white diffuser shows (Front Light 1 on a white sheet with a white projection is white). The default, 12.27, is the light at which the median of Resolume’s 33 demo clips fills the range on the gauze with nothing behind; with Back Light at its default too, the picture clips.'],
  ['pixelFill', PT.PIXEL_FILL, (v) => `${(FillFromParam(V(v)) * 100).toFixed(1)}%`, 'The projector’s mirror fill factor, 50% to 100% (TI’s DLP7000: 92%). Each output pixel’s light falls on a centred square of that share of the pixel, and that is what the weave beats against. With Draw Mesh off it changes nothing.'],
  ['backLight', PT.BACK_LIGHT, light, '0 to 16, as 16 p²: the light on the scene behind. The layer below is taken as the scene lit at Back Light 1, seen through the holes. At 0 a dark backstage stays invisible, whatever the layer below holds, except where the projection’s own spill lights it. The default, 8.03, is where the median demo clip fills the range seen through the gauze with nothing projected.'],
  ['wash', PT.WASH, light, '0 to 16, as 16 p²: a steep white light on the gauze itself. It lights the threads and never the set, so a washed gauze looks solid.'],
  ['spillDistance', PT.SPILL_DISTANCE, (v) => `k ${SpillFromParam(V(v)).toFixed(2)} throws`, 'How far behind the gauze the set stands, 0 to 3 throws (one throw is the projector’s distance from the gauze). The projection’s light that passes the holes lands on the set magnified by 1 + k, spread over (1 + k)² the area and out of focus by the lens’s circle of confusion. At 0 the set is on the gauze.'],
  ['audience', PT.AUDIENCE, (v) => `v ${AudienceFromParam(V(v)).toFixed(2)} throws`, 'How far in front of the gauze the audience sits, 0.25 to 16 throws, as 0.25 × 64^p. The audience sees the spill magnified by (1 + k) / (1 + k / v): at 1 they see from where the projector is, and the spill hides behind its own image. With Spill Distance 0 it changes nothing.'],
  ['openArea', PT.OPEN_AREA, (v) => `φ ${clamp(V(v), 0, 1).toFixed(3)}`, 'φ, the holes’ share of the gauze, 0 to 1 (ShowTex GobelinTulle: 52–55%). The threads take the rest and reflect the front light; the holes pass the light from behind.'],
  ['thread', PT.THREAD_RED, null, 'The threads’ reflectance, picked as an sRGB colour and decoded to linear light with Linear Light on. The default, 0.8822 on all three, is ρ 0.753: ShowTex’s luminance factor for white GobelinTulle, 0.35, over the threads’ share, 0.465. The swatch is 8 bits a channel, as an HTML colour input is; the plugin takes any float.'],
  ['threadGreen', PT.THREAD_GREEN, null, ''],
  ['threadBlue', PT.THREAD_BLUE, null, ''],
  ['mesh', PT.MESH, (v) => `${MeshFromParam(V(v)).toFixed(2)} mm`, 'The weave’s cell, 0.5 to 10 mm wide as 0.5 × 20^p; its height is half (ShowTex: 2.5 × 1.25 mm). With Gauze Width it sets how many cells fall in a pixel, and so the moiré against the projector’s pixels. With Draw Mesh off it changes nothing.'],
  ['drawMesh', PT.DRAW_MESH, null, 'On: the weave per pixel, which beats against the projector’s pixels. Off: its average, φ, everywhere.'],
  ['gauzeWidth', PT.GAUZE_WIDTH, (v) => `${WidthFromParam(V(v)).toFixed(2)} m`, 'The gauze image’s width, 2 to 20 m, as 2 × 10^p (ShowTex sells GobelinTulle 950 cm wide). It sets the mesh’s and the lens’s size in pixels: on a narrow gauze the weave is coarser and the spill’s disc wider.'],
];

const PARAMS = SPEC.map(([id, slot, display, hint]) => {
  const types = { boolean: 'boolean', standard: 'standard', red: 'colour', green: 'colour', blue: 'colour' };
  const row = SCRIM_TABLE[slot];
  const p = { id, slot, name: row.name, type: types[row.kind], default: ScrimDefaults()[slot], group: row.group, hint };
  if (display) p.display = display;
  return p;
});
if (PARAMS.some((p, i) => p.slot !== i)) throw new Error('the panel is not in the plugin’s order');

//===========================================================================
// The page.
//===========================================================================

const query = new URLSearchParams(window.location.search);
const REPO = 'https://github.com/stoatworks-labs/scrim';

const page = { mounted: null, instance: null, stats: '' };

/** The line under the picture, from the frame's numbers. Not the plugin's. */
function statsFor(s, width) {
  const a = s.asked;
  const phi = clamp(a.open, 0, 1);
  const rho = (s.reflect[0] + s.reflect[1] + s.reflect[2]) / 3;
  const parts = [];
  parts.push(a.drawMesh
    ? `the weave: a ${a.meshMm.toFixed(2)} mm cell is ${s.pitchPx.toFixed(3)} px at ${width} px wide, ${s.freq[0].toFixed(3)} cycles a pixel across and ${s.freq[1].toFixed(3)} down`
    : `the weave averaged (Draw Mesh off): φ ${phi.toFixed(3)} everywhere`);
  parts.push(`the set ×${s.magnification.toFixed(2)}, seen at ×${s.apparent.toFixed(3)}`);
  parts.push(s.radiusPx < 1 / 64 ? 'the spill’s disc a point' : `the spill’s disc ${s.radiusPx.toFixed(2)} px in radius${s.radiusCapped ? ' (capped)' : ''}`);
  parts.push(`on average the projection ×${(rho * (1 - phi) * s.frontGain).toFixed(2)} and the scene ×${(phi * s.back).toFixed(2)}${a.linear ? ' in linear light' : ' on the codes'}`);
  return parts.join(' · ');
}

function createRenderer(gl, quad) {
  const program = compileScreen(gl);
  const instance = new ScrimInstance(gl, quad, program);
  const projection = new Layer(gl, quad);
  const blackScene = new BlackLayer(gl);
  page.instance = instance;
  installHook(gl, quad, program);

  return {
    render({ input, params, width, height, time, variant }) {
      // The host restates every parameter each frame.
      for (const p of PARAMS) instance.SetFloatParameter(p.slot, params.get(p.id));

      // Dest, the layer below: the scene behind. The kit has already drawn
      // its clip (or the visitor's picture) into `input`; the black layer is
      // the page's own.
      const dest = page.mounted?.state?.sourceId === BLACK ? blackScene.render(width, height) : input;
      // Src, this layer: the projection, at the output's raster and the same time.
      const src = projection.render(variant ?? PROJECTION_DEFAULT, width, height, time);

      gl.bindFramebuffer(gl.FRAMEBUFFER, null);
      gl.viewport(0, 0, width, height);
      gl.disable(gl.BLEND);

      instance.ProcessOpenGL(dest, src, width, height, null);
      if (instance.lastSetup) page.stats = statsFor(instance.lastSetup, width);
    },
  };
}

const COMMON_GAPS = {
  es: 'GLSL ES 3.00 in WebGL2, not desktop GL 4.1 core. The shader text is the plugin’s; only the version line and precision qualifiers are changed by the kit. Every position is an integer from gl_FragCoord and every input a texelFetch at integer texels, so no interpolation or texture filter is involved; what can differ on another GPU is the rounding of pow in the sRGB pair, of the division in the weave’s box integrals and of the spill’s sums, a few float ulps, which in 8 bits is now and then one level.',
  presets: 'The presets are the page’s. The plugin declares none; each is only a combination of the plugin’s own parameters.',
  about: 'The About block — a text line and link buttons for the host’s panel — is absent: a web page has links of its own. So are the plugin’s log file and the harness’s negative controls (the Fault uniform is 0, as shipped). There is no audio path, so nothing here is missing for want of one.',
};

const mounted = mountDemo({
  name: kMixerName,
  kind: 'mixer',
  pluginId: kMixerId,
  tagline:
    'A theatre gauze as an FFGL mixer: this layer is what the projector sends onto the gauze, the layer below is the scene behind it, and what the audience sees is the light the threads reflect plus the light that comes through the holes. With the front lit and the back dark the gauze is a screen; light the back and darken the front and it all but disappears; in between you see both. The projection’s light that passes the holes lands on the set behind, bigger, dimmer and out of focus, and the weave beats against the projector’s pixels.',
  repo: REPO,
  blurb:
    'It is SW Scrim’s own GLSL — its one pass, ported from the repository to WebGL2 unedited — run by a JavaScript port of the plugin’s CPU half: the frame’s numbers (the lights, the spill’s magnifications and disc, the weave’s frequencies, in double as the plugin works them out) and every conversion. SW Scrim is a mixer, so it needs two pictures: the projection (Src, this layer) and the scene behind (Dest, the layer below), each one of the kit’s generated clips or a black layer, and the scene can be a picture of your own. In Resolume the projection layer’s fader would be Opacity; here it is a slider.',
  sources: SCENES,
  variants: {
    label: 'Projection',
    default: PROJECTION_DEFAULT,
    options: PROJECTIONS.map((p) => ({ id: p.id, name: p.name, hint: `Src, this layer, what the projector sends: ${p.hint}` })),
  },
  params: PARAMS,
  // The plugin declares no presets. These are the page's: combinations of
  // the plugin's own parameters and nothing else.
  presets: {
    'A screen: the front lit, the back dark (Back Light 0)': { backLight: 0 },
    'The reveal: the back lit, the projector off (Front Light 0)': { frontLight: 0 },
    'Gentler lights (Front Light 2.8, Back Light 1.9)': { frontLight: ParamForLight(2.8), backLight: ParamForLight(1.9) },
    'A wash on the gauze, the projector off (Wash 2, Front Light 0)': { wash: ParamForLight(2.0), frontLight: 0 },
    'The spill alone: the set lit through the holes (Back Light 0, Spill Distance 1.5)': { backLight: 0, spillDistance: ParamForSpill(1.5) },
    'The audience at the projector (Audience 1): the spill hides behind its image': { backLight: 0, spillDistance: ParamForSpill(1.5), audience: ParamForAudience(1.0) },
    'A narrow gauze, a coarse mesh: the weave shows (Gauze Width 2 m, Mesh 10 mm)': { gauzeWidth: ParamForWidth(2.0), mesh: ParamForMesh(10.0) },
    'The widest disc: a 2 m gauze, the set 3 throws back (Back Light 0)': { gauzeWidth: ParamForWidth(2.0), spillDistance: ParamForSpill(3.0), backLight: 0 },
    'The weave’s average (Draw Mesh off)': { drawMesh: 0 },
    'Black threads: nothing on the gauze reflects (Thread black)': { thread: 0, threadGreen: 0, threadBlue: 0 },
    'Gamma-space arithmetic (Linear Light off)': { linearLight: 0 },
  },
  differences: [
    'Two inputs, both generated here. The kit this page is built on hands a demo one input; SW Scrim is a mixer. The scene behind (Dest, the layer below) is the kit’s clip picker, renamed Scene behind, with a black layer added (a dark set), and “Use my own…” replaces it with a picture or video of yours, decoded in the page and never uploaded. The projection (Src, this layer) is the Projection dropdown: the same kit clips, drawn by a second renderer at the output’s size and at the same time, or black.',
    'The kit’s clips are premultiplied, and so is a picture of your own once the browser has decoded it; the plugin reads both inputs as straight alpha (whether Resolume hands a mixer straight or premultiplied textures is one of the plugin’s open questions). Only the “Shape on transparency” clip and a picture with transparency have any alpha below 1, and there the plugin sees the colour times the alpha twice, so their soft edges come out darker than a straight clip’s would.',
    'Opacity and Linear Light are on the panel. In Resolume Arena a mixer parameter named Opacity is bound to the layer’s opacity fader, and a mixer’s first parameter — Linear Light here, which is why it is first and on — is hidden. Both were measured on other mixers (genlock, wipe, relay, lenticular, pepper and penumbra), not yet on SW Scrim.',
    'The frame’s numbers are a port. The shader is the plugin’s, unedited. What feeds it is translated to JavaScript by hand in demo/port.js: Controls.cpp’s conversions, Model.cpp’s Plan — the lights’ gains, the spill’s magnifications (1 + k for the set, (1 + k)/(1 + k/v) for the audience) and its defocus disc from the lens’s pupil, and the weave’s frequencies — and Scrim.cpp’s ProcessOpenGL, with every uniform. Plan computes in double; a JavaScript number is a double, so the port does the same operations in the same precision, including the split of the weave’s phase into an exact 8-bit part (FreqHi) and a remainder (FreqLo), and only the upload to the shader rounds to float, as the plugin’s does. It is checked by a reader and by the crosscheck (demo/tools/crosscheck/: this page’s frames against the plugin’s own harness, smtest --pipe, on the same two inputs, frame for frame, and the port’s numbers against the C++’s), not by the plugin’s own harness, which has never heard of this page. demo/tools/check_shaders.py fails the repository’s verify script if a character of the shader drifts, or if any C++ the port was translated from changes.',
    'The hardware sizes are the used sizes. Resolume hands a mixer padded textures (720 rows in a 768-row texture, say); the plugin reads each input by texelFetch inside its used size and never reads the padding, and the plugin’s --mixer check is what shows that. Here the textures are unpadded, so the padding is never there to read.',
    'Both inputs are at the output’s size, so the plugin’s hand bilinear (a resized input’s light, resampled by hand) runs only for a picture of your own once the Composition is changed from its size. The crosscheck runs it on a projection at three quarters of the output’s size.',
    'The clock is the page’s, and only the clips use it. The plugin keeps no state and has no clock at all; Pause, Step and Restart move the kit’s clips, never the gauze.',
    COMMON_GAPS.es,
    'The line under the picture is not the plugin. It reports what the port computed for this frame — the weave’s frequency in cycles a pixel, the set’s magnification and the audience’s, the spill disc’s radius in pixels, and the average gain on each input — and the plugin draws no such thing.',
    COMMON_GAPS.presets,
    COMMON_GAPS.about,
    'Nothing here is measured. The plugin’s numerical proof — with nothing behind lit, the projection comes back unchanged to the bit; a dark backstage changes no float at any open area; the spill’s edges where similar triangles put them to within a fraction of a pixel and its disc the lens’s circle of confusion; 17 wrong versions and 39 one-character mutants each caught — is tools/smtest in the repository, and that harness, not this page, is the reason to believe the gauze. It checks the model, not a real gauze: no photograph of a scrim was compared.',
  ],
  showBackdrop: false,
  createRenderer,
});

page.mounted = mounted ?? null;

//---------------------------------------------------------------------------
// The kit labels its clip picker "Clip". With two inputs that is ambiguous,
// so it is renamed for what it feeds, the scene behind (Dest), and the black
// layer is added to it: the kit lists only its own SOURCES, skips an id it
// does not know, and accepts any id in `sources` from ?clip=, so the page's
// id rides through state.sourceId and the renderer swaps in the black layer
// (the kit still draws a clip into `input` underneath, unused). The variant
// dropdown is the projection (Src); `?projection=<id>` picks it, the way the
// kit's `?clip=` picks the scene.
//---------------------------------------------------------------------------
for (const label of document.querySelectorAll('.transport__label')) {
  if (label.textContent === 'Clip') {
    label.textContent = 'Scene behind';
    label.title = 'Dest, the layer below: the scene behind the gauze, as lit at Back Light 1.';
    const select = label.closest('.transport__field')?.querySelector('select');
    if (select) {
      const option = new Option('Black (a dark set)', BLACK);
      option.title = 'An opaque black layer: nothing behind the gauze to light, so only the threads show.';
      select.append(option);
      select.value = mounted?.state?.sourceId ?? SCENES[0];
    }
  }
  if (label.textContent === 'Projection') {
    label.title = 'Src, this layer: what the projector sends onto the gauze.';
  }
}
const fileLabel = document.querySelector('.transport__file');
if (fileLabel) fileLabel.title = 'A picture or a video from this machine as the scene behind (Dest). It is decoded in the page and never uploaded anywhere.';

const wantedProjection = query.get('projection');
if (mounted?.state && wantedProjection && PROJECTIONS.some((p) => p.id === wantedProjection)) {
  mounted.state.variant = wantedProjection;
  const select = [...document.querySelectorAll('.transport__label')]
    .find((l) => l.textContent === 'Projection')?.closest('.transport__field')?.querySelector('select');
  if (select) select.value = wantedProjection;
}

// The line under the picture.
const statLine = document.createElement('p');
statLine.className = 'stage__status';
statLine.setAttribute('aria-live', 'off');
statLine.dataset.role = 'scrim-stats';
document.querySelector('.stage')?.append(statLine);
if (statLine.isConnected) {
  const tick = () => {
    if (statLine.textContent !== page.stats) statLine.textContent = page.stats;
    requestAnimationFrame(tick);
  };
  requestAnimationFrame(tick);
}

// For a headless check: the mounted page.
window.scrimPage = page;
