import {md5, sha256} from './passport_hashes.mjs';

export const maxBundleBytes = 8 * 1024 * 1024;
const tableStart = 0x8000;
const tableEnd = 0x8c00;
const partitionBytes = 32;
const headerBytes = 24;
const segmentHeaderBytes = 8;
const versionOffset = 48;
const nameOffset = 80;
const fieldBytes = 32;
const digestBytes = 32;
const appPrefix = 'Passport-';
const appName = /^[A-Za-z0-9_-]{1,22}$/;
const playKey = /^[A-Za-z0-9][A-Za-z0-9_-]{0,79}$/;
const communityOrigin = 'https://ai-passport.folotoy.cn';

function equal(a, b) {
  return a.length === b.length && a.every((byte, index) => byte === b[index]);
}

function asciiField(bytes, offset, width) {
  const field = bytes.subarray(offset, offset + width);
  const end = field.indexOf(0);
  const text = end < 0 ? field : field.subarray(0, end);
  if (!text.length || !text.every(byte => byte >= 0x20 && byte <= 0x7e)) return '';
  return String.fromCharCode(...text);
}

function partitionTable(bytes) {
  const table = new DataView(bytes.buffer, bytes.byteOffset + tableStart, tableEnd - tableStart);
  const partitions = [];
  for (let i = 0; i < table.byteLength; i += partitionBytes) {
    const magic = table.getUint16(i, true);
    if (magic === 0xebeb) {
      if (!equal(md5(bytes.subarray(tableStart, tableStart + i)),
                 bytes.subarray(tableStart + i + 16, tableStart + i + 32))) throw new Error('bundle_table');
      return partitions;
    }
    if (magic !== 0x50aa) break;
    const type = table.getUint8(i + 2);
    const subtype = table.getUint8(i + 3);
    const start = table.getUint32(i + 4, true);
    const size = table.getUint32(i + 8, true);
    const label = asciiField(bytes, tableStart + i + 12, 16);
    const end = start + size;
    if (!label || start < 0x9000 || !size || end > maxBundleBytes ||
        start % (type === 0 ? 0x10000 : 0x1000) || size % 0x1000 ||
        partitions.some(part => part.label === label || (start < part.end && end > part.start))) {
      throw new Error('bundle_table');
    }
    partitions.push({type, subtype, start, end, label, flags: table.getUint32(i + 28, true)});
  }
  throw new Error('bundle_table');
}

function blank(bytes) {
  return bytes.every(byte => byte === 0xff);
}

function inspectApp(bytes, limits) {
  if (bytes.length < headerBytes + segmentHeaderBytes + 256 + 16 + digestBytes ||
      bytes[0] !== 0xe9 || !bytes[1] || bytes[1] > 16 || bytes[23] !== 1) throw new Error('bundle_image');
  const image = new DataView(bytes.buffer, bytes.byteOffset, bytes.length);
  if (image.getUint16(12, true) !== 5) throw new Error('bundle_target');
  if (image.getUint32(28, true) < 256 || image.getUint32(32, true) !== 0xabcd5432) {
    throw new Error('bundle_image');
  }
  const version = asciiField(bytes, versionOffset, fieldBytes);
  if (!version || version.length >= fieldBytes) throw new Error('bundle_image');

  let end = headerBytes;
  let checksum = 0xef;
  for (let i = 0; i < bytes[1]; i++) {
    if (end + segmentHeaderBytes > bytes.length) throw new Error('bundle_image');
    const length = image.getUint32(end + 4, true);
    const start = end + segmentHeaderBytes;
    end = start + length;
    if (length % 4 || end > bytes.length) throw new Error('bundle_image');
    for (let j = start; j < end; j++) checksum ^= bytes[j];
  }
  const digestStart = (end + 16) & ~15;
  const size = digestStart + digestBytes;
  if (size > bytes.length || bytes[digestStart - 1] !== checksum) throw new Error('bundle_image');
  if (size > limits.max_app_bytes) throw new Error('app_too_large');
  if (!blank(bytes.subarray(size))) throw new Error('bundle_extra');
  if (!equal(sha256(bytes.subarray(0, digestStart)), bytes.subarray(digestStart, size))) {
    throw new Error('bundle_image');
  }
  return {image: bytes.slice(0, size), version, name: asciiField(bytes, nameOffset, fieldBytes)};
}

// Read the same raw, single-app layout as tools/extract_passport_app.py.
// Read-only slices keep the merged image out of the device's memory and storage.
export async function inspectCommunity(file, limits) {
  if (file.size > maxBundleBytes) throw new Error('bundle_size');
  if (file.size < tableEnd) throw new Error('bundle_format');
  return inspectBundle(new Uint8Array(await file.arrayBuffer()), limits);
}

