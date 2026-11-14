import assert from 'node:assert/strict';
import {createHash} from 'node:crypto';
import {test} from 'node:test';
import {appTitleHeader, inspectCommunity, inspectOfficial, makeUpload, maxBundleBytes, officialFirmware,
  officialPlayKey, suggestedName} from '../main/runner/assets/passport_import.mjs';

const appOffset = 0x10000;
const tableOffset = 0x8000;
const limits = {max_app_bytes: 0x250000};
const factory = [0, 0, appOffset, 0x200000, 'factory'];
const nvs = [1, 2, 0x9000, 0x6000, 'nvs'];
const phy = [1, 1, 0xf000, 0x1000, 'phy_init'];
const cardId = [1, 2, 0x356000, 0x4000, 'cardid'];
const recovery = [0, 0x20, 0x700000, 0x100000, 'recovery'];
const legacy = [nvs, phy, factory, cardId, recovery];
const hash = (algorithm, data) => createHash(algorithm).update(data).digest();

function app(name = 'community-game') {
  const header = Buffer.alloc(24);
  header.set([0xe9, 2]);
  header[12] = 5; // ESP32-C3
  header[23] = 1;
  const desc = Buffer.alloc(256);
  desc.writeUInt32LE(0xabcd5432);
  desc.write('v1.0.0', 16);
  desc.write(name, 48);
  const first = Buffer.alloc(8);
  first.writeUInt32LE(0x3c000020);
  first.writeUInt32LE(desc.length, 4);
  const second = Buffer.alloc(8);
  second.writeUInt32LE(0x40380000);
  second.writeUInt32LE(4, 4);
  const payload = Buffer.from('game');
  const checksum = [...desc, ...payload].reduce((acc, byte) => acc ^ byte, 0xef);
  const data = Buffer.concat([header, first, desc, second, payload]);
  const image = Buffer.concat([data, Buffer.alloc(15 - data.length % 16), Buffer.from([checksum])]);
  return Buffer.concat([image, hash('sha256', image)]);
}

function bundle(image = app(), entries = [nvs, phy, factory]) {
  const raw = Buffer.alloc(appOffset + image.length, 0xff);
  raw[0] = 0xe9;
  let index = tableOffset;
  for (const [type, subtype, start, size, label, flags = 0] of entries) {
    raw.fill(0, index, index + 32);
    raw.writeUInt16LE(0x50aa, index);
    raw.set([type, subtype], index + 2);
    raw.writeUInt32LE(start, index + 4);
    raw.writeUInt32LE(size, index + 8);
    raw.write(label, index + 12, 16, 'ascii');
    raw.writeUInt32LE(flags, index + 28);
    index += 32;
  }
  raw.writeUInt16LE(0xebeb, index);
  raw.set(hash('md5', raw.subarray(tableOffset, index)), index + 16);
  raw.set(image, appOffset);
  return raw;
}

const inspect = bytes => inspectCommunity(new Blob([bytes]), limits);

test('extracts the exact image, keeps padding out, and changes only its name and checksums', async () => {
  const original = app();
  const padded = Buffer.concat([original, Buffer.alloc(2048, 0xff)]);
  const data = await inspect(bundle(padded));
  assert.equal(data.version, 'v1.0.0');
  assert.equal(data.name, 'community-game');
  assert.deepEqual(Buffer.from(data.image), original);
  assert.deepEqual(Buffer.from(makeUpload(data.image, 'Minesweeper')), app('Passport-Minesweeper'));
  assert.deepEqual(Buffer.from(data.image), original);
  assert.equal(suggestedName('Passport-Existing', 'other.bin', data.image), 'Existing');
  assert.match(suggestedName(data.name, '扫雷.bin', data.image), /^Game-[0-9a-f]{8}$/);
  assert.throws(() => makeUpload(data.image, '../game'), /bundle_name/);
});

test('rejects bad format, partition table, target and image checksums', async () => {
  await assert.rejects(inspect(app()), /bundle_format/);
  await assert.rejects(inspectCommunity({size: maxBundleBytes + 1}, limits), /bundle_size/);
  const table = bundle();
  table[tableOffset + 32 * 3 + 16] ^= 1;
  await assert.rejects(inspect(table), /bundle_table/);
  await assert.rejects(inspect(bundle(app(), [nvs, phy, factory, [1, 0x82, 0x20000, 0x1000, 'overlap']])),
    /bundle_table/);
  const otherChip = bundle();
  otherChip[appOffset + 12] = 9;
  await assert.rejects(inspect(otherChip), /bundle_target/);
  const badHash = bundle();
  badHash[badHash.length - 1] ^= 1;
  await assert.rejects(inspect(badHash), /bundle_image/);
  await assert.rejects(inspect(bundle(app().subarray(0, -1))), /bundle_image/);
});

test('rejects extra applications, resource partitions, data to discard and oversized apps', async () => {
  await assert.rejects(inspect(bundle(app(), [nvs, phy, factory, [0, 16, 0x210000, 0x10000, 'ota_0']])),
    /bundle_app_count/);
  await assert.rejects(inspect(bundle(app(), [nvs, phy, factory, [1, 0x82, 0x210000, 0x1000, 'assets']])),
    /bundle_extra/);
  const settings = bundle();
  settings[0x9000] = 0;
  await assert.rejects(inspect(settings), /bundle_extra/);
  await assert.rejects(inspect(bundle(Buffer.concat([app(), Buffer.from('signature')]))), /bundle_extra/);
  await assert.rejects(inspectCommunity(new Blob([bundle()]), {max_app_bytes: app().length - 1}),
    /app_too_large/);
});

