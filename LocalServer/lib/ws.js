// Minimal RFC 6455 WebSocket server-side support (text frames, ping/close). No dependencies.
const crypto = require("crypto");

const GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

function accept(req, socket, onOpen) {
  const key = req.headers["sec-websocket-key"];
  if (!key) {
    socket.end("HTTP/1.1 400 Bad Request\r\n\r\n");
    return;
  }
  const hash = crypto.createHash("sha1").update(key + GUID).digest("base64");
  const proto = req.headers["sec-websocket-protocol"];
  socket.write(
    "HTTP/1.1 101 Switching Protocols\r\n" +
      "Upgrade: websocket\r\nConnection: Upgrade\r\n" +
      `Sec-WebSocket-Accept: ${hash}\r\n` +
      (proto ? `Sec-WebSocket-Protocol: ${proto.split(",")[0].trim()}\r\n` : "") +
      "\r\n"
  );

  const conn = {
    open: true,
    onMessage: () => {},
    onClose: () => {},
    send(text) {
      if (!conn.open) return;
      socket.write(frame(0x1, Buffer.from(text, "utf8")));
    },
    close() {
      if (!conn.open) return;
      conn.open = false;
      socket.end(frame(0x8, Buffer.alloc(0)));
    },
  };

  let buf = Buffer.alloc(0);
  socket.on("data", (chunk) => {
    buf = Buffer.concat([buf, chunk]);
    for (;;) {
      const f = parse(buf);
      if (!f) break;
      buf = buf.subarray(f.length);
      if (f.opcode === 0x1) conn.onMessage(f.payload.toString("utf8"));
      else if (f.opcode === 0x9) socket.write(frame(0xa, f.payload));
      else if (f.opcode === 0x8) conn.close();
    }
  });
  socket.on("close", () => {
    conn.open = false;
    conn.onClose();
  });
  socket.on("error", () => {});
  onOpen(conn);
}

function frame(opcode, payload) {
  const len = payload.length;
  let head;
  if (len < 126) head = Buffer.from([0x80 | opcode, len]);
  else if (len < 65536) {
    head = Buffer.alloc(4);
    head[0] = 0x80 | opcode;
    head[1] = 126;
    head.writeUInt16BE(len, 2);
  } else {
    head = Buffer.alloc(10);
    head[0] = 0x80 | opcode;
    head[1] = 127;
    head.writeBigUInt64BE(BigInt(len), 2);
  }
  return Buffer.concat([head, payload]);
}

function parse(buf) {
  if (buf.length < 2) return null;
  const opcode = buf[0] & 0x0f;
  const masked = (buf[1] & 0x80) !== 0;
  let len = buf[1] & 0x7f;
  let off = 2;
  if (len === 126) {
    if (buf.length < 4) return null;
    len = buf.readUInt16BE(2);
    off = 4;
  } else if (len === 127) {
    if (buf.length < 10) return null;
    len = Number(buf.readBigUInt64BE(2));
    off = 10;
  }
  const maskOff = off;
  if (masked) off += 4;
  if (buf.length < off + len) return null;
  const payload = Buffer.from(buf.subarray(off, off + len));
  if (masked) for (let i = 0; i < len; i++) payload[i] ^= buf[maskOff + (i & 3)];
  return { opcode, payload, length: off + len };
}

module.exports = { accept };
