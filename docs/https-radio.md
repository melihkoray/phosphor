# HTTPS radio regression

Tested on a Waveshare ESP32-S3-Touch-LCD-3.49 V2 with Arduino ESP32 3.3.12.
The configured test stream was `http://ic1.smcdn.pl/2360-1.mp3`: HTTP played,
while the equivalent HTTPS URL failed on the board but worked on a desktop.

## Cause and fix

The vendored audio library initializes both I2S TX and RX. Phosphor only uses
TX, but RX reserves about 32 KiB of internal DMA buffers plus driver overhead.
The core allocates mbedTLS memory internally, so free PSRAM does not satisfy
those allocations. Before the fix, TLS failed with:

```text
(-32512) SSL - Memory allocation failed
```

Make RX optional (enabled by default for existing library callers) and select
TX-only in Phosphor. All RX operations tolerate a missing channel. This leaves
room for TLS and allows the built-in CA bundle to replace `setInsecure()`.
Hostnames still reach `NetworkClientSecure::connect`, preserving SNI and name
verification. TCP and handshake limits are set separately, and a redirect can
reuse a connection only when its hostname, scheme and port match.

With the test firmware, internal free memory before HTTPS increased from
38,408 to 75,388 bytes; the first ESKA TLS connection took 1,941 ms. The
requested ESKA URL returned 200 `audio/mpeg`, Icecast metadata and MP3 audio
at 128 kbit/s, 48 kHz stereo, without a redirect. Desktop OpenSSL verified its
certificate chain and hostname with TLS 1.2.

## On-device checks

The following checks were run on 2026-11-25, with four status samples at
four-second intervals per URL. Positive results included MP3 decoder logs
and activity from decoded PCM in the spectrum callback. Redirected stations
also delivered ICY track titles.

| URL | Result |
|---|---|
| `http://ic1.smcdn.pl/2360-1.mp3` | MP3 playback |
| `https://ic1.smcdn.pl/2360-1.mp3` | Verified TLS and MP3 playback |
| `https://stream.nightride.fm/nightride.mp3` | Verified TLS, chunked MP3, 320 kbit/s, 44.1 kHz |
| `https://media-ssl.musicradio.com/Heart80sMP3` | Verified TLS, MP3, 44.1 kHz |
| `https://streams.80s80s.de/web/mp3-128` | HTTPS redirect, MP3 and ICY titles |
| `http://streams.80s80s.de/web/mp3-128` | HTTP redirect, MP3 and ICY titles |
| `https://streams.fluxfm.de/Chillhop/mp3-128/streams.fluxfm.de/` | HTTPS redirect, MP3 and ICY titles |
| `https://212.180.238.42/2360-1.mp3` | Rejected: hostname mismatch, X509 error -9984 |
| `https://self-signed.badssl.com/` | Rejected: untrusted certificate, bundle verification error |

The hardware run used a firmware with these playback, trust and timeout
changes. This isolated PR was built separately; it additionally keeps verbose
connection/heap logs at debug level Info or above and preserves the vendored
library's existing HTTPS port-80 workaround. The URLs tested above do not use
an explicit HTTPS port 80.

A correct system time is required for certificate validity checks; existing
SNTP setup is retained. No insecure fallback is used. No independent acoustic
measurement or multi-hour soak test was performed. Saved radio settings were
unchanged after the tests.
