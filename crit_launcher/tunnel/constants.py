"""e4mc endpoints, the pinned relay helper and the log patterns that reveal a LAN port."""

import re


# e4mcbiat is the standalone client for the e4mc QUIC relay ("e4mc but it's a tool").
# It is downloaded on first use; the launcher does not bundle it.
E4MC_BIAT_VERSION = "0.2.2"
E4MC_BIAT_URL = ("https://github.com/DuncanRuns/e4mcbiat/releases/download/"
                 f"v{E4MC_BIAT_VERSION}/e4mcbiat-{E4MC_BIAT_VERSION}-all.jar")
E4MC_BIAT_SHA256 = "3a4a6fbb79c5da44ce3f6da06689bdbbc36bc933896c5c2735d501604aa297a5"
E4MC_BIAT_FILENAME = f"e4mcbiat-{E4MC_BIAT_VERSION}-all.jar"
E4MC_MIN_JAVA = 11

# Line printed by e4mcbiat once the relay assigns a public domain.
E4MC_DOMAIN_RE = re.compile(r"Connected,\s*domain:\s*(\S+)")

# Minecraft writes one of these when a singleplayer world is opened to LAN.
LAN_PORT_PATTERNS = (
    re.compile(r"Local game hosted on port\s+(\d+)", re.IGNORECASE),
    re.compile(r"Started serving on\s+(\d+)", re.IGNORECASE),
    re.compile(r"hosting (?:the )?(?:local )?game on port\s+(\d+)", re.IGNORECASE),
)
