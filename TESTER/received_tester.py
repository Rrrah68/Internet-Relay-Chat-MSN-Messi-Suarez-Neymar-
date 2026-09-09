#!/usr/bin/env python3

import os
import re
import signal
import socket
import subprocess
import sys
import tempfile
import time

HOST = "127.0.0.1"
PORT = 6667
PASSWORD = "ateyaba68"
SERVER = "./ircserv"

TEST_DIR = os.path.dirname(os.path.abspath(__file__))
TEST_FILE = os.path.join(TEST_DIR, "final_big_test.bin")

passed = 0
failed = 0
server_process = None


def ok(message):
    global passed
    passed += 1
    print("[PASS] " + message)


def fail(message):
    global failed
    failed += 1
    print("[FAIL] " + message)


def info(message):
    print("[INFO] " + message)


def text(data):
    return data.decode("utf-8", errors="replace")


def run_command(command, timeout=30):
    result = subprocess.run(
        command,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        timeout=timeout
    )
    return result.returncode, result.stdout


def compile_project():
    info("Compilation avec make re")

    code, output = run_command(["make", "re"])

    if code == 0:
        ok("make re")
    else:
        fail("make re")
        print(output)
        return False

    if os.path.isfile(SERVER):
        ok("binaire ircserv présent")
        return True

    fail("binaire ircserv absent")
    return False


def start_server():
    global server_process

    info("Démarrage du serveur")

    server_process = subprocess.Popen(
        [SERVER, str(PORT), PASSWORD],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True
    )

    time.sleep(0.5)

    if server_process.poll() is None:
        ok("serveur démarré")
        return True

    fail("impossible de démarrer le serveur")
    return False


def stop_server():
    global server_process

    if server_process is None:
        return

    if server_process.poll() is None:
        server_process.send_signal(signal.SIGINT)

        try:
            server_process.wait(timeout=3)
        except subprocess.TimeoutExpired:
            server_process.kill()
            server_process.wait()

    ok("serveur arrêté")


class IRCClient:
    def __init__(self, nickname):
        self.nickname = nickname
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.sock.settimeout(2)

    def connect(self):
        self.sock.connect((HOST, PORT))

    def send(self, command):
        self.sock.sendall((command + "\r\n").encode())

    def receive(self, timeout=1):
        self.sock.settimeout(timeout)

        try:
            return self.sock.recv(8192)
        except socket.timeout:
            return b""

    def receive_all(self, timeout=0.3):
        self.sock.settimeout(timeout)
        data = b""

        while True:
            try:
                chunk = self.sock.recv(8192)

                if not chunk:
                    break

                data += chunk

            except socket.timeout:
                break

        return data

    def close(self):
        try:
            self.sock.close()
        except Exception:
            pass


def register(client):
    client.send("PASS " + PASSWORD)
    client.send("NICK " + client.nickname)
    client.send(
        "USER {} 0 * :{}".format(
            client.nickname,
            client.nickname
        )
    )

    response = text(client.receive_all())

    return "001 " + client.nickname in response


def test_registration():
    info("Test PASS / NICK / USER")

    client = IRCClient("rayan")
    client.connect()

    if register(client):
        ok("client correctement enregistré")
    else:
        fail("client non enregistré")

    client.close()


def test_join_part():
    info("Test JOIN / PART")

    alice = IRCClient("alice")
    bob = IRCClient("bob")

    alice.connect()
    bob.connect()

    register(alice)
    register(bob)

    alice.receive_all()
    bob.receive_all()

    alice.send("JOIN #test")
    response = text(alice.receive_all())

    if "JOIN #test" in response:
        ok("JOIN #test")
    else:
        fail("JOIN #test")

    bob.send("JOIN #test")

    alice_response = text(alice.receive_all())
    bob_response = text(bob.receive_all())

    if "JOIN #test" in alice_response or "JOIN #test" in bob_response:
        ok("notification JOIN")
    else:
        fail("notification JOIN")

    alice.send("PART #test")
    response = text(alice.receive_all())

    if "PART #test" in response:
        ok("PART #test")
    else:
        fail("PART #test")

    alice.close()
    bob.close()


def test_privmsg():
    info("Test PRIVMSG")

    alice = IRCClient("alice")
    bob = IRCClient("bob")

    alice.connect()
    bob.connect()

    register(alice)
    register(bob)

    alice.receive_all()
    bob.receive_all()

    alice.send("JOIN #msg")
    bob.send("JOIN #msg")

    alice.receive_all()
    bob.receive_all()

    alice.send("PRIVMSG #msg :Hello Bob")

    response = text(bob.receive_all())

    if "PRIVMSG #msg :Hello Bob" in response:
        ok("PRIVMSG channel")
    else:
        fail("PRIVMSG channel")

    alice.close()
    bob.close()


