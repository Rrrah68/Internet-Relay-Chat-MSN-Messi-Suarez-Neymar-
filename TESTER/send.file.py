#!/usr/bin/env python3

import socket
import sys


HOST = "127.0.0.1"
PORT = 6667
PASSWORD = "ateyaba68"


def send_command(sock, command):
	sock.sendall((command + "\r\n").encode())


def connect_client(nickname):
	sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
	sock.connect((HOST, PORT))

	send_command(sock, "PASS " + PASSWORD)
	send_command(sock, "NICK " + nickname)
	send_command(sock, "USER " + nickname + " 0 * :" + nickname)

	return sock


def receive_file(receiver, file_size, filename):
	received_data = b""
	header = b""

	receiver.settimeout(2)

	while b"\r\n" not in header:
		chunk = receiver.recv(4096)
		if not chunk:
			return b""
		header += chunk

	header_end = header.find(b"\r\n")
	header = header[:header_end + 2]

	print("[INFO] Notification reçue : {}".format(
		header.decode("utf-8", errors="replace").strip()
	))

	remaining_data = header[header_end + 2:]

	if len(remaining_data) > 0:
		received_data += remaining_data

	while len(received_data) < file_size:
		chunk = receiver.recv(4096)

		if not chunk:
			break

		received_data += chunk

	received_data = received_data[:file_size]

	print(
		"[INFO] Bob a reçu {} / {} octets".format(
			len(received_data),
			file_size
		)
	)

	output_filename = "received_" + filename.split("/")[-1]

	with open(output_filename, "wb") as file:
		file.write(received_data)

	print("[OK] Fichier reçu sauvegardé : {}".format(output_filename))

	return received_data


def send_file(sender, receiver, receiver_name, filename):
	with open(filename, "rb") as file:
		data = file.read()

	file_size = len(data)
	file_name = filename.split("/")[-1]

	send_command(
		sender,
		"FTSEND {} {} {}".format(
			receiver_name,
			file_name,
			file_size
		)
	)

	print("[OK] FTSEND envoyé")
	print("[INFO] Taille : {} octets".format(file_size))

	sender.sendall(data)

	print(
		"[OK] Données envoyées : {} octets".format(
			file_size
		)
	)

	received_data = receive_file(
		receiver,
		file_size,
		filename
	)

	if received_data == data:
		print("[SUCCESS] Les fichiers sont identiques")
	else:
		print("[ERROR] Les fichiers sont différents")


def main():
	if len(sys.argv) != 2:
		print("Usage: ./send.file.py <fichier>")
		return 1

	filename = sys.argv[1]

	sender = connect_client("rayan")
	receiver = connect_client("bob")

	print("[OK] Rayan connecté")
	print("[OK] Bob connecté")

	receiver.settimeout(0.1)

	while True:
		try:
			receiver.recv(4096)
		except socket.timeout:
			break

	receiver.settimeout(None)

	send_file(sender, receiver, "bob", filename)

	sender.close()
	receiver.close()

	return 0


if __name__ == "__main__":
	sys.exit(main())