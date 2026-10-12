const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const crypto = require('node:crypto');
const { publicationState, planReleases, checkPublication, publishRelease, reconcileLatest } = require('../.github/scripts/releases.cjs');
const root = path.resolve(__dirname, '..');
const sha = 'b'.repeat(40), oldSha = 'a'.repeat(40);

function fixture(t, { tag = 'v1.2', fresh = false, label = `source:${oldSha}` } = {}) {
  const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'powershift-release-test-'));
  t.after(() => fs.rmSync(directory, { recursive: true, force: true }));
  const zipPath = path.join(directory, 'release.zip');
  fs.writeFileSync(zipPath, Buffer.from('bytes from the validated build'));
  const name = `PowerShift-V${tag.slice(1)}-x64.zip`;
  const body = fs.readFileSync(path.join(root, '.github/release-notes', `${tag}.md`), 'utf8').trim();
  const context = { repo: { owner: 'test', repo: 'PowerShift' }, eventName: 'push', ref: `refs/tags/${tag}`, runId: 50 };
  const state = { releases: fresh ? [] : [{ id: 1, tag_name: tag, name: `PowerShift V${tag.slice(1)}`,
    body: 'old notes', draft: false, prerelease: false,
    assets: [{ id: 10, name, label, state: 'uploaded', size: 100 }] }],
    tags: [{ name: tag, commit: { sha } }], latest: fresh ? null : 1, nextId: 100, calls: [], failures: [] };
  const notFound = () => { const error = new Error('Not found'); error.status = 404; throw error; };
  const fail = action => {
    state.calls.push(action);
    if (state.failures[0] === action) { state.failures.shift(); throw new Error(`Injected ${action} failure`); }
  };
  const repos = {
    listTags: 'tags', listReleases: 'releases', listReleaseAssets: 'assets',
    getCommit: async input => ({ data: { sha: state.tags.find(item => item.name === input.ref)?.commit.sha } }),
    getReleaseByTag: async input => ({ data: structuredClone(state.releases.find(item => item.tag_name === input.tag) || notFound()) }),
    createRelease: async input => {
      fail('create'); const release = { ...input, id: ++state.nextId, assets: [] };
      state.releases.push(release); return { data: structuredClone(release) };
    },
    uploadReleaseAsset: async input => {
      fail('upload'); const asset = { id: ++state.nextId, name: input.name, label: input.label,
        size: input.data.length, state: 'uploaded', digest: `sha256:${crypto.createHash('sha256').update(input.data).digest('hex')}` };
      state.releases.find(item => item.id === input.release_id).assets.push(asset);
      return { data: structuredClone(asset) };
    },
    updateReleaseAsset: async input => {
      fail(input.name === name ? (input.asset_id === 10 ? 'rollback' : 'promote') : 'backup');
      const asset = state.releases.flatMap(item => item.assets).find(item => item.id === input.asset_id);
      Object.assign(asset, { name: input.name }, input.label ? { label: input.label } : {});
      return { data: structuredClone(asset) };
    },
    deleteReleaseAsset: async input => {
      fail('delete'); for (const release of state.releases) release.assets = release.assets.filter(item => item.id !== input.asset_id);
    },
    updateRelease: async input => {
      fail(input.body === undefined ? 'latest' : 'notes');
      const release = state.releases.find(item => item.id === input.release_id);
      if (input.body !== undefined) Object.assign(release, { name: input.name, body: input.body, draft: input.draft, prerelease: input.prerelease });
      if (input.make_latest === 'true') state.latest = release.id;
      return { data: structuredClone(release) };
    },
    getLatestRelease: async () => ({ data: structuredClone(state.releases.find(item => item.id === state.latest) || notFound()) }),
  };
  const github = { rest: { repos }, paginate: async (method, input) => structuredClone(method === 'tags' ? state.tags :
    method === 'releases' ? state.releases : state.releases.find(item => item.id === input.release_id).assets) };
  const args = { github, context, root, tag, sha, replace: true, zipPath };
  const inspect = async () => {
    const outputs = {};
    await checkPublication({ ...args, core: { setOutput: (key, value) => outputs[key] = value } });
    return outputs;
  };
  return { args, state, body, name, inspect };
}

