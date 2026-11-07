# Build configuration

These are the measured parent/child configuration contents with **one public-export
substitution**: `CONFIG_MODULE_SIG_KEY` uses the normal relative
`certs/signing_key.pem` instead of the experiment's private absolute filename.
No signing key or certificate is included. Generate an appropriate key for a new
build; do not try to reuse the experiment's key.

The remaining settings are unchanged, and parent/child differ only in their
release suffix. [environment.json](../measurements/environment.json) preserves
the SHA-256 identities of both original, unsanitized measured configs and the
original suffix-normalized comparison. [files.json](../measurements/files.json)
identifies the public copies. These two kinds of hash must not be interchanged.
