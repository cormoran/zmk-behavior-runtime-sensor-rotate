/**
 * ZMK Module Template - Main Application
 * Demonstrates custom RPC communication with a ZMK device
 */

import "./App.css";
import { connect as gattConnect } from "@zmkfirmware/zmk-studio-ts-client/transport/gatt";
import {
  ZMKConnection,
  isWebSerialSupported,
  isWebBluetoothSupported,
  connectSerial,
} from "@cormoran/zmk-studio-react-hook";
import { RuntimeSensorRotateConfig } from "./RuntimeSensorRotateConfig";

// Custom subsystem identifier - must match firmware registration
export const SUBSYSTEM_IDENTIFIER = "cormoran_rsr";

// Credits the template this module was scaffolded from -- kept distinct from
// this module's own repo so it survives regardless of where the module ends
// up living.
export const TEMPLATE_CREDIT_REPO = "cormoran/zmk-module-template";

function App() {
  return (
    <div className="app">
      <header className="app-header">
        <h1>🔧 ZMK Runtime Sensor Rotate</h1>
        <p>Configure sensor rotation bindings via ZMK Studio</p>
      </header>

      <ZMKConnection
        autoReconnect
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
              <>
                <div className="connect-buttons">
                  {isWebSerialSupported() && (
                    <button
                      className="btn btn-primary"
                      onClick={() => connect(connectSerial)}
                    >
                      🔌 Connect USB
                    </button>
                  )}
                  {isWebBluetoothSupported() && (
                    <button
                      className="btn btn-primary"
                      onClick={() => connect(gattConnect)}
                    >
                      📶 Connect Bluetooth
                    </button>
                  )}
                  {!isWebSerialSupported() && !isWebBluetoothSupported() && (
                    <div className="warning-message">
                      <p>
                        ⚠️ Web Serial and Web Bluetooth are unavailable here.
                        Use a Chromium-based browser (Chrome, Edge, ...) over
                        HTTPS or localhost to connect to your keyboard.
                      </p>
                    </div>
                  )}
                </div>
                {isWebBluetoothSupported() && (
                  <p className="hint-message">
                    📶 Not showing up? Some firmware only advertises the Studio
                    Bluetooth service once unlocked — press the unlock key (
                    <code>&amp;studio_unlock</code> behavior) on your keyboard,
                    then try connecting again.
                  </p>
                )}
              </>
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
        <p className="template-credit">
          Built from{" "}
          <a
            href={`https://github.com/${TEMPLATE_CREDIT_REPO}`}
            target="_blank"
            rel="noreferrer"
          >
            {TEMPLATE_CREDIT_REPO}
          </a>{" "}
          - AI ready ZMK module template by{" "}
          <a
            href="https://github.com/cormoran"
            target="_blank"
            rel="noreferrer"
          >
            @cormoran
          </a>
        </p>
      </footer>
    </div>
  );
}

export default App;
