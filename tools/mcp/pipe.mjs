import net from 'node:net';
import { randomUUID } from 'node:crypto';

export const maximumMessageBytes = 16 * 1024 * 1024;

export function request(pipeName, method, params = {}, { signal, timeout = 35000 } = {}) {
  if (!/^velos-[a-zA-Z0-9_-]{1,90}$/.test(pipeName)) return Promise.reject(new Error('Invalid Velos control pipe name.'));
  const id = randomUUID();
  const payload = Buffer.from(JSON.stringify({ id, method, params }));
  if (payload.length > maximumMessageBytes) return Promise.reject(new Error('Control request exceeds 16 MB.'));
  if (signal?.aborted) return Promise.reject(new Error('Request cancelled before dispatch.'));
  return new Promise((resolve, reject) => {
    const socket = net.createConnection(`\\\\.\\pipe\\${pipeName}`);
    let received = Buffer.alloc(0);
    let expected;
    let finished = false;
    const fail = error => {
      if (finished) return;
      finished = true;
      clearTimeout(timer);
      signal?.removeEventListener('abort', cancelled);
      socket.destroy();
      reject(error);
    };
    const cancelled = () => fail(new Error('Request cancelled. An already executing mutation may complete; read state before retrying.'));
    const timer = setTimeout(() => fail(new Error('Editor control timed out. Read scene state before retrying a mutation.')), timeout);
    signal?.addEventListener('abort', cancelled, { once: true });
    socket.on('connect', () => {
      const header = Buffer.alloc(4);
      header.writeUInt32LE(payload.length);
      socket.write(Buffer.concat([header, payload]));
    });
    socket.on('data', chunk => {
      if (finished) return;
      if (received.length + chunk.length > maximumMessageBytes + 4) { fail(new Error('Editor response exceeds the message budget.')); return; }
      received = Buffer.concat([received, chunk]);
      if (expected === undefined && received.length >= 4) {
        expected = received.readUInt32LE(0);
        if (!expected || expected > maximumMessageBytes) { fail(new Error('Invalid editor response length.')); return; }
      }
      if (expected === undefined || received.length < expected + 4) return;
      try {
        if (received.length !== expected + 4) throw new Error('Unexpected trailing control data.');
        const response = JSON.parse(received.subarray(4).toString('utf8'));
        if (response.id !== id && response.id !== null) throw new Error('Editor response ID did not match the request.');
        finished = true;
        clearTimeout(timer);
        signal?.removeEventListener('abort', cancelled);
        socket.end(Buffer.from([1]), () => {
          if (response.ok !== true) {
            const error = new Error(response.error?.message ?? 'Editor operation failed.');
            error.code = response.error?.code ?? 'engine_error';
            reject(error);
          } else resolve(response.result);
        });
      } catch (error) { fail(error); }
    });
    socket.on('error', fail);
    socket.on('end', () => { if (!finished) fail(new Error('Editor disconnected before completing the response.')); });
  });
}