test('extracts only the factory image with legacy recovery and cardid reservations', async () => {
  const image = app();
  const raw = bundle(image, legacy);
  const padded = Buffer.alloc(maxBundleBytes, 0xff);
  raw.copy(padded);
  for (const input of [raw, padded, bundle(image, [nvs, phy, factory, cardId])]) {
    assert.deepEqual(Buffer.from((await inspect(input)).image), image);
  }

  app('recovery').copy(padded, recovery[2]);
  await assert.rejects(inspect(padded), /bundle_extra/);
  padded.fill(0xff, recovery[2]);
  padded[cardId[2]] = 0;
  await assert.rejects(inspect(padded), /bundle_extra/);
});

test('requires the known layout for legacy partition reservations', async () => {
  for (const changed of [
    [0, 0x20, 0x600000, 0x100000, 'recovery'],
    [0, 0x20, 0x700000, 0x80000, 'recovery'],
    [0, 0x10, 0x700000, 0x100000, 'recovery'],
    [...recovery, 1],
  ]) {
    await assert.rejects(inspect(bundle(app(), [nvs, phy, factory, changed])), /bundle_app_count/);
  }
  await assert.rejects(inspect(bundle(app(), [nvs, phy, factory, [1, 2, 0x360000, 0x4000, 'cardid']])),
    /bundle_extra/);
});

test('accepts official play page URLs only', () => {
  const base = 'https://ai-passport.folotoy.cn';
  assert.equal(officialPlayKey(`  ${base}/plays/1471/?from=share#install `), '1471');
  assert.equal(officialPlayKey(`${base}/en/plays/neon-rain`), 'neon-rain');
  for (const url of [`${base}/plays/`, `${base}/plays/1471/install/`, `${base}/plays/%2f1471/`,
    `${base}/api/download/community/neon-rain`, `https://user@${new URL(base).host}/plays/1471/`,
    `${base}.example.org/plays/1471/`, `${base}:8443/plays/1471/`,
    'http://ai-passport.folotoy.cn/plays/1471/', 'https://192.168.4.1/plays/1471/']) {
    assert.throws(() => officialPlayKey(url), /community_link/);
  }
});

test('encodes the official display title as bounded UTF-8 without splitting characters', () => {
  const decode = title => Buffer.from(appTitleHeader(title), 'base64').toString('utf8');
  const title = '黄金矿工：地底逃亡';
  assert.equal(decode(` ${title}\r\n续作 `), `${title} 续作`);
  assert.equal(decode('A' + '矿'.repeat(40)), 'A' + '矿'.repeat(31));
  assert.equal(appTitleHeader('\0\n'), '');
});

test('validates the official download before extracting an app', async () => {
  const image = app();
  const raw = bundle(image, legacy);
  const doc = {play: {id: 1471, slug: 'neon-rain', title: {zh: '霓虹雨'}, firmware: {
    available: true, url: '/api/download/community/neon-rain', format: 'esp-merged-0x0',
    size: raw.length, sha256: hash('sha256', raw).toString('hex'),
  }}};
  const release = officialFirmware(doc, '1471');
  assert.deepEqual(release, {id: 1471, slug: 'neon-rain', title: '霓虹雨', size: raw.length,
    sha256: doc.play.firmware.sha256});
  assert.deepEqual(officialFirmware(doc, 'neon-rain'), release);
  for (const id of [undefined, 0, -1, '1471', 1.5, Number.MAX_SAFE_INTEGER + 1]) {
    assert.throws(() => officialFirmware({play: {...doc.play, id}}, 'neon-rain'), /community_metadata/);
  }
  const response = new Response(new ReadableStream({start(stream) {
    stream.enqueue(raw.subarray(0, 2048));
    stream.enqueue(raw.subarray(2048));
    stream.close();
  }}));
  assert.deepEqual(Buffer.from((await inspectOfficial(response, release, limits)).image), image);
  await assert.rejects(inspectOfficial(new Response(raw.subarray(1)), release, limits), /community_integrity/);
  await assert.rejects(inspectOfficial(new Response(Buffer.concat([raw, Buffer.from([0])])), release, limits),
    /community_integrity/);
  raw[123] ^= 1;
  await assert.rejects(inspectOfficial(new Response(raw), release, limits), /community_integrity/);
  assert.throws(() => officialFirmware(doc, '1472'), /community_metadata/);
  assert.throws(() => officialFirmware({play: {...doc.play, firmware: undefined}}, '1471'), /community_missing/);
  const firmware = doc.play.firmware;
  const altered = changes => ({play: {...doc.play, firmware: {...firmware, ...changes}}});
  assert.throws(() => officialFirmware(altered({url: 'https://example.org/game.bin'}), '1471'), /community_format/);
  assert.throws(() => officialFirmware(altered({sha256: ''}), '1471'), /community_metadata/);
  assert.throws(() => officialFirmware(altered({size: maxBundleBytes + 1}), '1471'), /bundle_size/);
});
