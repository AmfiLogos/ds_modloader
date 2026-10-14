// Writes data/dsb.json: the DS wiki builder's payload (site/builder/data.js, window.DSB), unchanged.
// The builder's payload IS the game data the builder computes with (it is generated from the game tables of the
// build named in savegen.tag by the wiki pipeline); the engine reads the same rows so the sheet matches the wiki.
//   node gen/gen_tables.js [path/to/site/builder]      (default ~/dungeonsettlersdb/site/builder)
const fs = require('fs'), path = require('path'), vm = require('vm');
const dir = process.argv[2] || path.join(process.env.HOME, 'dungeonsettlersdb', 'site', 'builder');
const ctx = {}; ctx.window = ctx; vm.createContext(ctx);
vm.runInContext(fs.readFileSync(path.join(dir, 'data.js'), 'utf8'), ctx);
const D = ctx.DSB;
if (!D || !D.sv || !D.items) throw new Error('no DSB payload in ' + dir);
const out = path.join(__dirname, '..', 'data', 'dsb.json');
fs.writeFileSync(out, JSON.stringify(D));
process.stderr.write(`dsb.json: game build ${D.savegen && D.savegen.tag} (${D.savegen && D.savegen.siteVer}), ${fs.statSync(out).size} bytes\n`);
