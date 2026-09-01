import json
import os
import socket
import unittest
from unittest.mock import patch

os.environ.setdefault("UNREAL_MCP_LOG_LEVEL", "CRITICAL")

from unreal_mcp_server import UnrealConnection


class FakeSocket:
    def __init__(self, events):
        self.events = list(events)
        self.timeout = None
        self.sent = bytearray()
        self.closed = False

    def settimeout(self, timeout):
        self.timeout = timeout

    def recv(self, _buffer_size):
        event = self.events.pop(0)
        if isinstance(event, BaseException):
            raise event
        return event

    def sendall(self, payload):
        self.sent.extend(payload)

    def close(self):
        self.closed = True


class ReceiveFullResponseTests(unittest.TestCase):
    def test_fragmented_response_is_joined_once_to_eof(self):
        payload = json.dumps({
            "status": "success",
            "result": {"text": "x" * 200_000},
        }).encode("utf-8")
        events = [payload[index:index + 8192] for index in range(0, len(payload), 8192)]
        events.append(b"")

        data, chunks = UnrealConnection().receive_full_response(FakeSocket(events))

        self.assertEqual(data, payload)
        self.assertEqual(chunks, len(events) - 1)

    def test_legacy_complete_json_is_accepted_after_timeout(self):
        payload = b'{"status":"success","result":{}}'
        data, chunks = UnrealConnection().receive_full_response(
            FakeSocket([payload, socket.timeout()])
        )

        self.assertEqual(data, payload)
        self.assertEqual(chunks, 1)

    def test_invalid_partial_json_is_rejected_after_timeout(self):
        with self.assertRaisesRegex(Exception, "Timeout receiving Unreal response"):
            UnrealConnection().receive_full_response(
                FakeSocket([b'{"status":', socket.timeout()])
            )

    def test_response_size_limit_is_enforced(self):
        with patch("unreal_mcp_server.MAX_RESPONSE_BYTES", 4):
            with self.assertRaisesRegex(ValueError, "Response exceeds"):
                UnrealConnection().receive_full_response(FakeSocket([b"12345"]))

    def test_send_command_uses_one_compact_request_and_closes(self):
        payload = b'{"status":"success","result":{"value":1}}'
        fake_socket = FakeSocket([payload, b""])
        connection = UnrealConnection()

        def connect():
            connection.socket = fake_socket
            connection.connected = True
            return True

        connection.connect = connect
        response = connection.send_command("ping", {"label": "测试"})

        self.assertEqual(response["result"]["value"], 1)
        self.assertEqual(
            fake_socket.sent,
            '{"type":"ping","params":{"label":"测试"}}'.encode("utf-8"),
        )
        self.assertTrue(fake_socket.closed)


if __name__ == "__main__":
    unittest.main()
