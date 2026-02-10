# ZMK Runtime Sensor Rotate Module

This ZMK module provides a runtime-configurable sensor rotation behavior with web UI support.
It allows you to configure sensor rotation bindings per layer via ZMK Studio's custom RPC protocol,
with persistent storage.

## Features

- **Runtime Configuration**: Configure sensor rotation bindings without reflashing firmware
- **Per-Layer Bindings**: Different bindings for each keyboard layer
- **Persistent Storage**: Configuration is saved and restored across reboots
- **Web UI**: Easy-to-use web interface for configuration
- **Deduplication**: Prevents duplicate trigger processing when the same behavior instance is used across layers

## Setup

### 1. Add dependency to your `config/west.yml`

```yaml
manifest:
  remotes:
    ...
    - name: cormoran
      url-base: https://github.com/cormoran
  projects:
    ...
    - name: zmk-behavior-runtime-sensor-rotate
      remote: cormoran
      revision: main # or latest commit hash
    # Required: Use ZMK with custom studio protocol support
    - name: zmk
      remote: cormoran
      revision: v0.3+custom-studio-protocol
      import:
        file: app/west.yml
```

### 2. Enable in your `config/<shield>.conf`

```conf
# Enable ZMK Studio
CONFIG_ZMK_STUDIO=y

# Enable the runtime sensor rotate module
CONFIG_ZMK_RUNTIME_SENSOR_ROTATE=y
CONFIG_ZMK_RUNTIME_SENSOR_ROTATE_STUDIO_RPC=y
CONFIG_ZMK_BEHAVIOR_RUNTIME_SENSOR_ROTATE=y

# Enable settings storage (if not already enabled)
CONFIG_SETTINGS=y
CONFIG_SETTINGS_RUNTIME=y
```

### 3. Add to your keymap

In your `<keyboard>.keymap`, define a runtime sensor rotate behavior instance:

```dts
/ {
    behaviors {
        sensor_rotate_runtime: sensor_rotate_runtime {
            compatible = "zmk,behavior-runtime-sensor-rotate";
            #sensor-binding-cells = <0>;
            tap-ms = <5>;
            layers = <8>;  // Number of layers to support
        };
    };
};
```

Then use it in your sensor bindings:

```dts
&sensors {
    triggers-per-rotation = <20>;
};

&left_encoder {
    bindings = <&sensor_rotate_runtime>;
};
```

## Usage

### Web UI Configuration

1. **Build and deploy the Web UI**:
   ```bash
   cd web
   npm install
   npm run build
   ```

2. **Connect to your keyboard**:
   - Open the web UI in your browser
   - Click "Connect Serial" to connect via USB serial
   - The web UI will detect the custom subsystem

3. **Configure bindings**:
   - Select the sensor index (0 or 1)
   - Click "Load Configuration" to fetch current bindings
   - Select the layer you want to configure
   - Set clockwise and counter-clockwise bindings:
     - Enter behavior name (e.g., "kp" for key press)
     - Set param1 and param2 as needed
   - Click "Save Bindings" to persist the configuration

### Example Bindings

**Volume Control (Layer 0)**:
- Clockwise: `behavior: "kp"`, `param1: 128` (Volume Up)
- Counter-clockwise: `behavior: "kp"`, `param1: 129` (Volume Down)

**Layer Switch (Layer 1)**:
- Clockwise: `behavior: "to"`, `param1: 2` (Switch to layer 2)
- Counter-clockwise: `behavior: "to"`, `param1: 0` (Switch to layer 0)

## Development

### Repository Structure

```
.
├── dts/                    # Device tree bindings
├── include/                # Public header files
├── src/
│   ├── behaviors/         # Behavior implementation
│   └── studio/            # RPC handlers
├── proto/                 # Protocol buffer definitions
└── web/                   # Web UI
    ├── src/
    │   ├── RuntimeSensorRotateConfig.tsx  # Main UI component
    │   └── proto/         # Generated TypeScript types
    └── test/              # Web UI tests
```

### Building Firmware

