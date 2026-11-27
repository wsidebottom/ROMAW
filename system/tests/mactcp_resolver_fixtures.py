#!/usr/bin/env python3
# +===================================================================
#   File:      mactcp_resolver_fixtures.py
#
#   Summary:   Prepare bounded valid and malformed Hosts files for the real MacTCP guest resolver.
#
#   Classes:   None.
#
#   Functions: main.
#
#   Origin:    ROMAW resolver regression fixtures; no Apple binary inputs.
#
#   Copyright and Legal notices: See COPYING.
# ===================================================================+
from pathlib import Path


def main():
    output = Path(__file__).resolve().parents[2] / "build/mactcp"
    output.mkdir(parents=True, exist_ok=True)
    (output / "resolver-hosts.txt").write_text(
        "# BSD and classic DNS Hosts records\n"
        "127.0.0.1 loopback.fixture alias.fixture\n"
        "a.fixture IN A 127.0.0.2\n"
        "canonical.fixture A 127.0.0.3\n"
        "cname.fixture CNAME canonical.fixture\n"
        'cpu.fixture HINFO "Motorola 68040" "Mac OS"\n'
        "mail.fixture MX 20 secondary.fixture\n"
        "mail.fixture MX 10 primary.fixture\n"
        "1.0.0.127.in-addr.arpa PTR loopback.fixture\n"
        "domain.fixture NS ns.fixture\n",
        encoding="ascii",
    )
    (output / "resolver-cycle.txt").write_text(
        "a.fixture CNAME b.fixture\nb.fixture CNAME a.fixture\n", encoding="ascii"
    )
    (output / "resolver-malformed.txt").write_bytes(b"127.0.0.1 ok.fixture\0hidden")
    (output / "resolver-too-large.txt").write_bytes(b"#" * (1024 * 1024))
    print("Prepared MacTCP valid, cyclic, embedded-NUL and oversized Hosts fixtures")


if __name__ == "__main__":
    main()
