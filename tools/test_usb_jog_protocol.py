#!/usr/bin/env python3
"""Offline protocol-3 relay checks. No serial devices are opened."""
import unittest
from unittest.mock import patch
from run_usb_jog import relay, identify


class Link:
    def __init__(self, fd=1, messages=()):
        self.fd=fd
        self.pending=list(messages)
        self.sent=[]
        self.buffer=bytearray()
        self.port="fake"
    def send(self, message): self.sent.append(message)
    def read(self): pass
    def receive(self, *_args, **_kwargs): return self.pending.pop(0)
    def messages(self):
        while self.pending: yield self.pending.pop(0)


class ProtocolTest(unittest.TestCase):
    def status(self, **changes):
        return {"type":"AXIS", "state":1, "rev":0, "linked":1, "hz":0,
                "fwd":999, "back":1, "raw":5, "recovery":0,
                "homed":1, "home":5, "fault":0, "zeroed":1, "zero":1, "pos":0, **changes}
    def run_frame(self, frame):
        panel=Link(1)
        axis=Link(2,[frame])
        try:
            with patch("run_usb_jog.select.select",side_effect=[([2],[],[]),KeyboardInterrupt()]), patch("builtins.print"):
                try: relay(panel,axis)
                except KeyboardInterrupt: pass
        finally: self.assertEqual(axis.sent[-1],{"cmd":"STOP"})
        return panel.sent
    def test_homed_and_signed_position(self):
        for position in (0,-123,456):
            sent=self.run_frame(self.status(pos=position))
            self.assertEqual(sent[0]["homed"],1)
            self.assertEqual(sent[0]["pos"],position)
    def test_homing_progress_and_home_pending(self):
        for phase in (1,2,3,4,8,9):
            sent=self.run_frame(self.status(homed=0,home=phase,pos=-25))
            self.assertEqual(sent[0]["home"],phase)
    def test_invalid_reference_or_flags_stops_relay(self):
        for changes in ({"home":2},{"fault":1},{"homed":True},{"pos":1<<63},{"home":10}):
            with self.assertRaises(RuntimeError): self.run_frame(self.status(**changes))
    def test_versions_must_match(self):
        for version in (3,4):
            link=Link(messages=[{"role":"AXIS_C3","firmware":"axis_c3_jog",
                                "protocol":version,"device_id":"E4B323C6AB54"}])
            if version==4: self.assertEqual(identify(link)[0],"AXIS_C3")
            else:
                with self.assertRaises(RuntimeError): identify(link)


if __name__ == "__main__": unittest.main()