See the main [ZMK documentation](https://zmk.dev/docs) for building firmware with modules.

### Web UI Development

```bash
cd web
npm install
npm run dev      # Start development server
npm run build    # Build for production
npm test         # Run tests
```

## How It Works

### Firmware

1. **Behavior**: The `zmk,behavior-runtime-sensor-rotate` behavior maintains per-layer bindings in memory and storage
2. **RPC Protocol**: Custom Studio RPC handlers allow the web UI to get/set bindings
3. **Storage**: Zephyr's settings subsystem persists configuration across reboots
4. **Deduplication**: A flag prevents processing the same sensor data multiple times when a behavior instance is shared across layers

### Web UI

1. Connects to the keyboard via WebSerial
2. Calls custom RPC methods to retrieve/update configuration
3. Provides an intuitive interface for configuring bindings per layer

## Technical Details

### Protobuf Messages

- `GetLayerBindingsRequest/Response`: Get bindings for a specific sensor and layer
- `SetLayerBindingsRequest/Response`: Set bindings for a specific sensor and layer
- `GetAllLayerBindingsRequest/Response`: Get bindings for all layers of a sensor

### Storage Format

Bindings are stored in flash using Zephyr's settings subsystem under the key `runtime_sr/bindings`.

### Limitations

- Maximum 2 sensors supported (configurable via `ZMK_RUNTIME_SENSOR_ROTATE_MAX_SENSORS`)
- Maximum 8 layers supported (configurable via `ZMK_RUNTIME_SENSOR_ROTATE_MAX_LAYERS`)
- Behavior names are limited to 32 characters

## More Information

This module is based on the [ZMK Module Template with Custom Studio RPC](https://github.com/cormoran/zmk-module-template-with-custom-studio-rpc).

For more information about ZMK modules:
- [ZMK Module Creation](https://zmk.dev/docs/development/module-creation)
- [Zephyr Modules](https://docs.zephyrproject.org/latest/develop/modules.html)
- [react-zmk-studio](https://github.com/cormoran/react-zmk-studio)

## Development Guide

### Pre-commit Hooks

This repository uses [pre-commit](https://pre-commit.com/) hooks to ensure code quality before commits. The hooks automatically check and fix:

- Trailing whitespace
- End of file fixes
- YAML syntax
- Large files
- Merge conflicts
- **Web code**: Prettier formatting, ESLint linting, Jest tests
- **ZMK module**: Python unit tests

**Setup pre-commit hooks:**

```bash
# Install pre-commit (if not already installed)
pip install pre-commit

# Install the git hooks
pre-commit install
```

**Running pre-commit manually:**

```bash
# Run on all files
pre-commit run --all-files

# Run on staged files only (happens automatically on commit)
pre-commit run
```

The pre-commit hooks are automatically configured in the Copilot GitHub Actions environment and will run on all commits in CI.

### Setup

There are two west workspace layout options.

#### Option1: Download dependencies in parent directory

This option is west's standard way. Choose this option if you want to re-use dependent projects in other zephyr module development.

```bash
mkdir west-workspace
cd west-workspace # this directory becomes west workspace root (topdir)
git clone <this repository>
# rm -r .west # if exists to reset workspace
west init -l . --mf tests/west-test.yml
west update --narrow
west zephyr-export
```

The directory structure becomes like below:

```
west-workspace
  - .west/config
  - build : build output directory
  - <this repository>
  # other dependencies
  - zmk
  - zephyr
  - ...
  # You can develop other zephyr modules in this workspace
  - your-other-repo
```

You can switch between modules by removing `west-workspace/.west` and re-executing `west init ...`.

#### Option2: Download dependencies in ./dependencies (Enabled in dev-container)

Choose this option if you want to download dependencies under this directory (like node_modules in npm). This option is useful for specifying cache target in CI. The layout is relatively easy to recognize if you want to isolate dependencies.

```bash
git clone <this repository>
cd <cloned directory>
west init -l west --mf west-test-standalone.yml
# If you use dev container, start from below commands. Above commands are executed
# automatically.
west update --narrow
west zephyr-export
```

The directory structure becomes like below:

```
<this repository>
  - .west/config
  - build : build output directory
  - dependencies
    - zmk
    - zephyr
    - ...
```

### Dev container

Dev container is configured for setup option2. The container creates below volumes to re-use resources among containers.

- zmk-dependencies: dependencies dir for setup option2
- zmk-build: build output directory
- zmk-root-user: /root, the same to ZMK's official dev container

### Web UI

Please refer [./web/README.md](./web/README.md).

## Test

**ZMK firmware test**

`./tests` directory contains test config for posix to confirm module functionality and config for xiao board to confirm build works.

Tests can be executed by below command:

```bash
# Run all test case and verify results
python -m unittest
```

If you want to execute west command manually, run below. (for zmk-build, the result is not verified.)

```
# Build test firmware for xiao
# `-m tests/zmk-config .` means tests/zmk-config and this repo are added as additional zephyr module
west zmk-build tests/zmk-config/config -m tests/zmk-config .

# Run zmk test cases
# -m . is required to add this module to build
west zmk-test tests -m .
```

**Web UI test**

The `./web` directory includes Jest tests. See [./web/README.md](./web/README.md#testing) for more details.

```bash
cd web
npm test
```

## Publishing Web UI

### GitHub Pages (Production)

Github actions are pre-configured to publish web UI to github pages.

1. Visit Settings>Pages
1. Set source as "Github Actions"
1. Visit Actions>"Test and Build Web UI"
1. Click "Run workflow"

Then, the Web UI will be available in
`https://<your github account>.github.io/<repository name>/` like https://cormoran.github.io/zmk-module-template-with-custom-studio-rpc.

### Cloudflare Workers (Pull Request Preview)

For previewing web UI changes in pull requests:

1. Create a Cloudflare Workers project and configure secrets:

   - `CLOUDFLARE_API_TOKEN`: API token with Cloudflare Pages edit permission
   - `CLOUDFLARE_ACCOUNT_ID`: Your Cloudflare account ID
   - (Optional) `CLOUDFLARE_PROJECT_NAME`: Project name (defaults to `zmk-module-web-ui`)
   - Enable "Preview URLs" feature in cloudflare the project

2. Optionally set up an `approval-required` environment in github repository settings requiring approval from repository owners

3. Create a pull request with web UI changes - the preview deployment will trigger automatically and wait for approval

## Sync changes in template

By running `Actions > Sync Changes in Template > Run workflow`, pull request is created to your repository to reflect changes in template repository.

If the template contains changes in `.github/workflows/*`, registering your github personal access token as `GH_TOKEN` to repository secret is required.
The fine-grained token requires write to contents, pull-requests and workflows.
Please see detail in [actions-template-sync](https://github.com/AndreasAugustin/actions-template-sync).

## More Info

For more info on modules, you can read through through the
[Zephyr modules page](https://docs.zephyrproject.org/3.5.0/develop/modules.html)
and [ZMK's page on using modules](https://zmk.dev/docs/features/modules).
[Zephyr's west manifest page](https://docs.zephyrproject.org/3.5.0/develop/west/manifest.html#west-manifests)
may also be of use.
