"""Exercise the actual stdio executable and fixed-file save boundary using only stdlib."""
import json
import queue
import subprocess
import sys
import tempfile
import threading
from pathlib import Path

server, cli, build_dir = map(Path, sys.argv[1:])
build_dir = build_dir.resolve()

class Client:
    def __init__(self, project, *flags):
        self.process = subprocess.Popen([str(server), '--project', str(project), *flags],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            text=True, encoding='utf-8', bufsize=1)
        self.responses = queue.Queue()
        self.reader = threading.Thread(target=self.read, daemon=True)
        self.reader.start()
        self.next_id = 0

    def read(self):
        for line in self.process.stdout:
            self.responses.put(line)
        self.responses.put(None)

    def raw(self, line):
        self.process.stdin.write(line + '\n')
        self.process.stdin.flush()
        result = self.responses.get(timeout=10)
        assert result is not None, 'Server exited before replying'
        return json.loads(result)

    def rpc(self, method, params=None):
        self.next_id += 1
        message = dict(jsonrpc='2.0', id=self.next_id, method=method)
        if params is not None:
            message['params'] = params
        result = self.raw(json.dumps(message))
        assert result['id'] == self.next_id, result
        return result

    def initialize(self):
        result = self.rpc('initialize', dict(protocolVersion='2025-11-25', capabilities={},
            clientInfo=dict(name='stdio-test', version='1')))
        assert result['result']['protocolVersion'] == '2025-11-25'
        self.process.stdin.write(json.dumps(dict(jsonrpc='2.0', method='notifications/initialized')) + '\n')
        self.process.stdin.flush()
        self.info = self.tool('project_get', {})
        assert self.info['ok']

    def tool(self, name, arguments):
        result = self.rpc('tools/call', dict(name=name, arguments=arguments))['result']
        assert json.loads(result['content'][0]['text']) == result['structuredContent']
        assert result['isError'] == (not result['structuredContent']['ok'])
        return result['structuredContent']

    def context(self, key):
        info = self.tool('project_get', {})
        return dict(project_id=info['project_id'], session_id=info['session_id'],
            expected_revision=info['revision'], request_key=key)

    def close(self):
        if self.process.poll() is None:
            self.process.stdin.close()
            self.process.wait(timeout=5)
        self.reader.join(timeout=2)
        assert self.process.returncode == 0, self.process.stderr.read()

    def terminate(self):
        if self.process.poll() is None:
            self.process.kill()
        self.process.wait(timeout=5)
        self.reader.join(timeout=2)
        for stream in (self.process.stdin, self.process.stdout, self.process.stderr):
            stream.close()

clients = []
def start(project, *flags):
    client = Client(project, *flags)
    clients.append(client)
    client.initialize()
    return client

directory = tempfile.TemporaryDirectory(prefix='mcp-stdio-', dir=build_dir)
root = Path(directory.name).resolve()
assert root.is_relative_to(build_dir)
try:
    project = root / 'agent \u65e5\u672c project.nle'
    subprocess.run([str(cli), 'demo', str(project)], check=True, capture_output=True)
    original = project.read_bytes()
    readonly = start(project)
    catalog = readonly.rpc('tools/list')['result']['tools']
    assert len(catalog) == 9 and all('inputSchema' in t and 'outputSchema' in t for t in catalog)
    assert readonly.raw('{')['error']['code'] == -32700
    assert readonly.rpc('ping')['result'] == {}
    assert readonly.rpc('tools/call', dict(name='shell', arguments={}))['error']['code'] == -32602
    denied = readonly.tool('project_save', readonly.context('save'))
    assert denied['error']['code'] == 'permission_denied' and project.read_bytes() == original
    readonly.close()

    client = start(project, '--allow-edit', '--allow-save', '--actor', 'agent:stdio-test')
    # A cooperative second writer is rejected, even within the same OS user.
    competitor = subprocess.run([str(server), '--project', str(project), '--allow-save'],
        input='', capture_output=True, text=True, encoding='utf-8', timeout=5)
    assert competitor.returncode != 0 and 'project_locked' in competitor.stderr
    assert competitor.stdout == ''
    args = client.context('preview')
    args.update(label='Agent creates a review sequence', commands=[dict(op='create_sequence', name='Agent review',
        frame_duration=dict(value='1001', rate='30000'), **{'as': 'review'})])
    preview = client.tool('edit_preview', args)
    assert preview['ok'] and preview['provisional'] and project.read_bytes() == original
    assert client.tool('edit_preview', args) == preview
    commit = client.context('commit')
    commit['proposal_id'] = preview['proposal_id']
    committed = client.tool('edit_commit', commit)
    assert committed['ok'] and project.read_bytes() == original
    assert client.tool('edit_commit', commit) == committed
    save_as = client.context('save-as')
    forbidden = root / 'another.nle'
    save_as['path'] = str(forbidden)
    assert client.tool('project_save', save_as)['error']['code'] == 'invalid_arguments'
    assert not forbidden.exists()
    save = client.context('save')
    saved = client.tool('project_save', save)
    assert saved['ok'] and project.read_bytes() != original
    assert b'agent:stdio-test' in project.read_bytes()
    expected_saved = project.read_bytes()
    assert client.tool('project_save', save) == saved and project.read_bytes() == expected_saved
    subprocess.run([str(cli), 'inspect', str(project)], check=True, capture_output=True)
    # A noncooperating external writer is detected before replacement.
    external = expected_saved.replace(b'Agent review', b'Other review')
    project.write_bytes(external)
    assert client.tool('project_save', client.context('external-conflict'))['error']['code'] == 'save_conflict'
    assert project.read_bytes() == external
    client.close()

    # Closing stdin does not save unsaved edits, and releases the cooperative lock.
    transient = start(project, '--allow-edit', '--allow-save')
    proposed = transient.context('transient')
    proposed.update(label='Unsaved', commands=[dict(op='create_sequence', name='Unsaved sequence')])
    preview = transient.tool('edit_preview', proposed)
    commit = transient.context('transient-commit'); commit['proposal_id'] = preview['proposal_id']
    assert transient.tool('edit_commit', commit)['ok']
    transient.close()
    assert project.read_bytes() == external
    reopened = start(project, '--allow-save')
    assert reopened.info['session_id'] != transient.info['session_id']
    assert reopened.info['history']['undo_entries'] == 0
    reopened.close()

    # Reject a replacement symlink before a fixed-path save, where the OS permits creation.
    linkable = root / 'replaceable.nle'; linkable.write_bytes(external)
    link_client = start(linkable, '--allow-save')
    linkable.unlink()
    try:
        linkable.symlink_to(project)
    except OSError:
        linkable.write_bytes(external)
    else:
        assert link_client.tool('project_save', link_client.context('link'))['error']['code'] == 'save_conflict'
        assert project.read_bytes() == external
    link_client.close()

    oversized = start(project)
    result = oversized.raw(' ' * (1024 * 1024 + 1))
    assert result['error']['code'] == -32700
    oversized.process.wait(timeout=5)
    assert oversized.process.returncode != 0
    print('MCP stdio, Unicode paths, fixed-file save, conflicts, writer lock, retry and EOF checks passed')
finally:
    for client in clients:
        client.terminate()
    assert root.resolve().is_relative_to(build_dir)
    directory.cleanup()
