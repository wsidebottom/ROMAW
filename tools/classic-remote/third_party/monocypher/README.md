# Monocypher 4.0.3

Unmodified `src/monocypher.c`, `src/monocypher.h`,
`src/optional/monocypher-ed25519.c`, `src/optional/monocypher-ed25519.h` and
`LICENCE.md` from the [official release](https://monocypher.org/download/).
Only the Ed25519 verifier and its dependencies enter the Mac application.
Native Linux and Windows libraries provide key generation/signing for the local
build/controller. Vendor files retain upstream formatting and license notices.

Downloaded `https://monocypher.org/download/monocypher-4.0.3.tar.gz`; verified
against the SHA-512 published alongside that archive:

```text
40904ada5c7ee4f7741733e38b69a30a4b0561cbffba5ffe7c2dce16136d540251ec0d9056ff606510d3b5b708fb8a40db7e0870d4a0b2dc17ba2bfb880f8965
```

The upstream dual BSD-2-Clause/CC0 licensing is preserved in `LICENCE.md`.
