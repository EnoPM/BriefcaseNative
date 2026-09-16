# Portable administration identity storage

The administration password and the TLS private key are separate secrets.

## Administration password

The administrator chose to store the password as plain text in the `password`
field of `Briefcase/Admin/server.json` configuration version 2. At startup, the
service derives a PBKDF2 verifier in memory. Network exchanges remain protected by
TLS after certificate verification. This choice does not change how the TLS private
key is protected.

## TLS private key

Windows protects the existing private key with DPAPI for the account that configured
the server. Linux stores the standard PEM key in `Admin/tls-private-key.pem`, with
access limited to the server account and system administrators. The private
directory uses mode `0700` and the key uses mode `0600`.

The configuration references only a relative filename and also stores the public
certificate, salt, verifier, address and port. The private key is excluded from user
packages and Git.

The server starts without requesting another secret. Moving a Linux installation or
its backup requires preserving the file owner and permissions. A person who obtains
the private key or a backup containing it can use the key without the original
machine account, so filesystem permissions and backup controls protect the secret
at rest. TLS traffic remains encrypted.

No client automatically trusts a new certificate. Replacing or migrating an
identity requires the administrator to approve the new certificate fingerprint.

## Optional encrypted storage

An installation may instead use a PKCS#8 private key encrypted by a passphrase
provided at startup or by an independent secret manager. This preserves encryption
at rest but requires a defined source for the passphrase. Keeping a second
decryption key beside the encrypted file does not meaningfully protect against an
attacker who can copy both files.
