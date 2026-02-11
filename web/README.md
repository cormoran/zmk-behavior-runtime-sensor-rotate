# Runtime Sensor Rotate - Web UI

This is a web application for configuring ZMK runtime sensor rotation bindings
via a custom Studio RPC subsystem.

## Features

- **Device Connection**: Connect to ZMK devices via Bluetooth (GATT) or Serial
- **Sensor Configuration**: Configure sensor rotation bindings per layer
- **Per-Sensor Setup**: Configure multiple sensors independently
- **Behavior Selection**: Select from available ZMK behaviors via dropdown
- **Live Updates**: Changes are saved to persistent storage immediately
- **React + TypeScript**: Modern web development with Vite for fast builds
- **react-zmk-studio**: Uses the `@cormoran/zmk-studio-react-hook` library for
  simplified ZMK integration

## Quick Start

```bash
# Install dependencies
npm install

# Generate TypeScript types from proto
npm run generate

# Run development server
npm run dev

# Build for production
npm run build

# Run tests
npm test
```

## Project Structure

```
src/
├── main.tsx                          # React entry point
├── App.tsx                           # Main application with connection UI
├── RuntimeSensorRotateConfig.tsx     # Sensor rotation configuration component
├── App.css                           # Styles
└── proto/                            # Generated protobuf TypeScript types
    └── zmk/template/
        └── custom.ts                 # Generated from custom.proto

test/
└── App.spec.tsx                      # Tests for App component
```

## How It Works

### 1. Protocol Definition

The protobuf schema is defined in `../proto/cormoran/rsr/custom.proto`:

```proto
message Request {
    oneof request_type {
        SetLayerCwBindingRequest set_layer_cw_binding = 1;
        SetLayerCcwBindingRequest set_layer_ccw_binding = 2;
        GetAllLayerBindingsRequest get_all_layer_bindings = 3;
        GetSensorsRequest get_sensors = 4;
    }
}

message Response {
    oneof response_type {
        ErrorResponse error = 1;
        SetLayerCwBindingResponse set_layer_cw_binding = 2;
        SetLayerCcwBindingResponse set_layer_ccw_binding = 3;
        GetAllLayerBindingsResponse get_all_layer_bindings = 4;
        GetSensorsResponse get_sensors = 5;
    }
}
```

**Key message types:**

- `SetLayerCwBindingRequest/Response`: Set clockwise binding for a sensor/layer
- `SetLayerCcwBindingRequest/Response`: Set counter-clockwise binding for a sensor/layer
- `GetAllLayerBindingsRequest/Response`: Get all bindings for a sensor
- `GetSensorsRequest/Response`: List available sensors

### 2. Code Generation

TypeScript types are generated using `ts-proto`:

```bash
npm run generate
```

This runs `buf generate` which uses the configuration in `buf.gen.yaml`.

### 3. Using react-zmk-studio

The app uses the `@cormoran/zmk-studio-react-hook` library:

```typescript
import { useZMKApp, ZMKCustomSubsystem } from "@cormoran/zmk-studio-react-hook";

// Connect to device
const { state, connect, findSubsystem, isConnected } = useZMKApp();

// Find your subsystem
const subsystem = findSubsystem("cormoran_rsr");

// Create service and make RPC calls
const service = new ZMKCustomSubsystem(state.connection, subsystem.index);
const response = await service.callRPC(payload);
```

## Testing

This template includes Jest tests as a reference implementation for template users.

### Running Tests

```bash
# Run all tests
npm test

# Run tests in watch mode
npm run test:watch

# Run tests with coverage
npm run test:coverage
```

### Test Structure

The tests demonstrate how to use the `react-zmk-studio` test helpers:

- **App.spec.tsx**: Tests for the main application component and sensor rotation configuration UI

### Writing Tests

Use the test helpers from `@cormoran/zmk-studio-react-hook/testing`:

```typescript
import {
  createConnectedMockZMKApp,
  ZMKAppProvider,
} from "@cormoran/zmk-studio-react-hook/testing";

// Create a mock connected device with subsystems
const mockZMKApp = createConnectedMockZMKApp({
  deviceName: "Test Device",
  subsystems: ["cormoran_rsr"],
});

// Wrap your component with the provider
render(
  <ZMKAppProvider value={mockZMKApp}>
    <YourComponent />
  </ZMKAppProvider>
);
```

See the test files in `./test/` for complete examples.

## Usage

### Connecting to Device

1. Build and serve the web UI:

   ```bash
   npm install
   npm run build
   # Open dist/index.html or deploy to a web server
   ```

2. Open the web UI in a browser that supports WebSerial/WebBluetooth (Chrome, Edge)

3. Click "Connect Serial" or "Connect BLE" to connect to your keyboard

### Configuring Bindings

1. **Select Sensor**: Choose the sensor index (0, 1, etc.) from the dropdown
2. **Load Configuration**: Click "Load Configuration" to fetch current bindings
3. **Select Layer**: Choose the layer you want to configure
4. **Configure Clockwise Binding**:
   - Select behavior from dropdown (e.g., "kp" for key press)
   - Enter param1 (e.g., key code)
   - Enter param2 if needed
   - Specify tap duration in ms
5. **Configure Counter-Clockwise Binding**: Same as clockwise
6. **Save**: Click "Save CW Binding" or "Save CCW Binding" to persist changes

The configuration is automatically saved to the keyboard's persistent storage.

## Dependencies

- **@cormoran/zmk-studio-react-hook**: React hooks for ZMK Studio (includes
  connection management and RPC utilities)
- **@zmkfirmware/zmk-studio-ts-client**: Patched ZMK Studio TypeScript client
  with custom RPC support
- **ts-proto**: Protocol buffers code generator for TypeScript
- **React 19**: Modern React with hooks
- **Vite**: Fast build tool and dev server

### Development Dependencies

- **Jest**: Testing framework
- **@testing-library/react**: React testing utilities
- **ts-jest**: TypeScript support for Jest
- **identity-obj-proxy**: CSS mock for testing

## Development Notes

- The `react-zmk-studio` directory contains a copy of the library for
  reference - it's automatically built and linked via `package.json`
- Proto generation uses `buf` and `ts-proto` for clean TypeScript types
- Connection state is managed by the `useZMKApp` hook from react-zmk-studio
- RPC calls are made through `ZMKCustomSubsystem` service class

## See Also

- [design.md](./design.md) - Detailed frontend architecture documentation
- [react-zmk-studio/README.md](./react-zmk-studio/README.md) - react-zmk-studio
  library documentation
