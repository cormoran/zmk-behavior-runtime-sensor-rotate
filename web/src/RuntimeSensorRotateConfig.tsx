/**
 * Runtime Sensor Rotate Configuration Component
 * Provides UI for configuring sensor rotation bindings per layer
 */

import { useContext, useState, useEffect, useCallback, useMemo } from "react";
import {
  ZMKAppContext,
  ZMKCustomSubsystem,
} from "@cormoran/zmk-studio-react-hook";
import {
  Request,
  Response,
  Binding,
  LayerBindings,
  SensorInfo,
} from "./proto/zmk/template/custom";
import { call_rpc } from "@zmkfirmware/zmk-studio-ts-client";
import type { GetBehaviorDetailsResponse } from "@zmkfirmware/zmk-studio-ts-client/behaviors";

export const SUBSYSTEM_IDENTIFIER = "zmk__template";


export function RuntimeSensorRotateConfig() {
  const zmkApp = useContext(ZMKAppContext);
  const [sensors, setSensors] = useState<SensorInfo[]>([]);
  const [sensorIndex, setSensorIndex] = useState<number>(0);
  const [selectedLayer, setSelectedLayer] = useState<number>(0);
  const [allLayerBindings, setAllLayerBindings] = useState<LayerBindings[]>([]);
  const [behaviors, setBehaviors] = useState<GetBehaviorDetailsResponse[]>([]);
  const [isLoading, setIsLoading] = useState(false);
  const [error, setError] = useState<string | null>(null);

  // eslint-disable-next-line react-hooks/exhaustive-deps
  const subsystem = useMemo(() => zmkApp?.findSubsystem(SUBSYSTEM_IDENTIFIER), [zmkApp?.state.customSubsystems]);

  // Load available sensors
  useEffect(() => {
    const loadSensors = async () => {
      if (!zmkApp?.state.connection || !subsystem) return;
      try {
        const service = new ZMKCustomSubsystem(
          zmkApp.state.connection,
          subsystem.index
        );

        const request = Request.create({
          getSensors: {},
        });

        const payload = Request.encode(request).finish();
        const responsePayload = await service.callRPC(payload);

        if (responsePayload) {
          const resp = Response.decode(responsePayload);

          if (resp.getSensors) {
            setSensors(resp.getSensors.sensors || []);
          }
        }
      } catch (err) {
        console.error("Failed to load sensors:", err);
      }
    };

    loadSensors();
  }, [zmkApp?.state.connection, subsystem]);

  // Load available behaviors from ZMK Studio
  useEffect(() => {
    const loadBehaviors = async () => {
      if (!zmkApp?.state.connection) return;
      setIsLoading(true);
      setError(null);

      const conn = zmkApp.state.connection;

        const res = await call_rpc(conn, {
            behaviors: {
                listAllBehaviors: true
            }
        });
        if (res.meta) {
            setIsLoading(false);
            setError(`Error: ${res.meta}`);
            return;
        }
        const behaviorIds = res.behaviors?.listAllBehaviors?.behaviors ?? [];
        const behaviors = await Promise.all(behaviorIds.map(async (b) => {
            const res = await call_rpc(conn, {
                behaviors: {
                    getBehaviorDetails: {
                        behaviorId: b
                    }
                }
            })
            return res.behaviors?.getBehaviorDetails;
        }));
        setBehaviors(behaviors.filter((b) => b !== undefined));
        setIsLoading(false);
    };

    loadBehaviors();
  }, [zmkApp?.state.connection]);

  // Load all layer bindings for the selected sensor
  const loadAllLayerBindings = useCallback(async () => {
    if (!zmkApp || !zmkApp.state.connection || !subsystem) return;
    console.log("Loading layer bindings for sensor index:", sensorIndex);

    setIsLoading(true);
    setError(null);

    try {
      const service = new ZMKCustomSubsystem(
        zmkApp.state.connection,
        subsystem.index
      );

      const request = Request.create({
        getAllLayerBindings: {
          sensorIndex: sensorIndex,
        },
      });

      const payload = Request.encode(request).finish();
      const responsePayload = await service.callRPC(payload);

      if (responsePayload) {
        const resp = Response.decode(responsePayload);
        console.log("Received layer bindings response:", resp);
        if (resp.getAllLayerBindings) {
          setAllLayerBindings(resp.getAllLayerBindings.bindings || []);
        } else if (resp.error) {
          setError(`Error: ${resp.error.message}`);
        }
      }
    } catch (err) {
      console.error("Failed to load layer bindings:", err);
      setError(
        `Failed to load: ${err instanceof Error ? err.message : "Unknown error"}`
      );
    } finally {
      setIsLoading(false);
    }
  }, [zmkApp, subsystem, sensorIndex]);

  // Save bindings for a specific layer
  const saveLayerBindings = useCallback(async (
    layer: number,
    cwBinding: Binding,
    ccwBinding: Binding
  ) => {
    if (!zmkApp.state.connection || !subsystem) return;

    setIsLoading(true);
    setError(null);

    try {
      const service = new ZMKCustomSubsystem(
        zmkApp.state.connection,
        subsystem.index
      );

      const request = Request.create({
        setLayerBindings: {
          sensorIndex: sensorIndex,
          layer: layer,
          cwBinding: cwBinding,
          ccwBinding: ccwBinding,
        },
      });

      const payload = Request.encode(request).finish();
      const responsePayload = await service.callRPC(payload);

      if (responsePayload) {
        const resp = Response.decode(responsePayload);

        if (resp.setLayerBindings?.success) {
          // Reload bindings to show updated values
          await loadAllLayerBindings();
        } else if (resp.error) {
          setError(`Error: ${resp.error.message}`);
        }
      }
    } catch (err) {
      console.error("Failed to save layer bindings:", err);
      setError(
        `Failed to save: ${err instanceof Error ? err.message : "Unknown error"}`
      );
    } finally {
      setIsLoading(false);
    }
  }, [zmkApp?.state.connection, subsystem, sensorIndex, loadAllLayerBindings]);

  if (!zmkApp) return null;

  if (!subsystem) {
    return (
      <section className="card">
        <div className="warning-message">
          <p>
            ⚠️ Subsystem "{SUBSYSTEM_IDENTIFIER}" not found. Make sure your
            firmware includes the runtime-sensor-rotate module.
          </p>
        </div>
      </section>
    );
  }

  return (
    <section className="card">
      <h2>🔄 Runtime Sensor Rotate Configuration</h2>
      <p>
        Configure sensor rotation bindings per layer with persistent storage.
      </p>

      <div className="input-group">
        <label htmlFor="sensor-select">Sensor:</label>
        <select
          id="sensor-select"
          value={sensorIndex}
          onChange={(e) => setSensorIndex(parseInt(e.target.value))}
        >
          {sensors.length > 0 ? (
            sensors.map((sensor) => (
              <option key={sensor.index} value={sensor.index}>
                {sensor.name} (Index {sensor.index})
              </option>
            ))
          ) : (
            <>
              <option value="0">Sensor 0</option>
              <option value="1">Sensor 1</option>
            </>
          )}
        </select>
      </div>

      <button
        className="btn btn-primary"
        disabled={isLoading}
        onClick={loadAllLayerBindings}
      >
        {isLoading ? "⏳ Loading..." : "📥 Load Configuration"}
      </button>

      {error && (
        <div className="error-message">
          <p>🚨 {error}</p>
        </div>
      )}

      {allLayerBindings.length > 0 && (
        <div className="layer-config">
          <h3>Layer Configuration</h3>

          <div className="input-group">
            <label htmlFor="layer-select">Select Layer:</label>
            <select
              id="layer-select"
              value={selectedLayer}
              onChange={(e) => setSelectedLayer(parseInt(e.target.value))}
            >
              {allLayerBindings.map((_, index) => (
                <option key={index} value={index}>
                  Layer {index}
                </option>
              ))}
            </select>
          </div>

          {allLayerBindings[selectedLayer] && (
            <LayerBindingEditor
              layer={selectedLayer}
              bindings={allLayerBindings[selectedLayer]}
              behaviors={behaviors}
              onSave={saveLayerBindings}
              isLoading={isLoading}
            />
          )}
        </div>
      )}
    </section>
  );
}

