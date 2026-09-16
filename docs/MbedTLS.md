# Mbed TLS

Pinned version: **3.6.7**, from the 3.6 LTS branch.

Official archive:
https://github.com/Mbed-TLS/mbedtls/releases/download/mbedtls-3.6.7/mbedtls-3.6.7.tar.bz2

SHA-256 verified against the official checksum file:
`a7e8bcbec0e6f761b4af24f25677626b35f762f68eef79c08677a363212d11f6`

Briefcase uses the **Apache-2.0** license option. The upstream
`third_party/mbedtls-3.6.7/LICENSE` file is preserved and distributed as
`Briefcase/Licenses/MbedTLS.txt`.

The upstream code is unchanged. `runtime/Briefcase.Admin/TlsConfig.h` configures
standard C++ mutexes and disables persistent PSA storage, DTLS, early data and
renegotiation. Each connection owns a TLS context. Shared PSA structures are
protected by mutex callbacks configured once when the service starts.

Mbed TLS is linked statically, so the game loads no additional third-party DLL.
RSA/X.509 generation, SHA-256, PBKDF2-HMAC-SHA256, buffer clearing and random-number
generation use Mbed TLS through the independent `Briefcase.Admin.Crypto` target.
The library selects the operating system entropy source. This module contains no
Windows API calls.

The current persistent storage remains isolated in the platform identity-storage
implementation. Windows installations use DPAPI in
`runtime/Briefcase.Admin/WindowsIdentity.cpp`; Linux installations use restrictive
file permissions. Transport and secure file operations also have platform-specific
implementations.
