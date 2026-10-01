"""Exercise launcher-approved media probing and import through the stdio MCP server."""

import json
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path


server, cli, ffprobe, corpus, build = map(Path, sys.argv[1:])


class Client:
    def __init__(self, project, *flags):
        self.process = subprocess.Popen(
            [str(server), "--project", str(project), *flags],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            text=True, encoding="utf-8", bufsize=1)
        self.request_id = 0
        self.rpc("initialize", {"protocolVersion": "2025-11-25", "capabilities": {},
                                "clientInfo": {"name": "mcp-media-test", "version": "1"}})
        self.process.stdin.write('{"jsonrpc":"2.0","method":"notifications/initialized"}\n')
        self.process.stdin.flush()
        self.info = self.tool("project_get", {})

    def rpc(self, method, params=None):
        self.request_id += 1
        request = {"jsonrpc": "2.0", "id": self.request_id, "method": method}
        if params is not None:
            request["params"] = params
        self.process.stdin.write(json.dumps(request, ensure_ascii=False) + "\n")
        self.process.stdin.flush()
        response = json.loads(self.process.stdout.readline())
        assert response["id"] == self.request_id, response
        return response["result"]

    def tool(self, name, args):
        result = self.rpc("tools/call", {"name": name, "arguments": args})
        assert json.loads(result["content"][0]["text"]) == result["structuredContent"]
        return result["structuredContent"]

    def context(self, key):
        info = self.tool("project_get", {})
        return {"session_id": info["session_id"], "project_id": info["project_id"],
                "expected_revision": info["revision"], "request_key": key}

    def status(self, job_id):
        info = self.tool("project_get", {})
        return self.tool("media_probe_status", {"session_id": info["session_id"],
                        "project_id": info["project_id"], "job_id": job_id})

    def ready(self, job_id):
        deadline = time.monotonic() + 15
        while time.monotonic() < deadline:
            result = self.status(job_id)
            if result["status"] != "running":
                return result
            time.sleep(0.02)
        raise AssertionError("probe did not finish")

    def close(self):
        self.process.stdin.close()
        self.process.wait(timeout=10)
        assert self.process.returncode == 0, self.process.stderr.read()


def rejected(result, code):
    assert result["ok"] is False and result["error"]["code"] == code, result


