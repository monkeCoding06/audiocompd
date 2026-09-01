# <img src="branding/audiocompd.png" alt="audiocompd logo" width="56" align="center"> audiocompd

`audiocompd` is a Linux real-time audio compressor designed to run as a
systemd user service. Its PipeWire backend appears to desktop applications as
a virtual output device, compresses everything routed to it, and sends the
result to a configured physical output. The DSP core remains independent of
the audio server, and native ALSA and JACK backends are also available.

The project intentionally produces two executables:

- `audiocompd` — the service application
- `unit_test` — the self-contained unit-test executable

## Current features

- PipeWire virtual sink for system-wide desktop playback compression
- Explicit PipeWire target sink, preventing the processed stream from feeding
  back into the virtual sink
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
- PipeWire development files when `AUDIOCOMPD_WITH_PIPEWIRE=ON`

On Debian or Ubuntu the development packages are commonly installed with:

```bash
sudo apt install build-essential cmake ninja-build pkg-config \
    libxml2-dev libasound2-dev libjack-jackd2-dev libpipewire-0.3-dev
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
    -DAUDIOCOMPD_WITH_JACK=OFF \
    -DAUDIOCOMPD_WITH_PIPEWIRE=ON
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

Run with the PipeWire virtual-sink example:

```bash
./build/audiocompd \
    --config config/audiocompd.xml \
    --schema schema/audiocompd.xsd
```

While it is running, `wpctl status` should show an output named
`audiocompd Compressed Output`. Make it the default output using its numeric
sink ID:

```bash
wpctl set-default SINK_ID
```

On older WirePlumber versions that refuse to select a virtual node, use the
PipeWire PulseAudio compatibility command instead:

```bash
pactl set-default-sink audiocompd
```

Restart or move any already-running playback streams after changing the
default. New PipeWire and PulseAudio-compatible applications will then play
through the compressor.

## Configuration

The XSD permits exactly one backend below `<backend>`.

The standard installed paths are:

```text
/etc/audiocompd/audiocompd.xml
/usr/share/audiocompd/audiocompd.xsd
```

### PipeWire

```xml
<backend>
    <pipewire>
        <nodeName>audiocompd</nodeName>
        <nodeDescription>audiocompd Compressed Output</nodeDescription>
        <targetSink>alsa_output.pci-0000_0a_00.4.analog-stereo</targetSink>
        <sampleRate>48000</sampleRate>
        <channels>2</channels>
        <quantum>256</quantum>
    </pipewire>
</backend>
```

`targetSink` must be the `node.name` of the real hardware output, not the
virtual audiocompd sink. Find it with:

```bash
wpctl status
wpctl inspect SINK_ID | rg 'node.name|node.description|media.class'
```

The included configuration targets the Matisse analog output shown above.
Change it when using the project on another machine. The PipeWire backend
currently accepts one or two channels and uses interleaved 32-bit float audio
at its PipeWire boundary.

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
journalctl --user -u audiocompd
```

An optional `<filePath>` after `<level>` additionally writes the same messages
to a file. File logging should normally be left disabled for the systemd
service.

## Install as a systemd user service

```bash
sudo cmake --install build --prefix /usr
systemctl --user daemon-reload
systemctl --user enable --now audiocompd
```

Check its state and logs:

```bash
systemctl --user status audiocompd
journalctl --user -u audiocompd -f
```

The service is intentionally a user unit: PipeWire and WirePlumber normally
run in the logged-in user's session, and the user service automatically
inherits the correct runtime directory and access to that audio graph. No
username or numeric UID is hard-coded into the project.

This covers applications routed through PipeWire, including its PulseAudio and
JACK compatibility layers. A program that opens an ALSA `hw:` device directly
bypasses the PipeWire graph and therefore cannot be intercepted by the virtual
sink.

## Source layout

```text
src/application    service lifecycle
src/audio          backend-neutral audio engine and factory
src/audio/backends ALSA, JACK, and PipeWire adapters
src/compressor     real-time DSP
src/config         XML/XSD-backed typed configuration
src/logging        project logger
src/runtime        POSIX signal handling
tests              hardware-independent unit tests
```

No logging, allocation, file access, or locking occurs inside the successful
real-time processing path. Buffer storage is allocated before processing
begins.

