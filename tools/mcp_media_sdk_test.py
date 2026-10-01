"""Official MCP SDK acceptance for approved import, rough cut and relink."""

import asyncio
import json
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

from jsonschema import Draft202012Validator
from mcp import Client, StdioServerParameters


server, cli, probe, corpus, build = map(Path, sys.argv[1:6])
export_enabled = sys.argv[6:] == ["export"]


async def exercise(project, root, original):
    flags = ["--project", str(project), "--actor", "agent:media-sdk", "--allow-edit",
             "--allow-save", "--allow-media", "--media-root", str(root),
             "--ffprobe", str(probe)]
    if export_enabled:
        flags += ["--allow-export", "--output-root", str(root)]
    async with Client(StdioServerParameters(command=str(server), args=flags),
                      read_timeout_seconds=10) as client:
        assert client.protocol_version == "2025-11-25"
        tools = {tool.name: tool for tool in (await client.list_tools()).tools}
        assert len(tools) == (17 if export_enabled else 14)
        for tool in tools.values():
            Draft202012Validator.check_schema(tool.input_schema)
            Draft202012Validator.check_schema(tool.output_schema)

        async def invoke(name, args):
            Draft202012Validator(tools[name].input_schema).validate(args)
            response = await client.call_tool(name, args)
            assert not response.is_error, response
            data = response.structured_content
            assert data and data["ok"]
            Draft202012Validator(tools[name].output_schema).validate(data)
            assert json.loads(response.content[0].text) == data
            return data

        info = await invoke("project_get", {})
        assert info["allow_media"] and info["media_count"] == 0
        identity = {"session_id": info["session_id"], "project_id": info["project_id"]}

        async def context(key):
            current = await invoke("project_get", {})
            return dict(identity, expected_revision=current["revision"], request_key=key)

        async def ready(job_id):
            for _ in range(500):
                status = await invoke("media_probe_status", dict(identity, job_id=job_id))
                if status["status"] != "running":
                    assert status["status"] == "ready", status
                    return status
                await asyncio.sleep(0.02)
            raise AssertionError("media probe timed out")

        start = await invoke("media_probe_start", dict(await context("import-start"),
                                                        path=str(original)))
        assert (await ready(start["job_id"]))["proposal"]["kind"] == "import"
        imported = await invoke("media_import_commit", dict(await context("import-commit"),
                                                              job_id=start["job_id"]))
        media_id = imported["media_id"]
        commands = [
            {"op": "create_sequence", "name": "Agent rough cut", "as": "sequence"},
            {"op": "create_track", "sequence_id": "$sequence", "kind": "audio",
             "name": "A1", "as": "track"},
            {"op": "insert_clip", "track_id": "$track", "media_id": media_id,
             "position": {"value": "0", "rate": "1"},
             "source_in": {"value": "0", "rate": "1"},
             "duration": {"value": "2", "rate": "1"}},
        ]
        preview = await invoke("edit_preview", dict(await context("cut-preview"),
                                                     label="Build audio rough cut", commands=commands))
        await invoke("edit_commit", dict(await context("cut-commit"),
                                         proposal_id=preview["proposal_id"]))

        moved = root / "moved.wav"
        original.rename(moved)
        relink = await invoke("media_probe_start", dict(await context("relink-start"),
                                                        path=str(moved), relink_media_id=media_id))
        assert (await ready(relink["job_id"]))["proposal"]["kind"] == "relink"
        await invoke("media_relink_commit", dict(await context("relink-commit"),
                                                 job_id=relink["job_id"]))
        saved = await invoke("project_save", await context("save"))
        state = (await invoke("project_snapshot", {"project_id": info["project_id"]}))["snapshot"]
        assert saved["saved_revision"] == state["revision"]
        assert state["media"][0]["locations"][0]["uri"] == str(moved)
        assert state["sequences"][0]["tracks"][0]["clips"][0]["media_id"] == media_id
        if export_enabled:
            async def rejected_export(path, key):
                bad = dict(await context(key), sequence_id=state["sequences"][0]["id"],
                           path=str(path), preset="lossless_reference", overwrite=True)
                response = await client.call_tool("export_start", bad)
                assert response.is_error and response.structured_content["error"]["code"] == "output_path_denied", response

            await rejected_export(project, "export-project-denied")
            await rejected_export(root.parent / "outside.mkv", "export-outside-denied")
            output = root / "caf\u00e9 reference.mkv"
            sequence = state["sequences"][0]["id"]
            args = dict(await context("export-start"), sequence_id=sequence,
                        path=str(output), preset="lossless_reference", overwrite=False)
            started = await invoke("export_start", args)
            assert await invoke("export_start", args) == started
            # Change the live project while the captured revision exports.
            undo = await invoke("history_undo", await context("export-live-undo"))
            assert undo["revision"] != started["base_revision"]
            for _ in range(1000):
                status = await invoke("export_status", dict(identity, job_id=started["job_id"]))
                if status["status"] != "running":
                    break
                await asyncio.sleep(0.02)
            assert status["status"] == "succeeded", status
            assert status["result"]["revision"] == state["revision"]
            assert status["result"]["samples"] == "96000"
            facts = json.loads(subprocess.check_output([str(probe), "-v", "error", "-show_streams",
                                                       "-show_format", "-of", "json", str(output)]))
            assert {s["codec_name"] for s in facts["streams"]} == {"ffv1", "pcm_f32le"}
            assert abs(float(facts["format"]["duration"]) - 2.0) < 0.01
            assert status["progress"]["samples_complete"] == "96000"
            # A cancellation after installation cannot retroactively claim cancellation.
            cancelled = await invoke("export_cancel", dict(await context("export-late-cancel"),
                                                           job_id=started["job_id"]))
            assert cancelled["status"] == "succeeded" and output.exists()
            await invoke("history_redo", await context("export-live-redo"))
            original_bytes = output.read_bytes()
            again = dict(await context("export-no-overwrite"), sequence_id=sequence,
                         path=str(output), preset="lossless_reference", overwrite=False)
            collision = await invoke("export_start", again)
            for _ in range(1000):
                failed = await invoke("export_status", dict(identity, job_id=collision["job_id"]))
                if failed["status"] != "running":
                    break
                await asyncio.sleep(0.02)
            assert failed["status"] == "failed", failed
            assert "exists" in failed["job_error"], failed
            assert output.read_bytes() == original_bytes
            await invoke("project_save", await context("export-save"))
            state = (await invoke("project_snapshot", {"project_id": info["project_id"]}))["snapshot"]

    if export_enabled:
        read_only_flags = [flag for flag in flags if flag not in ("--allow-edit", "--allow-save")]
        async with Client(StdioServerParameters(command=str(server), args=read_only_flags),
                          read_timeout_seconds=10) as client:
            info_only = (await client.call_tool("project_get", {})).structured_content
            assert info_only["allow_export"] and not info_only["allow_edit"] and not info_only["allow_save"]
            context_only = {"session_id": info_only["session_id"], "project_id": info_only["project_id"],
                            "expected_revision": info_only["revision"]}
            destination = root / "cancelled.mkv"
            started = (await client.call_tool("export_start", dict(context_only, request_key="cancel-start",
                        sequence_id=state["sequences"][0]["id"], path=str(destination),
                        preset="lossless_reference", overwrite=False))).structured_content
            assert started["ok"], started
            cancelled = (await client.call_tool("export_cancel", dict(context_only, request_key="cancel",
                         job_id=started["job_id"]))).structured_content
            assert cancelled["ok"] and cancelled["cancel_requested"]
            identity_only = {k: context_only[k] for k in ("session_id", "project_id")}
            for _ in range(1000):
                status = (await client.call_tool("export_status", dict(identity_only,
                          job_id=started["job_id"]))).structured_content
                if status["status"] != "running":
                    break
                await asyncio.sleep(0.02)
            assert status["status"] in ("cancelled", "succeeded"), status
            assert destination.exists() == (status["status"] == "succeeded")
            assert not list(root.glob("cancelled.mkv.nle-export-*"))

    async with Client(StdioServerParameters(command=str(server),
                      args=["--project", str(project)]), read_timeout_seconds=10) as client:
        tools = {tool.name for tool in (await client.list_tools()).tools}
        assert len(tools) == 9
        response = await client.call_tool("project_get", {})
        assert response.structured_content["media_count"] == 1
        response = await client.call_tool("project_snapshot", {"project_id": info["project_id"]})
        reopened = response.structured_content["snapshot"]
        assert reopened["media"] == state["media"]
        assert reopened["sequences"] == state["sequences"]


with tempfile.TemporaryDirectory(prefix="mcp-media-sdk-", dir=build) as directory:
    root = Path(directory).resolve()
    approved = root / "approved"
    approved.mkdir()
    original = approved / "caf\u00e9 & tone.wav"
    shutil.copyfile(corpus / original.name, original)
    project = root / "empty.nle"
    subprocess.run([str(cli), "new", str(project), "Agent media"], check=True,
                   capture_output=True)
    asyncio.run(exercise(project, approved, original))

print("Official MCP client import, rough cut, relink and native reopen passed")
