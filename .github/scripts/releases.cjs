const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const isVersion = tag => /^v\d+\.\d+(\.\d+)?$/.test(tag);
const zipName = tag => `PowerShift-V${tag.slice(1)}-x64.zip`;
const normalize = value => (value || '').replace(/\r\n/g, '\n').trim();
function compareVersions(a, b) {
  const left = a.slice(1).split('.').map(Number), right = b.slice(1).split('.').map(Number);
  for (let i = 0; i < 3; ++i) { const difference = (left[i] || 0) - (right[i] || 0); if (difference) return difference; }
  return a.localeCompare(b);
}
function notesFor(root, tag) {
  const body = normalize(fs.readFileSync(path.join(root, '.github', 'release-notes', `${tag}.md`), 'utf8'));
  if (!body) throw new Error(`Missing release notes for ${tag}.`);
  return body;
}
function temporaryAssets(assets, name) {
  const escaped = name.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
  const pattern = new RegExp(`^${escaped}\\.(pending|previous)-\\d+-\\d+$`);
  return assets.filter(asset => pattern.test(asset.name));
}
function publicationState(release, tag, sha, body, explicitReplacement = false) {
  const name = zipName(tag), label = `source:${sha}`;
  const assets = release ? release.assets : [];
  const asset = assets.find(item => item.name === name && item.state === 'uploaded' && item.size > 0);
  const temporary = temporaryAssets(assets, name);
  const replace = explicitReplacement || Boolean(asset && /^source:[0-9a-f]{40}$/.test(asset.label || '') && asset.label !== label) ||
    temporary.some(item => item.name.includes('.pending-') && item.label === label);
  const zipReady = Boolean(asset && (!replace || asset.label === label));
  const metadataReady = Boolean(release && !release.draft && !release.prerelease &&
    release.name === `PowerShift V${tag.slice(1)}` && normalize(release.body) === normalize(body) && !temporary.length);
  return { replace, zipReady, complete: zipReady && metadataReady, temporary };
}
async function releaseFor(github, repo, tag) {
  try { return (await github.rest.repos.getReleaseByTag({ ...repo, tag })).data; }
  catch (error) { if (error.status !== 404) throw error; return null; }
}
async function checkTag(github, repo, tag, sha) {
  const { data: commit } = await github.rest.repos.getCommit({ ...repo, ref: tag });
  if (commit.sha !== sha) throw new Error(`Tag ${tag} changed during publication.`);
}
async function planReleases({ github, context, core, root }) {
  const tags = await github.paginate(github.rest.repos.listTags, context.repo);
  const releases = await github.paginate(github.rest.repos.listReleases, context.repo);
  const pending = tags.filter(item => isVersion(item.name)).sort((a, b) => compareVersions(a.name, b.name)).map(tag => {
    const release = releases.find(item => item.tag_name === tag.name);
    const explicit = context.eventName === 'push' && context.ref === `refs/tags/${tag.name}`;
    const state = publicationState(release, tag.name, tag.commit.sha, notesFor(root, tag.name), explicit);
    return state.complete ? null : { tag: tag.name, sha: tag.commit.sha, replace: state.replace };
  }).filter(Boolean);
  core.setOutput('matrix', JSON.stringify({ include: pending }));
  core.setOutput('pending', String(pending.length > 0));
}
async function checkPublication({ github, context, core, root, tag, sha, replace }) {
  await checkTag(github, context.repo, tag, sha);
  const release = await releaseFor(github, context.repo, tag);
  const state = publicationState(release, tag, sha, notesFor(root, tag), replace);
  core.setOutput('publish', String(!state.complete));
  core.setOutput('build', String(!state.zipReady));
}
async function publishRelease({ github, context, root, tag, sha, replace, zipPath }) {
  const repo = context.repo, name = zipName(tag), label = `source:${sha}`, body = notesFor(root, tag);
  await checkTag(github, repo, tag, sha);
  let release = await releaseFor(github, repo, tag);
  if (!release) release = (await github.rest.repos.createRelease({ ...repo, tag_name: tag,
    name: `PowerShift V${tag.slice(1)}`, body, draft: true, prerelease: false })).data;
  let assets = await github.paginate(github.rest.repos.listReleaseAssets, { ...repo, release_id: release.id });
  const state = publicationState({ ...release, assets }, tag, sha, body, replace);
  if (!state.zipReady) {
    if (!zipPath) throw new Error('A validated ZIP is required for this publication.');
    const bytes = fs.readFileSync(zipPath);
    const digest = `sha256:${crypto.createHash('sha256').update(bytes).digest('hex')}`;
    let staged = state.temporary.find(asset => asset.name.includes('.pending-') && asset.label === label &&
      asset.state === 'uploaded' && asset.size === bytes.length && asset.digest === digest);
    if (!staged) {
      staged = (await github.rest.repos.uploadReleaseAsset({ ...repo, release_id: release.id,
        name: `${name}.pending-${context.runId}-${Date.now()}`, label, data: bytes,
        headers: { 'content-type': 'application/zip', 'content-length': bytes.length } })).data;
      if (staged.state !== 'uploaded' || staged.size !== bytes.length || (staged.digest && staged.digest !== digest))
        throw new Error('Uploaded ZIP verification failed.');
    }
    const existing = assets.find(asset => asset.name === name);
    if (existing) await github.rest.repos.updateReleaseAsset({ ...repo, asset_id: existing.id,
      name: `${name}.previous-${context.runId}-${Date.now()}` });
    try { await github.rest.repos.updateReleaseAsset({ ...repo, asset_id: staged.id, name, label }); }
    catch (error) {
      // If rollback fails, retain both files so a later run can recover.
      if (existing) {
        try { await github.rest.repos.updateReleaseAsset({ ...repo, asset_id: existing.id, name }); }
        catch (rollback) { throw new AggregateError([error, rollback], 'Release replacement needs recovery.'); }
      }
      throw error;
    }
  }
  // Finish metadata/cleanup even when rebuilding and uploading are unnecessary.
  assets = await github.paginate(github.rest.repos.listReleaseAssets, { ...repo, release_id: release.id });
  for (const asset of temporaryAssets(assets, name)) await github.rest.repos.deleteReleaseAsset({ ...repo, asset_id: asset.id });
  const { data: published } = await github.rest.repos.updateRelease({ ...repo, release_id: release.id,
    name: `PowerShift V${tag.slice(1)}`, body, draft: false, prerelease: false, make_latest: 'false' });
  return published;
}
async function reconcileLatest({ github, context }) {
  const tags = await github.paginate(github.rest.repos.listTags, context.repo);
  const releases = await github.paginate(github.rest.repos.listReleases, context.repo);
  const published = releases.filter(release => {
    if (!isVersion(release.tag_name) || release.draft || release.prerelease) return false;
    const tag = tags.find(item => item.name === release.tag_name);
    return tag && release.assets.some(asset => asset.name === zipName(tag.name) && asset.state === 'uploaded' &&
      asset.size > 0 && (!/^source:[0-9a-f]{40}$/.test(asset.label || '') || asset.label === `source:${tag.commit.sha}`));
  }).sort((a, b) => compareVersions(a.tag_name, b.tag_name));
  if (!published.length) return;
  const latest = published[published.length - 1];
  let current;
  try { current = (await github.rest.repos.getLatestRelease(context.repo)).data; }
  catch (error) { if (error.status !== 404) throw error; }
  if (!current || current.id !== latest.id)
    await github.rest.repos.updateRelease({ ...context.repo, release_id: latest.id, make_latest: 'true' });
}
module.exports = { publicationState, temporaryAssets, compareVersions, planReleases, checkPublication, publishRelease, reconcileLatest };
