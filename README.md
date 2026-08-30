# audiocompd

`audiocompd` is a Linux real-time audio compressor designed to run as a
systemd service. Its DSP core is independent of the audio server. The first
version includes native ALSA and JACK backends; additional backends can be
added by implementing `AudioBackend` and registering it in
`AudioBackendFactory`.

The project intentionally produces two executables:

- `audiocompd` — the service application
- `unit_test` — the self-contained unit-test executable

## Current features

- ALSA full-duplex capture and playback
- JACK client with optional physical-port auto-connect
- Stereo-linked soft-knee compressor
- XML configuration validated against an XSD before devices are opened
- Thread-safe stderr/file logger; stderr is captured by the systemd journal
- `SIGINT` and `SIGTERM` shutdown using `sigwait`
- Optional compile-time audio backends
- Hardware-independent DSP and engine tests

## Dependencies

- CMake 3.20 or newer
- A C++17 compiler
- pthreads
- libxml2 development files
- pkg-config
- ALSA development files when `AUDIOCOMPD_WITH_ALSA=ON`
- JACK development files when `AUDIOCOMPD_WITH_JACK=ON`

On Debian or Ubuntu the development packages are commonly installed with:

```bash
sudo apt install build-essential cmake ninja-build pkg-config \
    libxml2-dev libasound2-dev libjack-jackd2-dev
```

## Build and test

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

Backends can be disabled independently:

```bash
cmake -S . -B build -G Ninja \
    -DAUDIOCOMPD_WITH_ALSA=ON \
    -DAUDIOCOMPD_WITH_JACK=OFF
```

Disabling a backend removes its external dependency. At least one usable
backend should be enabled for the service executable.

## Run locally

Validate the example configuration without opening an audio device:

```bash
./build/audiocompd \
    --config config/audiocompd.xml \
    --schema schema/audiocompd.xsd \
    --validate-config
```

Run with the ALSA example:

```bash
./build/audiocompd \
    --config config/audiocompd.xml \
    --schema schema/audiocompd.xsd
```

The default ALSA device can itself be provided by PipeWire or PulseAudio via
the system's ALSA plugin configuration. PipeWire's JACK compatibility can also
be used through the native JACK backend.

## Configuration

The XSD permits exactly one backend below `<backend>`.

The standard installed paths are:

```text
/etc/audiocompd/audiocompd.xml
/usr/share/audiocompd/audiocompd.xsd
```

### ALSA

```xml
<backend>
    <alsa>
        <inputDevice>default</inputDevice>
        <outputDevice>default</outputDevice>
        <sampleRate>48000</sampleRate>
        <channels>2</channels>
        <periodFrames>256</periodFrames>
        <periods>4</periods>
    </alsa>
</backend>
```

ALSA samples are converted from signed 16-bit interleaved PCM into the
backend-neutral planar floating-point representation used by the DSP.

### JACK

```xml
<backend>
    <jack>
        <clientName>audiocompd</clientName>
        <channels>2</channels>
        <autoConnect>true</autoConnect>
    </jack>
</backend>
```

When `autoConnect` is true and no explicit ports are listed, audiocompd
connects to the first matching physical capture and playback ports. Explicit
ports can be provided by repeating `<inputPort>` and `<outputPort>` after
`<autoConnect>`.

JACK supplies its own sample rate and buffer size, so those values are not part
of its XML configuration.

### Compressor

```xml
<compressor enabled="true">
    <thresholdDb>-18.0</thresholdDb>
    <ratio>4.0</ratio>
    <attackMs>10.0</attackMs>
    <releaseMs>100.0</releaseMs>
    <kneeDb>6.0</kneeDb>
    <makeupGainDb>0.0</makeupGainDb>
</compressor>
```

All channels share the same detected peak and gain reduction. This preserves
the stereo image rather than compressing the left and right channels
independently.

### Logging

```xml
<logging>
    <level>info</level>
</logging>
```

Logs always go to stderr. Under systemd they can be viewed with:

```bash
journalctl -u audiocompd
```

An optional `<filePath>` after `<level>` additionally writes the same messages
to a file. File logging should normally be left disabled for the systemd
service.

## Install as a system service

```bash
sudo cmake --install build --prefix /usr
sudo useradd --system --no-create-home --shell /usr/sbin/nologin audiocompd
sudo usermod -aG audio audiocompd
sudo systemctl daemon-reload
sudo systemctl enable --now audiocompd
```

Check its state and logs:

```bash
systemctl status audiocompd
journalctl -u audiocompd -f
```

ALSA is the simplest backend for a system-level service. JACK or a native
PipeWire backend must be able to reach the audio-server socket belonging to the
service user. This is a deployment concern and does not change the DSP or audio
engine.

## Source layout

```text
src/application    service lifecycle
src/audio          backend-neutral audio engine and factory
src/audio/backends ALSA and JACK adapters
src/compressor     real-time DSP
src/config         XML/XSD-backed typed configuration
src/logging        project logger
src/runtime        POSIX signal handling
tests              hardware-independent unit tests
```

No logging, allocation, file access, or locking occurs inside the successful
real-time processing path. Buffer storage is allocated before processing
begins.