with tempfile.TemporaryDirectory(prefix="mcp-media-", dir=build) as directory:
    root = Path(directory).resolve()
    media_root = root / "approved"
    media_root.mkdir()
    unicode_file = media_root / "caf\u00e9 & tone.wav"
    tone = media_root / "tone.wav"
    changed = media_root / "changed.wav"
    corrupt = media_root / "corrupt.wav"
    for source, target in [(corpus / unicode_file.name, unicode_file),
                           (corpus / "tone.wav", tone),
                           (corpus / "tone.wav", changed),
                           (corpus / "corrupt.wav", corrupt)]:
        shutil.copyfile(source, target)
    project = root / "empty.nle"
    subprocess.run([str(cli), "new", str(project), "Media session"], check=True,
                   capture_output=True)
    client = Client(project, "--allow-media", "--media-root", str(media_root),
                    "--ffprobe", str(ffprobe), "--allow-edit", "--allow-save")
    try:
        assert len(client.rpc("tools/list")["tools"]) == 14
        denied = client.context("outside")
        denied["path"] = str(project)
        rejected(client.tool("media_probe_start", denied), "media_path_denied")
        missing = client.context("missing")
        missing["path"] = str(media_root / "missing.wav")
        rejected(client.tool("media_probe_start", missing), "media_unavailable")
        start = client.context("unicode-start")
        start["path"] = str(unicode_file)
        begun = client.tool("media_probe_start", start)
        assert begun["status"] == "running"
        assert client.tool("media_probe_start", start) == begun
        ready = client.ready(begun["job_id"])
        assert ready["status"] == "ready" and ready["proposal"]["kind"] == "import"
        assert ready["proposal"]["name"] == unicode_file.name
        assert ready["proposal"]["stream_count"] == 1
        commit = client.context("unicode-commit")
        commit["job_id"] = begun["job_id"]
        imported = client.tool("media_import_commit", commit)
        assert imported["ok"] and imported["media_id"] == "1"
        assert client.tool("media_import_commit", commit) == imported
        assert client.tool("project_get", {})["media_count"] == 1
        undo = client.tool("history_undo", client.context("undo"))
        assert undo["changed"] and client.tool("project_get", {})["media_count"] == 0
        redo = client.tool("history_redo", client.context("redo"))
        assert redo["changed"] and client.tool("project_get", {})["media_count"] == 1
        assert client.tool("media_import_commit", commit) == imported

        moved = media_root / "moved.wav"
        unicode_file.rename(moved)
        relink_start = client.context("relink-start")
        relink_start.update(path=str(moved), relink_media_id="1")
        relink_job = client.tool("media_probe_start", relink_start)["job_id"]
        relink_ready = client.ready(relink_job)
        assert relink_ready["status"] == "ready"
        assert relink_ready["proposal"]["kind"] == "relink"
        assert relink_ready["proposal"]["media_id"] == "1"
        wrong_commit = client.context("wrong-commit-kind")
        wrong_commit["job_id"] = relink_job
        rejected(client.tool("media_import_commit", wrong_commit), "invalid_arguments")
        relink_commit = client.context("relink-commit")
        relink_commit["job_id"] = relink_job
        relinked = client.tool("media_relink_commit", relink_commit)
        assert relinked["ok"] and relinked["media_id"] == "1"
        assert client.tool("media_relink_commit", relink_commit) == relinked
        asset = client.tool("project_snapshot", {"project_id": client.info["project_id"]})[
            "snapshot"]["media"][0]
        assert asset["locations"][0]["uri"] == str(moved)
        assert client.tool("history_undo", client.context("undo-relink"))["changed"]
        assert client.tool("history_redo", client.context("redo-relink"))["changed"]
        noop_start = client.context("noop-relink-start")
        noop_start.update(path=str(moved), relink_media_id="1")
        noop_job = client.tool("media_probe_start", noop_start)["job_id"]
        assert client.ready(noop_job)["status"] == "ready"
        noop_commit = client.context("noop-relink-commit")
        noop_commit["job_id"] = noop_job
        before_noop = noop_commit["expected_revision"]
        noop = client.tool("media_relink_commit", noop_commit)
        assert noop["ok"] and noop["operation_id"] is None
        assert noop["revision"] == before_noop

        second = client.context("stale-start")
        second["path"] = str(tone)
        job = client.tool("media_probe_start", second)["job_id"]
        assert client.ready(job)["status"] == "ready"
        edit = client.context("new-sequence")
        edit["label"] = "Change project while probe waits"
        edit["commands"] = [{"op": "create_sequence", "name": "Main"}]
        proposal = client.tool("edit_preview", edit)
        apply = client.context("apply-sequence")
        apply["proposal_id"] = proposal["proposal_id"]
        assert client.tool("edit_commit", apply)["ok"]
        stale = client.context("stale-commit")
        stale["job_id"] = job
        rejected(client.tool("media_import_commit", stale), "revision_conflict")
        assert client.status(job)["status"] == "stale"

        cancel_start = client.context("cancel-start")
        cancel_start["path"] = str(tone)
        cancel_job = client.tool("media_probe_start", cancel_start)["job_id"]
        cancel = client.context("cancel")
        cancel["job_id"] = cancel_job
        assert client.tool("media_probe_cancel", cancel)["cancelled"]
        assert client.status(cancel_job)["status"] == "cancelled"
        never_commit = client.context("cancel-commit")
        never_commit["job_id"] = cancel_job
        rejected(client.tool("media_import_commit", never_commit), "job_not_ready")

        changed_start = client.context("changed-start")
        changed_start["path"] = str(changed)
        changed_job = client.tool("media_probe_start", changed_start)["job_id"]
        assert client.ready(changed_job)["status"] == "ready"
        with changed.open("ab") as output:
            output.write(b"x")
        changed_commit = client.context("changed-commit")
        changed_commit["job_id"] = changed_job
        rejected(client.tool("media_import_commit", changed_commit), "media_changed")
        assert client.status(changed_job)["status"] == "stale"

        bad_start = client.context("bad-start")
        bad_start["path"] = str(corrupt)
        bad_job = client.tool("media_probe_start", bad_start)["job_id"]
        assert client.ready(bad_job)["status"] == "failed"
        assert client.tool("project_get", {})["media_count"] == 1
        assert client.tool("project_save", client.context("save"))["ok"]
    finally:
        client.close()

    readonly = Client(project, "--allow-media", "--media-root", str(media_root),
                      "--ffprobe", str(ffprobe))
    try:
        args = readonly.context("readonly-start")
        args["path"] = str(tone)
        job = readonly.tool("media_probe_start", args)["job_id"]
        assert readonly.ready(job)["status"] == "ready"
        denied_commit = readonly.context("readonly-commit")
        denied_commit["job_id"] = job
        rejected(readonly.tool("media_import_commit", denied_commit), "permission_denied")
    finally:
        readonly.close()

print("MCP media jobs, approved import, cancel, stale, changed and retry passed")
