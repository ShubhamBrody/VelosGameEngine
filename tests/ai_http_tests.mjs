import http from 'node:http';
import { execFile } from 'node:child_process';

const executable = process.argv[2];
if (!executable) throw new Error('Native HTTP test executable is required.');

const server = http.createServer((request, response) => {
  const chunks = [];
  let received = 0;
  request.on('data', chunk => {
    received += chunk.length;
    if (received > 128 * 1024) return request.destroy();
    chunks.push(chunk);
  });
  request.on('end', () => {
    let body;
    try { body = JSON.parse(Buffer.concat(chunks).toString('utf8') || '{}'); }
    catch { response.writeHead(400).end(); return; }
    if (request.url === '/fail') {
      response.writeHead(503).end();
    } else if (request.url === '/openai') {
      if (request.headers.authorization !== 'Bearer disposable-test-value' || !body.stream) {
        response.writeHead(400).end();
        return;
      }
      response.writeHead(200, { 'Content-Type': 'text/event-stream' });
      const data = Buffer.from(`data: ${JSON.stringify({ choices: [{ delta: { content: 'Native caf\u00e9' } }] })}\r\n\r\ndata: [DONE]\n\n`);
      for (let offset = 0; offset < data.length; offset += 7) response.write(data.subarray(offset, offset + 7));
      response.end();
    } else if (request.url === '/ollama/api/chat') {
      if (request.headers.authorization || body.model !== 'test-local') {
        response.writeHead(400).end();
        return;
      }
      response.writeHead(200, { 'Content-Type': 'application/x-ndjson' });
      response.write(JSON.stringify({ message: { content: 'Native local fallback' }, done: false }) + '\n');
      response.end(JSON.stringify({ done: true, eval_count: 4 }) + '\n');
    } else if (request.url === '/cancel') {
      response.writeHead(200, { 'Content-Type': 'text/event-stream' });
      response.write('data: {"choices":[]}\n\n');
    } else {
      response.writeHead(404).end();
    }
  });
});

server.listen(0, '127.0.0.1', () => {
  const { port } = server.address();
  execFile(executable, [`http://127.0.0.1:${port}`], { timeout: 20000 }, (error, stdout, stderr) => {
    process.stdout.write(stdout);
    process.stderr.write(stderr);
    server.closeAllConnections();
    server.close();
    if (error) {
      process.stderr.write(`${error.message}\n`);
      process.exitCode = 1;
    }
  });
});