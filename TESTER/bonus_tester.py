#!/usr/bin/env python3
"""Standalone bonus tester for this ft_irc server.

It validates the project's FTSEND/raw-data/FTEND relay and the bundled bot's
response to a private !help command.  It uses only the Python standard library.
"""

import argparse
import os
import select
import signal
import socket
import subprocess
import sys
import time


DEFAULT_HOST = "127.0.0.1"
DEFAULT_PORT = 6667
DEFAULT_PASSWORD = "mypass"
DEFAULT_TIMEOUT = 3.0


class TestFailure(Exception):
    pass


class IRCClient:
    def __init__(self, host, port, timeout):
        self.sock = socket.create_connection((host, port), timeout=timeout)
        self.sock.setblocking(False)
        self.timeout = timeout
        self.buffer = bytearray()

    def close(self):
        self.sock.close()

    def send_line(self, line):
        self.sock.sendall((line.rstrip("\r\n") + "\r\n").encode("utf-8"))

    def send_raw(self, data):
        self.sock.sendall(data)

    def _receive(self, timeout):
        readable, _, _ = select.select([self.sock], [], [], timeout)
        if not readable:
            return False
        data = self.sock.recv(4096)
        if not data:
            raise TestFailure("the server closed the connection")
        self.buffer.extend(data)
        return True

    def read_line(self, timeout=None):
        deadline = time.monotonic() + (self.timeout if timeout is None else timeout)
        while True:
            marker = self.buffer.find(b"\n")
            if marker != -1:
                line = bytes(self.buffer[:marker + 1])
                del self.buffer[:marker + 1]
                return line.rstrip(b"\r\n").decode("utf-8", errors="replace")
            remaining = deadline - time.monotonic()
            if remaining <= 0 or not self._receive(remaining):
                raise TestFailure("timed out waiting for an IRC line")

    def wait_for_line(self, text, timeout=None):
        deadline = time.monotonic() + (self.timeout if timeout is None else timeout)
        received = []
        while time.monotonic() < deadline:
            line = self.read_line(max(0.01, deadline - time.monotonic()))
            received.append(line)
            if text in line:
                return line
        raise TestFailure(
            "timed out waiting for {!r}; received: {}".format(text, received)
        )

    def read_exact(self, size, timeout=None):
        deadline = time.monotonic() + (self.timeout if timeout is None else timeout)
        while len(self.buffer) < size:
            remaining = deadline - time.monotonic()
            if remaining <= 0 or not self._receive(remaining):
                raise TestFailure(
                    "timed out waiting for {} raw byte(s); received {}".format(
                        size, len(self.buffer)
                    )
                )
        data = bytes(self.buffer[:size])
        del self.buffer[:size]
        return data


def status(name, passed, detail=""):
    label = "PASS" if passed else "FAIL"
    print("[{}] {}{}".format(label, name, ": " + detail if detail else ""))


def wait_for_server(host, port, timeout):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        try:
            probe = socket.create_connection((host, port), timeout=0.2)
            probe.close()
            return
        except OSError:
            time.sleep(0.05)
    raise TestFailure("could not connect to {}:{}".format(host, port))


def register(client, password, nick):
    client.send_line("PASS " + password)
    client.send_line("NICK " + nick)
    client.send_line("USER {} 0 * :{}".format(nick, nick))
    client.wait_for_line("001 " + nick)


def test_file_transfer(host, port, password, timeout):
    sender = IRCClient(host, port, timeout)
    receiver = IRCClient(host, port, timeout)
    try:
        register(sender, password, "file_sender")
        register(receiver, password, "file_receiver")

        payload = b"ft_irc\x00bonus\r\nraw-data\xff\x10"
        filename = "bonus_payload.bin"
        sender.send_line(
            "FTSEND file_receiver {} {}".format(filename, len(payload))
        )
        receiver.wait_for_line("FTSEND {} {}".format(filename, len(payload)))
        sender.wait_for_line("902 file_sender")

        sender.send_raw(payload)
        relayed = receiver.read_exact(len(payload), timeout)
        if relayed != payload:
            raise TestFailure(
                "raw payload differs: expected {!r}, received {!r}".format(
                    payload, relayed
                )
            )

        sender.send_line("FTEND file_receiver")
        receiver.wait_for_line("FTEND")
        sender.wait_for_line("904 file_sender")
    finally:
        sender.close()
        receiver.close()