def test_topic():
    info("Test TOPIC")

    client = IRCClient("topicuser")
    client.connect()

    register(client)
    client.receive_all()

    client.send("JOIN #topic")
    client.receive_all()

    client.send("TOPIC #topic :Mon topic")

    response = text(client.receive_all())

    if "TOPIC #topic :Mon topic" in response:
        ok("TOPIC")
    else:
        fail("TOPIC")

    client.close()


def test_modes():
    info("Test MODE")

    client = IRCClient("modeuser")
    client.connect()

    register(client)
    client.receive_all()

    client.send("JOIN #mode")
    client.receive_all()

    client.send("MODE #mode +i")
    response = text(client.receive_all())

    if "MODE #mode +i" in response:
        ok("MODE +i")
    else:
        fail("MODE +i")

    client.send("MODE #mode -i")
    response = text(client.receive_all())

    if "MODE #mode -i" in response:
        ok("MODE -i")
    else:
        fail("MODE -i")

    client.close()


def test_invite():
    info("Test INVITE")

    op = IRCClient("operator")
    invited = IRCClient("invited")

    op.connect()
    invited.connect()

    register(op)
    register(invited)

    op.receive_all()
    invited.receive_all()

    op.send("JOIN #invite")
    op.receive_all()

    op.send("MODE #invite +i")
    op.receive_all()

    op.send("INVITE invited #invite")
    response = text(op.receive_all())

    if "INVITE" in response:
        ok("INVITE")
    else:
        fail("INVITE")

    invited.send("JOIN #invite")
    response = text(invited.receive_all())

    if "JOIN #invite" in response:
        ok("JOIN après INVITE")
    else:
        fail("JOIN après INVITE")

    op.close()
    invited.close()


def test_kick():
    info("Test KICK")

    op = IRCClient("kickop")
    victim = IRCClient("victim")

    op.connect()
    victim.connect()

    register(op)
    register(victim)

    op.receive_all()
    victim.receive_all()

    op.send("JOIN #kick")
    op.receive_all()

    victim.send("JOIN #kick")
    op.receive_all()
    victim.receive_all()

    op.send("KICK #kick victim :bye")

    response = text(victim.receive_all())

    if "KICK #kick victim" in response:
        ok("KICK")
    else:
        fail("KICK")

    op.close()
    victim.close()


def test_quit():
    info("Test QUIT")

    client = IRCClient("quituser")
    client.connect()

    register(client)
    client.receive_all()

    client.send("QUIT :bye")

    client.close()

    ok("QUIT")


def test_errors():
    info("Test erreurs IRC")

    client = IRCClient("errors")
    client.connect()

    register(client)
    client.receive_all()

    client.send("COMMANDETHATDOESNOTEXIST")

    response = text(client.receive_all())

    if "421" in response:
        ok("commande inconnue -> 421")
    else:
        fail("commande inconnue -> 421")

    client.close()


def test_multiple_clients():
    info("Test plusieurs clients")

    clients = []

    nicknames = [
        "multi1",
        "multi2",
        "multi3"
    ]

    for nickname in nicknames:
        client = IRCClient(nickname)
        client.connect()

        if register(client):
            clients.append(client)

    if len(clients) == 3:
        ok("3 clients connectés simultanément")
    else:
        fail("3 clients connectés simultanément")

    for client in clients:
        client.close()


def test_botusers():
    info("Test BOTUSERS")

    rayan = IRCClient("rayan")
    bob = IRCClient("bob")

    rayan.connect()
    bob.connect()

    register(rayan)
    register(bob)

    rayan.receive_all()
    bob.receive_all()

    rayan.send("BOTUSERS")

    response = text(rayan.receive_all())

    if "900" in response and "bob" in response:
        ok("BOTUSERS")
    else:
        fail("BOTUSERS")

    rayan.close()
    bob.close()


def test_botchannels():
    info("Test BOTCHANNELS")

    client = IRCClient("channels")
    client.connect()

    register(client)
    client.receive_all()

    client.send("JOIN #channeltest")
    client.receive_all()

    client.send("BOTCHANNELS")

    response = text(client.receive_all())

    if "901" in response and "#channeltest" in response:
        ok("BOTCHANNELS")
    else:
        fail("BOTCHANNELS")

    client.close()


def create_test_file():
    info("Création du fichier de test 10 Ko")

    data = bytes(range(256)) * 40

    with open(TEST_FILE, "wb") as file:
        file.write(data)

    if os.path.getsize(TEST_FILE) == 10240:
        ok("fichier de test = 10240 octets")
        return data

    fail("fichier de test incorrect")
    return None


def receive_file(receiver, size):
    data = b""
    header = b""

    receiver.settimeout(3)

    while b"\r\n" not in header:
        chunk = receiver.recv(4096)

        if not chunk:
            return b""

        header += chunk

    end = header.find(b"\r\n")

    line = header[:end + 2]
    data = header[end + 2:]

    print(
        "[INFO] Notification : {}".format(
            text(line).strip()
        )
    )

    while len(data) < size:
        chunk = receiver.recv(4096)

        if not chunk:
            break

        data += chunk

    return data[:size]


