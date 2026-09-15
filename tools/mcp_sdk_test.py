"""Interop with the official MCP Python SDK 2.2.0, including its auto-negotiation."""
import asyncio
import importlib.metadata
import json
import subprocess
import sys
import tempfile
import time
from pathlib import Path
from jsonschema import Draft202012Validator
from mcp import Client, StdioServerParameters

server, cli, build, report_file = map(Path, sys.argv[1:])
build = build.resolve()

def rational(value, rate=1):
    return dict(value=str(value), rate=str(rate))

async def exercise(project):
    began = time.perf_counter()
    async with Client(StdioServerParameters(command=str(server), args=['--project', str(project),
            '--actor', 'agent:official-sdk', '--allow-edit', '--allow-save']), read_timeout_seconds=10) as client:
        assert client.protocol_version == '2025-11-25'
        tools = {tool.name: tool for tool in (await client.list_tools()).tools}
        assert len(tools) == 9
        for tool in tools.values():
            Draft202012Validator.check_schema(tool.input_schema)
            Draft202012Validator.check_schema(tool.output_schema)
        calls = 0
        async def invoke(name, arguments):
            nonlocal calls
            Draft202012Validator(tools[name].input_schema).validate(arguments)
            result = await client.call_tool(name, arguments)
            calls += 1
            assert not result.is_error, result
            data = result.structured_content
            assert data and data['ok']
            Draft202012Validator(tools[name].output_schema).validate(data)
            assert json.loads(result.content[0].text) == data
            return data
        info = await invoke('project_get', {})
        context_base = dict(project_id=info['project_id'], session_id=info['session_id'])
        async def context(key):
            current = await invoke('project_get', {})
            return dict(context_base, expected_revision=current['revision'], request_key=key)
        inspection = dict(project_id=info['project_id'])
        original = (await invoke('project_snapshot', inspection))['snapshot']
        media = original['media'][0]['id']
        discarded = await invoke('edit_preview', dict(await context('discard-preview'), label='Discarded proposal',
            commands=[dict(op='create_sequence', name='Discard me')]))
        await invoke('edit_rollback', dict(await context('discard'), proposal_id=discarded['proposal_id']))
        assert (await invoke('project_snapshot', inspection))['snapshot'] == original
        commands = [
            dict(op='create_sequence', name='SDK review', frame_duration=rational(1001, 30000), **{'as': 'sequence'}),
            dict(op='create_track', sequence_id='$sequence', kind='video', name='V1', **{'as': 'video'}),
            dict(op='insert_clip', track_id='$video', media_id=media, position=rational(0), source_in=rational(1001, 30000), duration=rational(4), **{'as': 'opening'}),
            dict(op='move_clip', clip_id='$opening', track_id='$video', position=rational(1)),
            dict(op='trim_clip', clip_id='$opening', position=rational(0), source_in=rational(1001, 30000), duration=rational(3)),
            dict(op='split_clip', clip_id='$opening', position=rational(1), **{'as': 'right'}),
            dict(op='delete_clip', clip_id='$right'),
            dict(op='create_track', sequence_id='$sequence', kind='audio', name='Temporary A1', **{'as': 'audio'}),
            dict(op='reorder_track', sequence_id='$sequence', track_id='$audio', index='0'),
            dict(op='delete_track', track_id='$audio'),
        ]
        preview_args = dict(await context('preview'), label='SDK opening edit', commands=commands)
        preview = await invoke('edit_preview', preview_args)
        assert preview['provisional'] and (await invoke('project_snapshot', inspection))['snapshot'] == original
        commit_args = dict(await context('commit'), proposal_id=preview['proposal_id'])
        committed = await invoke('edit_commit', commit_args)
        assert await invoke('edit_commit', commit_args) == committed
        edited = (await invoke('project_snapshot', inspection))['snapshot']
        sequence = edited['sequences'][-1]
        assert sequence['name'] == 'SDK review' and sequence['frame_duration'] == rational(1001, 30000)
        assert len(sequence['tracks']) == 1
        assert sequence['tracks'][0]['clips'][0]['source_in'] == rational(1001, 30000)
        assert sequence['tracks'][0]['clips'][0]['duration'] == rational(1)
        await invoke('history_undo', await context('undo'))
        undone = (await invoke('project_snapshot', inspection))['snapshot']
        assert undone['sequences'] == original['sequences'] and undone['media'] == original['media']
        await invoke('history_redo', await context('redo'))
        redone = (await invoke('project_snapshot', inspection))['snapshot']
        assert redone['sequences'] == edited['sequences']
        save_args = await context('save')
        saved = await invoke('project_save', save_args)
        assert await invoke('project_save', save_args) == saved
        changes = await invoke('project_changes', dict(inspection, since_revision=original['revision']))
        assert len(changes['operations']) == 3
        assert all(op['actor']['id'] == 'agent:official-sdk' for op in changes['operations'])
        assert 'agent:official-sdk' in project.read_text(encoding='utf-8')
        return dict(sdk='mcp', sdk_version=importlib.metadata.version('mcp'),
            protocol=client.protocol_version, tools=sorted(tools), tool_calls=calls,
            commands_in_batch=len(commands), exact_frame_duration=sequence['frame_duration'],
            grouped_edit_undo_redo=True, idempotent_commit_and_save=True, schemas_validated=True,
            elapsed_ms=round((time.perf_counter()-began)*1000, 2))

directory = tempfile.TemporaryDirectory(prefix='mcp-sdk-', dir=build)
root = Path(directory.name).resolve()
assert root.is_relative_to(build)
try:
    project = root/'sdk-project.nle'
    subprocess.run([str(cli), 'demo', str(project)], check=True, capture_output=True)
    result = asyncio.run(exercise(project))
    subprocess.run([str(cli), 'inspect', str(project)], check=True, capture_output=True)
    report_file.write_text(json.dumps(result, indent=2)+'\n', encoding='utf-8')
    print(json.dumps(result, indent=2))
finally:
    assert root.resolve().is_relative_to(build)
    directory.cleanup()