interface LayerBindingEditorProps {
  layer: number;
  bindings: LayerBindings;
  behaviors: GetBehaviorDetailsResponse[];
  onSave: (layer: number, cwBinding: Binding, ccwBinding: Binding) => void;
  isLoading: boolean;
}

function LayerBindingEditor({
  layer,
  bindings,
  behaviors,
  onSave,
  isLoading,
}: LayerBindingEditorProps) {
  const [cwBehavior, setCwBehavior] = useState(
    bindings.cwBinding?.behavior || ""
  );
  const [cwParam1, setCwParam1] = useState(bindings.cwBinding?.param1 || 0);
  const [cwParam2, setCwParam2] = useState(bindings.cwBinding?.param2 || 0);

  const [ccwBehavior, setCcwBehavior] = useState(
    bindings.ccwBinding?.behavior || ""
  );
  const [ccwParam1, setCcwParam1] = useState(bindings.ccwBinding?.param1 || 0);
  const [ccwParam2, setCcwParam2] = useState(bindings.ccwBinding?.param2 || 0);

  const handleSave = () => {
    const cwBinding: Binding = {
      behavior: cwBehavior,
      param1: cwParam1,
      param2: cwParam2,
    };

    const ccwBinding: Binding = {
      behavior: ccwBehavior,
      param1: ccwParam1,
      param2: ccwParam2,
    };

    onSave(layer, cwBinding, ccwBinding);
  };

  return (
    <div className="binding-editor">
      <h4>Layer {layer} Bindings</h4>

      <div className="binding-group">
        <h5>↻ Clockwise Rotation</h5>
        <div className="input-group">
          <label>Behavior:</label>
          <input
            type="text"
            value={cwBehavior}
            onChange={(e) => setCwBehavior(e.target.value)}
            placeholder="e.g., kp"
            list="behaviors-list"
          />
        </div>
        <div className="input-group">
          <label>Param 1:</label>
          <input
            type="number"
            value={cwParam1}
            onChange={(e) => setCwParam1(parseInt(e.target.value) || 0)}
          />
        </div>
        <div className="input-group">
          <label>Param 2:</label>
          <input
            type="number"
            value={cwParam2}
            onChange={(e) => setCwParam2(parseInt(e.target.value) || 0)}
          />
        </div>
      </div>

      <div className="binding-group">
        <h5>↺ Counter-Clockwise Rotation</h5>
        <div className="input-group">
          <label>Behavior:</label>
          <input
            type="text"
            value={ccwBehavior}
            onChange={(e) => setCcwBehavior(e.target.value)}
            placeholder="e.g., kp"
            list="behaviors-list"
          />
        </div>
        <div className="input-group">
          <label>Param 1:</label>
          <input
            type="number"
            value={ccwParam1}
            onChange={(e) => setCcwParam1(parseInt(e.target.value) || 0)}
          />
        </div>
        <div className="input-group">
          <label>Param 2:</label>
          <input
            type="number"
            value={ccwParam2}
            onChange={(e) => setCcwParam2(parseInt(e.target.value) || 0)}
          />
        </div>
      </div>

      <datalist id="behaviors-list">
        {behaviors.map((b) => (
          <option key={b.id} value={b.id}>
            {b.displayName}
            </option>
        ))}
      </datalist>

      <button
        className="btn btn-primary"
        disabled={isLoading}
        onClick={handleSave}
      >
        {isLoading ? "⏳ Saving..." : "💾 Save Bindings"}
      </button>
    </div>
  );
}