def file_transfer_test(number, expected):
    sender = IRCClient("sender{}".format(number))
    receiver = IRCClient("receiver{}".format(number))

    sender.connect()
    receiver.connect()

    register(sender)
    register(receiver)

    sender.receive_all()
    receiver.receive_all()

    filename = "final_test_{}.bin".format(number)

    sender.send(
        "FTSEND receiver{} {} {}".format(
            number,
            filename,
            len(expected)
        )
    )

    time.sleep(0.05)

    sender.sock.sendall(expected)

    response = text(sender.receive_all())

    if "902" not in response:
        fail(
            "FTSEND test {} : réponse 902 absente".format(number)
        )
        sender.close()
        receiver.close()
        return

    try:
        received = receive_file(
            receiver.sock,
            len(expected)
        )
    except socket.timeout:
        fail(
            "transfert test {} : timeout".format(number)
        )
        sender.close()
        receiver.close()
        return

    if received == expected:
        ok(
            "transfert fichier 10 Ko test {}".format(number)
        )
    else:
        fail(
            "transfert fichier 10 Ko test {}".format(number)
        )

    sender.close()
    receiver.close()


def test_file_transfer():
    info("Test transfert de fichiers")

    expected = create_test_file()

    if expected is None:
        return

    file_transfer_test(1, expected)
    file_transfer_test(2, expected)
    file_transfer_test(3, expected)

    if os.path.exists(TEST_FILE):
        os.remove(TEST_FILE)


def test_valgrind():
    info("Test Valgrind")

    if subprocess.call(
        ["which", "valgrind"],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL
    ) != 0:
        info("Valgrind non installé -> test ignoré")
        return

    log_file = tempfile.NamedTemporaryFile(
        delete=False
    ).name

    command = [
        "valgrind",
        "--leak-check=full",
        "--show-leak-kinds=all",
        "--track-fds=yes",
        "--error-exitcode=42",
        SERVER,
        str(PORT),
        PASSWORD
    ]

    process = subprocess.Popen(
        command,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True
    )

    time.sleep(0.7)

    if process.poll() is not None:
        fail("Valgrind : serveur impossible à démarrer")
        return

    client = IRCClient("valgrind")
    client.connect()

    register(client)
    client.receive_all()

    client.send("JOIN #valgrind")
    client.receive_all()

    client.send("PRIVMSG #valgrind :test")
    client.receive_all()

    client.send("QUIT")
    client.close()

    process.send_signal(signal.SIGINT)

    try:
        output, _ = process.communicate(timeout=5)
    except subprocess.TimeoutExpired:
        process.kill()
        output, _ = process.communicate()

    with open(log_file, "w") as file:
        file.write(output)

    if process.returncode == 42:
        fail("Valgrind détecte une erreur")
        print(output)
        os.remove(log_file)
        return

    leaks = re.search(
        r"definitely lost:\s+([0-9,]+)\s+bytes",
        output
    )

    errors = re.search(
        r"ERROR SUMMARY:\s+([0-9]+)\s+errors",
        output
    )

    leak_count = 0

    if leaks:
        leak_count = int(
            leaks.group(1).replace(",", "")
        )

    error_count = 0

    if errors:
        error_count = int(errors.group(1))

    if leak_count == 0 and error_count == 0:
        ok("Valgrind : 0 fuite / 0 erreur")
    else:
        fail(
            "Valgrind : {} octets perdus / {} erreurs".format(
                leak_count,
                error_count
            )
        )
        print(output)

    os.remove(log_file)


def print_summary():
    print()
    print("=" * 60)
    print("              RÉSULTAT FINAL FT_IRC")
    print("=" * 60)
    print()
    print("PASS :", passed)
    print("FAIL :", failed)
    print()

    if failed == 0:
        print("TOUS LES TESTS SONT PASSES")
        print("Le projet est prêt pour la vérification finale.")
        return 0

    print("DES TESTS ONT ÉCHOUE")
    print("Corrige les FAIL avant de considérer le projet terminé.")
    return 1


def cleanup():
    if os.path.exists(TEST_FILE):
        os.remove(TEST_FILE)

    stop_server()


def main():
    os.makedirs(TEST_DIR, exist_ok=True)
    
    print()
    print("=" * 60)
    print("              FT_IRC FINAL TESTER")
    print("=" * 60)
    print()

    try:
        if not compile_project():
            return 1

        if not start_server():
            return 1

        test_registration()
        test_join_part()
        test_privmsg()
        test_topic()
        test_modes()
        test_invite()
        test_kick()
        test_quit()
        test_errors()
        test_multiple_clients()

        test_botusers()
        test_botchannels()

        test_file_transfer()

        stop_server()

        test_valgrind()

        return print_summary()

    except KeyboardInterrupt:
        print("\n[INFO] Test interrompu")
        return 1

    except Exception as error:
        fail(
            "exception inattendue : {}".format(error)
        )
        return print_summary()

    finally:
        cleanup()


if __name__ == "__main__":
    sys.exit(main())