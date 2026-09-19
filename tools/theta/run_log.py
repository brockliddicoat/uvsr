"""Small durable phase log and atomic final record for the native slice runners."""
import json
import os
import time


class RunLog:
    def __init__(self, output, token):
        self.output, self.token = output, token
        self.path = output / "events.jsonl"
        self.path.write_text("", encoding="utf-8")
        self.event("started")

    def event(self, phase, **details):
        event = dict(schema_version=1, run_token=self.token, phase=phase, utc_ns=time.time_ns(), **details)
        with self.path.open("a", encoding="utf-8") as stream:
            stream.write(json.dumps(event) + "\n")
            stream.flush()
            os.fsync(stream.fileno())

    def finish(self, report):
        self.event("finished", status=report["status"])
        temporary = self.output / "result.json.partial"
        with temporary.open("w", encoding="utf-8") as stream:
            stream.write(json.dumps(report, indent=2) + "\n")
            stream.flush()
            os.fsync(stream.fileno())
        temporary.replace(self.output / "result.json")
