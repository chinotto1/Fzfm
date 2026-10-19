# FM-1 browser update protocol

`fm1ota.js` is derived from Skiff v1.3.2 at commit
`86d8e09125a42945b0277c6516b1a15a9c53ab3b`, file `web/fm1ota.js`.
`fm1pkg.js` retains the product, logical-image and SHA-256 helpers from that
revision's `web/fm1pkg.js`; package mutation and firmware construction are omitted.

Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments.
GPL-3.0-only. The complete GPL text is included as `../LICENSE.txt` on the site
and at the repository root. Source: https://github.com/illuminus5/skiff.
Upstream hashes and revision are recorded in `third_party/fm1-installer.lock.json`.

Local protocol changes require matching MIDI port pairs and exact target loader
identities, validate identity/request checksums and stages, reject invalid read
ranges and ambiguous ports, release ports after errors, detect changed devices,
and verify the returned firmware identity after normal and resumed transfers.
Timing injection is used only by tests. The application validates whole-package
SHA-256 digests and sizes against its release catalog before entering update mode.
No Skiff firmware binaries, fonts, sounds or SDK resources are bundled.