def test_bot(host, port, password, bot_nick, timeout):
    client = IRCClient(host, port, timeout)
    try:
        register(client, password, "bot_probe")
        client.send_line("JOIN #bot")
        names = client.wait_for_line("353 bot_probe", timeout)
        if bot_nick not in names:
            raise TestFailure(
                "{} is not present in #bot ({})".format(bot_nick, names)
            )
        client.send_line("PRIVMSG {} :!help".format(bot_nick))
        reply = client.wait_for_line("PRIVMSG bot_probe", timeout)
        if "!help" not in reply:
            raise TestFailure("bot reply does not contain the help command list")
    finally:
        client.close()


def start_process(command, name):
    try:
        return subprocess.Popen(
            command,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
    except OSError as error:
        raise TestFailure("could not start {}: {}".format(name, error))


def stop_process(process):
    if process is None or process.poll() is not None:
        return
    process.send_signal(signal.SIGINT)
    try:
        process.wait(timeout=2)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait()


def parse_args():
    parser = argparse.ArgumentParser(
        description="Test the ft_irc file-transfer and bot bonuses."
    )
    parser.add_argument("--host", default=DEFAULT_HOST)
    parser.add_argument("--port", type=int, default=DEFAULT_PORT)
    parser.add_argument("--password", default=DEFAULT_PASSWORD)
    parser.add_argument("--binary", default="./ircserv")
    parser.add_argument("--bot-binary", default="./ircbot")
    parser.add_argument("--bot-nick", default="ircbot")
    parser.add_argument("--timeout", type=float, default=DEFAULT_TIMEOUT)
    parser.add_argument(
        "--mode", choices=("all", "file", "bot"), default="all",
        help="Run both checks or just one of them.",
    )
    parser.add_argument(
        "--no-start", action="store_true",
        help="Use an already-running IRC server.",
    )
    parser.add_argument(
        "--no-bot-start", action="store_true",
        help="Use an already-running bot.",
    )
    return parser.parse_args()


def main():
    args = parse_args()
    server = None
    bot = None
    failures = 0

    try:
        if not args.no_start:
            server_path = os.path.abspath(args.binary)
            server = start_process(
                [server_path, str(args.port), args.password], "IRC server"
            )
        wait_for_server(args.host, args.port, args.timeout)

        if args.mode in ("all", "file"):
            try:
                test_file_transfer(
                    args.host, args.port, args.password, args.timeout
                )
                status("File transfer: notification, raw payload, completion", True)
            except (OSError, TestFailure) as error:
                failures += 1
                status("File transfer: notification, raw payload, completion", False,
                       str(error))

        if args.mode in ("all", "bot"):
            if not args.no_bot_start:
                bot_path = os.path.abspath(args.bot_binary)
                bot = start_process(
                    [bot_path, args.host, str(args.port), args.password], "IRC bot"
                )
                time.sleep(0.2)
            try:
                test_bot(
                    args.host, args.port, args.password, args.bot_nick, args.timeout
                )
                status("Bot: private !help reply", True)
            except (OSError, TestFailure) as error:
                failures += 1
                status("Bot: private !help reply", False, str(error))
    except TestFailure as error:
        failures += 1
        status("Test setup", False, str(error))
    finally:
        stop_process(bot)
        stop_process(server)

    print("\n{} bonus check(s) failed.".format(failures) if failures
          else "\nBonus suite finished cleanly.")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
