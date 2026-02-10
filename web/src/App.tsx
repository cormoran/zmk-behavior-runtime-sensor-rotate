/**
 * ZMK Module Template - Main Application
 * Demonstrates custom RPC communication with a ZMK device
 */

import "./App.css";
import { connect as serial_connect } from "@zmkfirmware/zmk-studio-ts-client/transport/serial";
import { ZMKConnection } from "@cormoran/zmk-studio-react-hook";
import { RuntimeSensorRotateConfig } from "./RuntimeSensorRotateConfig";

// Custom subsystem identifier - must match firmware registration
export const SUBSYSTEM_IDENTIFIER = "cormoran_rsr";

function App() {
  return (
    <div className="app">
      <header className="app-header">
        <h1>🔧 ZMK Runtime Sensor Rotate</h1>
        <p>Configure sensor rotation bindings via ZMK Studio</p>
      </header>

      <ZMKConnection
        renderDisconnected={({ connect, isLoading, error }) => (
          <section className="card">
            <h2>Device Connection</h2>
            {isLoading && <p>⏳ Connecting...</p>}
            {error && (
              <div className="error-message">
                <p>🚨 {error}</p>
              </div>
            )}
            {!isLoading && (
              <button
                className="btn btn-primary"
                onClick={() => connect(serial_connect)}
              >
                🔌 Connect Serial
              </button>
            )}
          </section>
        )}
        renderConnected={({ disconnect, deviceName }) => (
          <>
            <section className="card">
              <h2>Device Connection</h2>
              <div className="device-info">
                <h3>✅ Connected to: {deviceName}</h3>
              </div>
              <button className="btn btn-secondary" onClick={disconnect}>
                Disconnect
              </button>
            </section>

            <RuntimeSensorRotateConfig />
          </>
        )}
      />

      <footer className="app-footer">
        <p>
          <strong>Runtime Sensor Rotate Module</strong> - Configure sensor
          bindings per layer
        </p>
      </footer>
    </div>
  );
}

export default App;
