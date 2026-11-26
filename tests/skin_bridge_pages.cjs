// Exercise the actual handlers in both bundled pages with a headless JS bridge.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');

async function verify(page) {
  const html = fs.readFileSync(path.join(__dirname, '..', page), 'utf8');
  const script = html.match(/<script>([\s\S]*?)<\/script>/)[1];
  new vm.Script(script, {filename: page});
  const handlers = ['skinContextKey', 'refreshSkinPreviews', 'pollSkins', '_loadHomeSkin', 'planLocal',
    'loadSkinProfile', 'applySkin', 'renderCurrent', 'renderLocalCapes', 'setCape'];
  const source = handlers.map(name => script.match(new RegExp(`^(?:async )?function ${name}\\([^]*?^}`, 'm'))[0]).join('\n');

  const calls = [], models = [], reports = [];
  const state = {active_account: {name: 'Alice', uuid: 'alice-uuid', type: 'offline'}, active_instance: {name: 'A'}};
  let local = {success: true, backend: 'critclient', name: 'Alice', variant: 'slim', skin: 'pixels', capes: []};
  let plan = {success: true, installed: ['A'], installable: [{name: 'Neo'}], total: 2, blocked: []};
  let pending, reset, refreshes = 0, detections = 0;
  const stage = {isConnected: true, appendChild() {}}, card = {isConnected: true};
  const ctx = vm.createContext({
    STATE: state, SK: {token: 0, capeToken: 0, tab: 'skins'}, HOME_SKIN: {token: 0}, LIVE_SKIN: {revision: 0},
    currentPage: 'skins', document: {hidden: false, createElement: () => ({})},
    $: selector => selector === '#skin-local-reset' ? {addEventListener: (_, fn) => { reset = fn; }} :
      selector === '#skin-current' ? card : stage,
    skinOffline: () => state.active_account.type === 'offline', icon: () => '', esc: String,
    isFc: () => false, liveNames: () => ['Alice'], showLocalSkinWarning() {}, lockSkins() {}, toast() {},
    detectSlim: () => { detections++; return false; }, loadImage: async () => ({}),
    mount3D: (_, img, options) => models.push(options.slim),
    askInstallSupport: async list => list.map(i => i.name), reportLocal: r => { reports.push(r); return r.success; },
    callApi: async (method, ...args) => {
      calls.push(JSON.parse(JSON.stringify([method, ...args])));
      switch (method) {
        case 'local_skin_plan': return plan;
        case 'local_skin_targets': return {success: true, instances: [], blocked: []};
        case 'local_skin_state': return pending || {...local};
        case 'get_live_skins': return {success: true, faces: {}, active: {image: local.skin, variant: local.variant}};
        case 'get_skin_profile': return {success: true, skin: null, capes: [], name: 'Alice'};
        case 'local_set_skin': case 'local_set_cape': case 'local_reset': case 'set_skin': case 'set_cape':
          return {success: true, results: [{name: 'A', ok: true, downloaded: false}]};
        default: throw Error(`Unexpected ${page} bridge call: ${method}`);
      }
    },
  });
  vm.runInContext(source, ctx, {filename: page});
  const render = ctx.renderCurrent, refresh = ctx.refreshSkinPreviews;
  ctx.renderCurrent = () => {};
  ctx.refreshSkinPreviews = () => { refreshes++; };
  ctx.loadHomeSkin = () => {};

  await ctx.applySkin('pixels', 'slim', 'example');
  assert.deepEqual(calls.slice(-2), [['local_skin_plan', 'skin'], ['local_set_skin', 'pixels', ['Neo'], 'slim']]);
  await ctx.loadSkinProfile();
  await ctx._loadHomeSkin(true);
  assert.deepEqual(calls.slice(-2), [['local_skin_state', 'A'], ['local_skin_state', 'A']]);
  assert.equal(ctx.SK.profile.variant, 'slim');
  assert.equal(models.at(-1), true);
  assert.equal(detections, 0);

  let release;
  pending = new Promise(resolve => { release = resolve; });
  const oldProfile = ctx.loadSkinProfile(), oldHome = ctx._loadHomeSkin(true);
  state.active_instance = {name: 'B'};
  pending = null;
  local = {...local, skin: null, variant: null};
  await ctx.loadSkinProfile();
  await ctx._loadHomeSkin(false);
  release({...local, skin: 'stale pixels', variant: 'slim'});
  await Promise.all([oldProfile, oldHome]);
  assert.equal(ctx.SK.profile.skin, null);
  assert.match(stage.innerHTML, /Default skin/); // no remote Image implementation in this context
  assert.deepEqual(calls.slice(-2), [['local_skin_state', 'B'], ['local_skin_state', 'B']]);

  await render();
  const beforeReset = refreshes, beforeReport = reports.length;
  await reset();
  assert.deepEqual(calls.at(-1), ['local_reset']);
  assert.equal(reports.length, beforeReport + 1);
  assert.equal(refreshes, beforeReset + 1);

  const beforePoll = refreshes;
  local = {...local, skin: 'same pixels', variant: 'classic'};
  await ctx.pollSkins(); await ctx.pollSkins();
  local.variant = 'slim'; await ctx.pollSkins();
  local.skin = null; local.variant = null; await ctx.pollSkins();
  assert.equal(refreshes, beforePoll + 3);
  local = {...local, backend: 'csl', skin: 'legacy pixels', variant: null};
  await ctx.loadSkinProfile(); await ctx._loadHomeSkin(true);
  assert.equal(detections, 2);
  assert.equal(ctx.SK.profile.variant, 'classic');

  ctx.SK.tab = 'capes';
  await ctx.renderLocalCapes(card);
  assert.match(card.innerHTML, /No eligible CustomSkinLoader instances/);
  assert.deepEqual(calls.at(-1), ['local_skin_targets', 'cape']);
  plan = {...plan, total: 0, installed: [], installable: []};
  await ctx.setCape('migrator', 'equip');
  assert.deepEqual(calls.at(-1), ['local_skin_plan', 'cape']);
  plan = {...plan, total: 1, installed: ['Neo']};
  ctx.SK.tab = 'skins';
  await ctx.setCape('migrator', 'equip');
  assert.ok(calls.some(c => JSON.stringify(c) === '["local_set_cape","migrator",[]]'));

  for (const type of ['microsoft', 'ely']) {
    state.active_account.type = type;
    await ctx.applySkin('auth pixels', 'classic');
    await ctx.setCape('cape', 'equip');
    assert.ok(calls.some(c => JSON.stringify(c) === '["set_skin","auth pixels","classic"]'));
    assert.ok(calls.some(c => JSON.stringify(c) === '["set_cape","cape"]'));
    assert.equal(calls.at(-1)[0], 'get_skin_profile');
  }
  assert.match(script, /window\.addEventListener\("focus", refreshSkinPreviews\)/);
  let home = 0, profile = 0;
  ctx.loadHomeSkin = () => { home++; }; ctx.loadSkinProfile = () => { profile++; };
  refresh(); assert.deepEqual([home, profile], [1, 1]);
}

Promise.all(['index.html', 'index.linux.html'].map(verify)).catch(err => { console.error(err); process.exitCode = 1; });
