import { crc32 } from "node:zlib";

// Assembles an animated PNG from same-sized PNG frames by reusing their compressed image data.
// GitHub renders APNG in Markdown like a GIF, at full colour and without extra tooling.

const SIGNATURE = Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]);

function chunks(png) {
  const list = [];
  for (let offset = 8; offset < png.length;) {
    const length = png.readUInt32BE(offset);
    const type = png.toString("ascii", offset + 4, offset + 8);
    list.push({ type, data: png.subarray(offset + 8, offset + 8 + length) });
    offset += 12 + length;
  }
  return list;
}

function chunk(type, data) {
  const head = Buffer.alloc(8);
  head.writeUInt32BE(data.length, 0);
  head.write(type, 4, "ascii");
  const crc = Buffer.alloc(4);
  crc.writeUInt32BE(crc32(Buffer.concat([head.subarray(4), data])) >>> 0, 0);
  return Buffer.concat([head, data, crc]);
}

/** frames: [{ png: Buffer, delayMs: number }], all with the same width and height. */
export function encodeApng(frames, loops = 0) {
  const first = chunks(frames[0].png);
  const header = first.find((item) => item.type === "IHDR").data;
  const width = header.readUInt32BE(0);
  const height = header.readUInt32BE(4);
  const out = [SIGNATURE, chunk("IHDR", header)];
  const control = Buffer.alloc(8);
  control.writeUInt32BE(frames.length, 0);
  control.writeUInt32BE(loops, 4);
  out.push(chunk("acTL", control));

  let sequence = 0;
  frames.forEach((frame, index) => {
    const parts = chunks(frame.png);
    const size = parts.find((item) => item.type === "IHDR").data;
    if (size.readUInt32BE(0) !== width || size.readUInt32BE(4) !== height) throw new Error(`Frame ${index} has a different size`);
    const fctl = Buffer.alloc(26);
    fctl.writeUInt32BE(sequence++, 0);
    fctl.writeUInt32BE(width, 4);
    fctl.writeUInt32BE(height, 8);
    fctl.writeUInt32BE(0, 12);
    fctl.writeUInt32BE(0, 16);
    fctl.writeUInt16BE(Math.max(1, Math.round(frame.delayMs)), 20);
    fctl.writeUInt16BE(1000, 22);
    fctl.writeUInt8(0, 24); // dispose: none
    fctl.writeUInt8(0, 25); // blend: source
    out.push(chunk("fcTL", fctl));
    for (const part of parts.filter((item) => item.type === "IDAT")) {
      if (index === 0) out.push(chunk("IDAT", part.data));
      else {
        const seq = Buffer.alloc(4);
        seq.writeUInt32BE(sequence++, 0);
        out.push(chunk("fdAT", Buffer.concat([seq, part.data])));
      }
    }
  });
  out.push(chunk("IEND", Buffer.alloc(0)));
  return Buffer.concat(out);
}
