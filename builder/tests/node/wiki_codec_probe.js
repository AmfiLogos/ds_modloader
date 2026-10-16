// Load the DS wiki builder (data.js + builder.js) in a node vm, to reuse its share-link codec as the REFERENCE for tests.
// (Reference only: the mod decodes in C. The wiki's save generator, savegen.js, is never loaded.)
const fs = require('fs'), vm = require('vm'), path = require('path');
const dir = process.env.DSB_DIR || path.join(process.env.HOME, 'dungeonsettlersdb', 'site', 'builder');
function load() {
  const noop = () => {}, el = new Proxy(function(){}, { get: (t, k) => k === Symbol.toPrimitive ? () => '' : el, apply: () => el, construct: () => el });
  const ctx = { console: { log: noop, warn: noop, error: noop }, setTimeout, clearTimeout, Promise,
                btoa: s => Buffer.from(s, 'binary').toString('base64'), atob: s => Buffer.from(s, 'base64').toString('binary'),
                unescape, escape, encodeURIComponent, decodeURIComponent, Math, JSON, Array, Object, String, Number, isNaN, parseInt, parseFloat, Date,
                document: el, location: { hash: '', search: '', pathname: '/builder/' }, navigator: { userAgent: 'node' }, localStorage: { getItem: () => null, setItem: noop },
                history: { replaceState: noop }, addEventListener: noop, requestAnimationFrame: noop };
  ctx.window = ctx; ctx.global = ctx; ctx.self = ctx;
  vm.createContext(ctx);
  vm.runInContext(fs.readFileSync(path.join(dir, 'data.js'), 'utf8'), ctx);
  try { vm.runInContext(fs.readFileSync(path.join(dir, 'builder.js'), 'utf8'), ctx, { filename: 'builder.js' }); } catch (e) { ctx.__err = String(e).slice(0, 200); }
  return ctx;
}
module.exports = { load };
if (require.main === module) {
  const c = load();
  console.log('builder.js error (expected, DOM):', c.__err);
  console.log('has codec:', typeof c.encodeTeam, typeof c._decodeBlob, typeof c.dsbIdx);
  const X = c.dsbIdx();
  console.log('fp', X.fp, 'sizes', X.RACE.length, X.BG.length, X.IND.length, X.INSC.length, X.TREE.length, X.SKILL.length, X.ITEM.length, X.SLOT.length, X.SOUL.length, X.ACQ.length, X.STAT.length);
  console.log('majors', c.D.majors, 'tiers', c.D.tiers.order, 'slots', c.D.slots);
}
