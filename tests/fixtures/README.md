# Project compatibility fixture

`project-v2-before-arp.fzb` is a 635-byte FM1P v2 project exported by this repository at commit `5143211` before the arp format change. It contains one empty drum instrument, its track and an empty pattern; no PCM, user recordings or third-party assets. The test verifies loading an actual earlier project and receiving safe default arp settings, independently of the new writer.
