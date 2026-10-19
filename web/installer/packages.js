// SPDX-License-Identifier: GPL-3.0-or-later
import {productOf, logicalImage, sha256hex} from './vendor/fm1pkg.js';

export const MAX_PACKAGE_BYTES = 2 * 1024 * 1024;
export function checkRelease(release) {
  if (release?.enabled !== true) throw new Error(release?.reason || 'This firmware has no installable hardware release.');
  if (!/^FM-1_\d{3}$/.test(release.product || '') ||
      !/^[a-f0-9]{64}$/.test(release.sha256 || '') ||
      !Number.isSafeInteger(release.size) || release.size < 960 || release.size > MAX_PACKAGE_BYTES)
    throw new Error('The firmware catalog is incomplete. No update can start.');
  return release;
}

export async function validatePackage(bytes, release) {
  checkRelease(release);
  if (!(bytes instanceof Uint8Array) || bytes.length !== release.size)
    throw new Error(`Choose the original ${release.filename}; the file size does not match this release.`);
  if (productOf(bytes) !== release.product)
    throw new Error(`This package is for another firmware. Choose ${release.filename}.`);
  const digest = await sha256hex(bytes);
  if (digest !== release.sha256)
    throw new Error(`Checksum mismatch. Download ${release.filename} again from its official release.`);
  return {image:logicalImage(bytes), product:release.product, digest, release};
}

export function loaderMismatch(identity, product) {
  return identity?.startsWith('ota-') && identity !== `ota-${product}`;
}