test('new versions publish a verified ZIP before leaving draft state', async t => {
  const f = fixture(t, { fresh: true });
  await publishRelease(f.args);
  const release = f.state.releases[0];
  assert.equal(release.draft, false);
  assert.equal(release.body, f.body);
  assert.equal(release.assets.length, 1);
  assert.equal(release.assets[0].name, f.name);
  assert.equal(release.assets[0].label, `source:${sha}`);
  assert(f.state.calls.indexOf('promote') < f.state.calls.indexOf('notes'));
  assert.deepEqual(await f.inspect(), { publish: 'false', build: 'false' });
});
test('same-version replacement uploads first and preserves the existing Release', async t => {
  const f = fixture(t);
  await publishRelease(f.args);
  assert.equal(f.state.releases[0].id, 1);
  assert.equal(f.state.releases[0].assets.length, 1);
  assert.notEqual(f.state.releases[0].assets[0].id, 10);
  assert(f.state.calls.indexOf('upload') < f.state.calls.indexOf('backup'));
  assert(f.state.calls.indexOf('promote') < f.state.calls.indexOf('delete'));
});
for (const failure of ['delete', 'notes']) test(`${failure} failure is repaired without rebuilding or reuploading`, async t => {
  const f = fixture(t);
  f.state.failures = [failure];
  await assert.rejects(publishRelease(f.args), /Injected/);
  assert.deepEqual(await f.inspect(), { publish: 'true', build: 'false' });
  const uploads = f.state.calls.filter(item => item === 'upload').length;
  await publishRelease({ ...f.args, zipPath: undefined });
  assert.equal(f.state.calls.filter(item => item === 'upload').length, uploads);
  assert.equal(f.state.releases[0].body, f.body);
  assert.equal(f.state.releases[0].assets.length, 1);
  assert.deepEqual(await f.inspect(), { publish: 'false', build: 'false' });
});
test('upload failure leaves the original ZIP available', async t => {
  const f = fixture(t); f.state.failures = ['upload'];
  await assert.rejects(publishRelease(f.args), /Injected upload/);
  assert.equal(f.state.releases[0].assets[0].id, 10);
  assert.equal(f.state.releases[0].assets[0].name, f.name);
});
test('failed promotion restores the old ZIP and remains recoverable', async t => {
  const f = fixture(t); f.state.failures = ['promote'];
  await assert.rejects(publishRelease(f.args), /Injected promote/);
  assert.equal(f.state.releases[0].assets.find(item => item.id === 10).name, f.name);
  await publishRelease(f.args);
  assert.equal(f.state.releases[0].assets.length, 1);
});
test('failed rollback preserves both uploads for a later successful recovery', async t => {
  const f = fixture(t); f.state.failures = ['promote', 'rollback'];
  await assert.rejects(publishRelease(f.args), AggregateError);
  assert.equal(f.state.releases[0].assets.length, 2);
  await publishRelease(f.args);
  assert.equal(f.state.releases[0].assets.length, 1);
  assert.equal(f.state.releases[0].assets[0].name, f.name);
});
test('healthy legacy versions and unrelated attachments are retained', async t => {
  const f = fixture(t, { tag: 'v1.0', label: '' });
  const release = f.state.releases[0]; release.body = f.body;
  release.assets.push({ id: 11, name: `${f.name}.previous-user-backup`, state: 'uploaded', size: 10 });
  assert.equal(publicationState(release, 'v1.0', sha, f.body).complete, true);
  await publishRelease({ ...f.args, replace: false, zipPath: undefined });
  assert.equal(release.assets.length, 2);
  assert.equal(release.assets[0].id, 10);
  assert(!f.state.calls.includes('upload'));
});
test('a tag changed after the build is rejected before uploads or edits', async t => {
  const f = fixture(t); f.state.tags[0].commit.sha = 'c'.repeat(40);
  await assert.rejects(publishRelease(f.args), /changed during publication/);
  assert.equal(f.state.calls.length, 0);
});
test('planning includes unfinished metadata even when the ZIP is ready', async t => {
  const f = fixture(t, { label: `source:${sha}` });
  const outputs = {};
  await planReleases({ ...f.args, core: { setOutput: (key, value) => outputs[key] = value } });
  assert.equal(outputs.pending, 'true');
  assert.equal(JSON.parse(outputs.matrix).include[0].tag, 'v1.2');
  assert.deepEqual(await f.inspect(), { publish: 'true', build: 'false' });
});
test('Latest is recalculated from published runtime ZIPs using numeric versions', async t => {
  const f = fixture(t, { label: `source:${sha}` });
  f.state.tags.push({ name: 'v1.10', commit: { sha } }, { name: 'v2.0', commit: { sha } });
  f.state.releases.push({ id: 2, tag_name: 'v1.10', draft: false, prerelease: false,
    assets: [{ name: 'PowerShift-V1.10-x64.zip', state: 'uploaded', size: 100, label: `source:${sha}` }] });
  await reconcileLatest(f.args);
  assert.equal(f.state.latest, 2); // An unpublished v2.0 must not become the download target.
});
