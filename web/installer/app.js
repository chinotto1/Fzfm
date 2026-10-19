// SPDX-License-Identifier: GPL-3.0-or-later
import {Updater} from './vendor/fm1ota.js';
import {checkRelease, validatePackage, loaderMismatch, MAX_PACKAGE_BYTES} from './packages.js';

const $ = id => document.getElementById(id);
const supported = window.isSecureContext && typeof navigator.requestMIDIAccess === 'function' && !!crypto.subtle;
let catalog, access, device, prepared, review, mode = 'install', busy = false, updating = false, epoch = 0;
const target = () => mode === 'install' ? catalog?.current : catalog?.recovery.find(r => r.id === $('firmware').value);
const title = r => `${r.name} ${r.version}`;
function log(message) {
  $('log').textContent += `${new Date().toLocaleTimeString()}  ${message}\n`;
  if ($('log').textContent.length > 24000) $('log').textContent = $('log').textContent.slice(-20000);
}
function deviceMessage(message, error = false) {
  $('device-status').textContent = message;
  $('device-status').classList.toggle('error', error);
}
function blockReason() {
  const r = target();
  if (!r) return 'Loading firmware options…';
  if (r.enabled !== true) return r.reason || 'No installable hardware package is published for this build.';
  if (!supported) return 'Use Chrome or Edge on Windows, macOS or Linux to connect the FM1.';
  if (!prepared || prepared.release !== r) return `Download ${r.filename} and choose it above to verify the package.`;
  if (!device) return 'Connect your FM1 to identify its current firmware.';
  if (loaderMismatch(device.identity, r.product)) {
    const original = catalog.recovery.find(item => `ota-${item.product}` === device.identity);
    return `The device is running ${device.identity}. Finish its interrupted update with ${original ? title(original) : 'its original matching package'} before changing firmware.`;
  }
  if (!$('backup-saved').checked) return 'Save your backup and confirm the checkbox before installing.';
  return '';
}
function render() {
  const r = target(), enabled = r?.enabled === true;
  $('current-panel').hidden = mode !== 'install';
  $('recovery-panel').hidden = mode !== 'recover';
  $('package-panel').hidden = !enabled;
  $('mode-install').setAttribute('aria-pressed', String(mode === 'install'));
  $('mode-recover').setAttribute('aria-pressed', String(mode === 'recover'));
  for (const id of ['mode-install', 'mode-recover', 'firmware', 'backup-saved']) $(id).disabled = busy || !catalog;
  $('firmware-file').disabled = busy || !enabled;
  $('connect').disabled = busy || !supported || !catalog;
  $('connect').textContent = device ? 'Reconnect / identify' : 'Connect FM1';
  $('install').textContent = mode === 'install' ? 'Install FM1 FZ' : device?.identity.startsWith('ota-') ? 'Resume matching update' : `Install ${r ? title(r) : 'firmware'}`;
  const reason = blockReason();
  $('install').disabled = busy || !!reason;
  $('action-help').textContent = reason || `Ready to install ${title(r)}. You will review this device and package before any update command is sent.`;
  $('action-help').classList.toggle('success', !reason);
}
function clearPackage() {
  epoch++;
  prepared = null;
  review = null;
  $('firmware-file').value = '';
  $('package-status').textContent = 'Select a firmware file to verify it.';
  $('package-status').className = '';
  $('package-details').open = false;
  const r = target();
  $('package-name').textContent = r?.filename || '—';
  $('package-product').textContent = r?.product || '—';
  $('package-hash').textContent = r?.sha256 || '—';
  if (mode === 'recover' && r) {
    $('firmware-description').textContent = r.description;
    $('firmware-download').href = r.download;
    $('firmware-download').textContent = `Get ${r.filename} from its publisher ↗`;
  }
  render();
}
for (const choice of ['install', 'recover']) $(`mode-${choice}`).onclick = () => {
  if (busy || !catalog) return;
  mode = choice;
  clearPackage();
};
$('firmware').onchange = () => { if (!busy) clearPackage(); };
$('backup-saved').onchange = render;
$('firmware-file').onchange = async event => {
  if (busy) return;
  const file = event.target.files[0], r = target(), token = ++epoch;
  prepared = null;
  $('package-status').className = '';
  render();
  if (!file) { $('package-status').textContent = 'Select a firmware file to verify it.'; return; }
  $('package-status').textContent = 'Checking package identity, size and SHA-256…';
  try {
    checkRelease(r);
    if (file.size > MAX_PACKAGE_BYTES) throw new Error('This file is too large for an FM1 update package.');
    const checked = await validatePackage(new Uint8Array(await file.arrayBuffer()), r);
    if (token !== epoch || r !== target()) return;
    prepared = checked;
    $('package-status').textContent = `${title(r)} verified · ${file.size.toLocaleString()} bytes · SHA-256 matches`;
    $('package-status').className = 'valid';
    log(`Verified ${r.filename} (${checked.digest}).`);
  } catch (error) {
    if (token !== epoch) return;
    $('package-status').textContent = error.message;
    $('package-status').className = 'error';
    log(`Package rejected: ${error.message}`);
  }
  if (token === epoch) render();
};
$('connect').onclick = async () => {
  if (busy || !supported) return;
  busy = true; device = null; render();
  deviceMessage('Waiting for MIDI permission and an FM1 identity…');
  try {
    if (!access?.sysexEnabled) access = await navigator.requestMIDIAccess({sysex:true});
    if (!access.sysexEnabled) throw new Error('Allow MIDI SysEx access in Chrome or Edge, then connect again.');
    access.onstatechange = event => {
      if (!busy && device && event.port?.state === 'disconnected' && device.ports.includes(event.port.id)) {
        device = null;
        deviceMessage('FM1 disconnected. Reconnect it and identify it again.');
        render();
      }
    };
    const found = await new Updater(access, log).find();
    if (!found) throw new Error('No FM1 answered. Check the data cable, stop other MIDI apps, and see recovery help below if Windows shows WL80UBOOT.');
    device = {identity:found.id.text, name:found.name, ports:[found.link.input.id, found.link.output.id]};
    await found.link.close();
    deviceMessage(`Connected: ${device.name} · ${device.identity}${device.identity.startsWith('ota-') ? ' · UPDATE MODE' : ''}`);
    log(`Read-only identity check: ${device.identity} on ${device.name}.`);
  } catch (error) {
    deviceMessage(error.name === 'NotAllowedError' || error.name === 'SecurityError'
      ? 'MIDI permission was denied. Allow MIDI SysEx for this site in Chrome or Edge and connect again.' : error.message, true);
    log(`Connection: ${error.message}`);
  } finally { busy = false; render(); }
};
$('install').onclick = () => {
  if (busy || blockReason()) return;
  review = {package:prepared, identity:device.identity, ports:[...device.ports], mode, epoch};
  $('confirm-description').textContent = `Install ${title(prepared.release)} on this FM1?`;
  $('confirm-device').textContent = `${device.name} · ${device.identity}`;
  $('confirm-file').textContent = prepared.release.filename;
  $('confirm-hash').textContent = prepared.digest;
  $('confirm-dialog').showModal();
};
function cancelReview() { review = null; $('confirm-dialog').close(); }
$('confirm-cancel').onclick = cancelReview;
$('confirm-dialog').addEventListener('cancel', () => { review = null; });
function transferStep(stage, detail) {
  const labels = {start:'Preparing update', verify:'Device is checking the package', loader:'Waiting for update mode', write:'Writing firmware · keep USB connected', reboot:'Waiting for firmware to restart', done:'Device returned with the expected firmware identity'};
  $('transfer-stage').textContent = labels[stage] || stage;
  let value = {start:3, verify:8, loader:12, reboot:96, done:100}[stage];
  if (stage === 'write') value = 15 + Math.min(1, (detail?.end || 0) / prepared.image.length) * 78;
  $('progress').value = Math.max($('progress').value, value || 0);
  $('transfer-percent').textContent = `${Math.floor($('progress').value)}%`;
  $('transfer-help').textContent = stage === 'done'
    ? `The device reports ${detail}. Check its screen, controls and sound. This protocol result does not verify physical memory capacity.`
    : 'Keep USB connected and the computer awake. If a transfer stops, retain this exact firmware package to resume it.';
  if (stage !== 'verify' && stage !== 'write') log(`Update stage: ${stage}${typeof detail === 'string' ? ` · ${detail}` : ''}`);
}
$('confirm-install').onclick = async () => {
  if (!review || busy || review.package !== prepared || review.identity !== device?.identity || review.epoch !== epoch || review.mode !== mode || blockReason()) {
    cancelReview(); render(); return;
  }
  const confirmed = review;
  review = null;
  $('confirm-dialog').close();
  busy = updating = true; render(); $('progress').value = 0; $('transfer-stage').className = '';
  try {
    // Recheck the catalog gate immediately before any mutating device command.
    checkRelease(confirmed.package.release);
    const updater = new Updater(access, log);
    if (confirmed.identity.startsWith('ota-')) {
      if (!await updater.resume(prepared.image, transferStep, prepared.product, confirmed.ports))
        throw new Error('The update did not finish. Reconnect and resume this exact package.');
    } else await updater.install(prepared.image, prepared.product, transferStep, confirmed.identity, confirmed.ports);
    $('transfer-stage').className = 'valid';
    deviceMessage('Update completed. Identify the FM1 again to start another operation.');
  } catch (error) {
    $('transfer-stage').textContent = 'Update stopped · recovery may be needed';
    $('transfer-stage').className = 'error';
    $('transfer-help').textContent = `${error.message} Keep the selected firmware file. If the device is in update mode, reconnect and resume its matching package.`;
    log(`Update stopped (${error.code || 'error'}): ${error.message}`);
  } finally { device = null; busy = updating = false; render(); }
};
window.addEventListener('beforeunload', event => {
  if (!updating) return;
  event.preventDefault(); event.returnValue = '';
});
$('save-log').onclick = () => {
  const url = URL.createObjectURL(new Blob([$('log').textContent || 'No device operations recorded.\n'], {type:'text/plain'}));
  const link = Object.assign(document.createElement('a'), {href:url, download:'fm1-installer-log.txt'});
  link.click(); setTimeout(() => URL.revokeObjectURL(url), 1000);
};
if (!supported) {
  $('browser-note').hidden = false;
  $('browser-note').textContent = 'You can review firmware and recovery information here. Hardware installation requires Chrome or Edge on Windows, macOS or Linux with MIDI SysEx permission. The iPad emulator is available from the link above.';
}
async function init() {
  try {
    const response = await fetch('./releases.json', {cache:'no-cache'});
    if (!response.ok) throw new Error('The firmware catalog could not be loaded. Reload this page.');
    const data = await response.json();
    if (!data.current || !Array.isArray(data.recovery) || !data.recovery.length) throw new Error('The firmware catalog is invalid. No update can start.');
    data.recovery.forEach(checkRelease);
    catalog = data;
    $('current-title').textContent = title(data.current);
    $('current-badge').textContent = data.current.enabled === true ? 'Hardware package published' : 'Hardware release pending';
    $('current-badge').classList.toggle('ready', data.current.enabled === true);
    $('current-reason').textContent = data.current.reason || 'A hardware firmware package is published.';
    $('current-source').textContent = data.current.commit?.slice(0, 7) || 'Development source';
    $('current-source').href = data.current.commit ? `https://github.com/chinotto1/Fzfm/commit/${data.current.commit}` : data.current.source;
    $('readiness').href = data.current.readiness;
    $('firmware').replaceChildren(...data.recovery.map(r => new Option(title(r), r.id)));
    clearPackage();
    log('Catalog loaded. No MIDI permission requested and no device commands sent.');
  } catch (error) {
    $('current-reason').textContent = error.message;
    $('current-badge').textContent = 'Release status unavailable';
    $('action-help').textContent = error.message;
    $('action-help').classList.add('error');
    log(error.message);
  }
}
init();