function inspectBundle(bytes, limits) {
  if (bytes[0] !== 0xe9) throw new Error('bundle_format');
  // The legacy FoloToy layout reserves this slot for device-resident recovery.
  const partitions = partitionTable(bytes).filter(part =>
    !(part.label === 'recovery' && part.type === 0 && part.subtype === 0x20 &&
      part.start === 0x700000 && part.end === maxBundleBytes && !part.flags));
  const applications = partitions.filter(part => part.type === 0);
  if (applications.length !== 1) throw new Error('bundle_app_count');
  const [app] = applications;
  if (app.subtype !== 0 || app.flags) throw new Error('bundle_extra');
  for (const part of partitions) {
    if (part === app) continue;
    const standard = part.type === 1 &&
      ((part.subtype === 2 && part.label === 'nvs') || (part.subtype === 1 && part.label === 'phy_init'));
    const cardId = part.label === 'cardid' && part.type === 1 && part.subtype === 2 &&
      part.start === 0x356000 && part.end === 0x35a000;
    if ((!standard && !cardId) || part.flags) throw new Error('bundle_extra');
  }
  // Reserved partitions and undeclared gaps may contain only erased bytes.
  if (!blank(bytes.subarray(tableEnd, app.start)) || !blank(bytes.subarray(app.end))) {
    throw new Error('bundle_extra');
  }
  return inspectApp(bytes.subarray(app.start, app.end), limits);
}

export function officialPlayKey(input) {
  let url;
  try { url = new URL(input.trim()); }
  catch { throw new Error('community_link'); }
  const key = /^\/(?:en\/)?plays\/([^/]+)\/?$/.exec(url.pathname)?.[1];
  if (url.origin !== communityOrigin || url.username || url.password || !key || !playKey.test(key)) {
    throw new Error('community_link');
  }
  return key;
}

export function officialFirmware(metadata, key) {
  const play = metadata?.play;
  const firmware = play?.firmware;
  if (!play || !Number.isSafeInteger(play.id) || play.id <= 0 ||
      (/^[1-9]\d*$/.test(key) ? String(play.id) !== key : play.slug !== key)) {
    throw new Error('community_metadata');
  }
  if (!firmware?.available) throw new Error('community_missing');
  if (typeof play.slug !== 'string' || !playKey.test(play.slug) ||
      firmware.url !== '/api/download/community/' + play.slug ||
      firmware.format !== 'esp-merged-0x0') throw new Error('community_format');
  if (!Number.isInteger(firmware.size) || firmware.size < tableEnd ||
      typeof firmware.sha256 !== 'string' || !/^[a-f0-9]{64}$/i.test(firmware.sha256)) {
    throw new Error('community_metadata');
  }
  if (firmware.size > maxBundleBytes) throw new Error('bundle_size');
  const title = [play.title?.zh, play.title?.en].find(value => typeof value === 'string' && value.trim());
  return {id: play.id, slug: play.slug, title: title?.trim() || play.slug,
    size: firmware.size, sha256: firmware.sha256.toLowerCase()};
}

export async function inspectOfficial(response, firmware, limits) {
  const bytes = new Uint8Array(firmware.size);
  const reader = response.body?.getReader();
  if (!reader) throw new Error('community_fetch');
  let offset = 0;
  try {
    while (true) {
      const {value, done} = await reader.read();
      if (done) break;
      if (value.length > bytes.length - offset) {
        await reader.cancel().catch(() => {});
        throw new Error('community_integrity');
      }
      bytes.set(value, offset);
      offset += value.length;
    }
  } finally {
    reader.releaseLock();
  }
  if (offset !== bytes.length || hex(sha256(bytes)) !== firmware.sha256) {
    throw new Error('community_integrity');
  }
  return inspectBundle(bytes, limits);
}

function hex(bytes) {
  return Array.from(bytes, byte => byte.toString(16).padStart(2, '0')).join('');
}

export function appTitleHeader(title) {
  const text = title.replace(/[\p{Cc}\u2028\u2029]+/gu, ' ').trim();
  const bytes = new Uint8Array(96);
  const {written} = new TextEncoder().encodeInto(text, bytes);
  return btoa(String.fromCharCode(...bytes.subarray(0, written)));
}

export function suggestedName(name, filename, image) {
  if (name.startsWith(appPrefix) && appName.test(name.slice(appPrefix.length))) {
    return name.slice(appPrefix.length);
  }
  const base = filename.replace(/\.bin$/i, '').replace(/^Passport-/i, '')
    .replace(/[^A-Za-z0-9_-]+/g, '-').replace(/^[-_]+|[-_]+$/g, '');
  const digest = hex(image.subarray(image.length - digestBytes, image.length - digestBytes + 4));
  return (base.slice(0, 13) || 'Game') + '-' + digest;
}

export function makeUpload(original, name) {
  if (!appName.test(name)) throw new Error('bundle_name');
  const image = original.slice();
  const digestStart = image.length - digestBytes;
  const nameEnd = nameOffset + fieldBytes;
  let checksum = image[digestStart - 1];
  for (let i = nameOffset; i < nameEnd; i++) checksum ^= image[i];
  image.fill(0, nameOffset, nameEnd);
  image.set(new TextEncoder().encode(appPrefix + name), nameOffset);
  for (let i = nameOffset; i < nameEnd; i++) checksum ^= image[i];
  image[digestStart - 1] = checksum;
  image.set(sha256(image.subarray(0, digestStart)), digestStart);
  return image;
}
