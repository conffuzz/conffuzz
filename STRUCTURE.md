# Structure of this Repository

## `Makefile`

- Provides rules to build ConfFuzz and all Docker fuzzing images.
- Usage is documented in the main README.md.

## `conffuzz-dev.dockerfile`

- Main ConfFuzz development environment, used as base by many Docker fuzz environments under `examples/`.

## `examples/`

- Contains all Docker files along with scripts and resources necessary to build them.
- These Docker environments are the ones we used to fuzz the paper's applications.

## `conffuzz/`

- Contains the source of ConfFuzz.

### `conffuzz/include/`

- Contains headers shared by the monitor and the instrumentation.

#### `conffuzz/include/conffuzz.h`

- Specifies the Monitor/Guest pipe communication protocol.

### `conffuzz/conffuzz.cpp`

- Implementation of the ConfFuzz monitor.

### `conffuzz/instrumentation.cpp`

- Implementation of the ConfFuzz Pin-based instrumentation.

### `conffuzz/*.sh`

- Helper scripts used by both monitor and instrumentation to retrieve DWARF type information, function signatures, etc.

### `conffuzz/static-analyze-entry-points.py`

- Static analysis script used to analyze the usage of a given API by an application (entry points, total API elements used).

## `resources/`

- Contains external resources needed to build ConfFuzz.

### `resources/pin-3.21-98484-ge7cd811fd-gcc-linux.tar.gz`

- Copy of Pin version 3.21 in case it gets updated upstream or goes offline.

### `resources/docker-cache @ $commit`

- Git submodule of ConfFuzz itself, at branch `docker-cache`.
- Used as part of the build of Docker fuzz images to execute `-genapi` rules.
- We cannot use the main sources because any changes to them would invalidate the Docker cache and force a rebuild.
