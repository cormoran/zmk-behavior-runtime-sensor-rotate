import { act, render, screen, waitFor } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import {
  createConnectedMockZMKApp,
  ZMKAppProvider,
} from "@cormoran/zmk-studio-react-hook/testing";
import {
  RuntimeSensorRotateConfig,
  SUBSYSTEM_IDENTIFIER,
} from "../src/RuntimeSensorRotateConfig";
import { Response } from "../src/proto/cormoran/rsr/custom";
import { LockState } from "@zmkfirmware/zmk-studio-ts-client/core";

// Mock the ZMK client so we can control call_rpc responses directly: both
// useStudioLockState's initial getLockState query and the cormoran_rsr
// custom-subsystem calls (routed through ZMKCustomSubsystem.callRPC, which
// itself goes through call_rpc) go through this module.
jest.mock("@zmkfirmware/zmk-studio-ts-client", () => ({
  create_rpc_connection: jest.fn(),
  call_rpc: jest.fn(),
  MetaError: class MetaError extends Error {
    condition: number;
    constructor(condition: number) {
      super(`meta error: ${condition}`);
      this.condition = condition;
      Object.setPrototypeOf(this, MetaError.prototype);
    }
  },
}));

const UNLOCK_REQUIRED = 1; // zmk.meta.ErrorConditions.UNLOCK_REQUIRED

describe("RuntimeSensorRotateConfig Component", () => {
  describe("Without ZMKAppContext", () => {
    it("should not render when ZMKAppContext is not provided", () => {
      const { container } = render(<RuntimeSensorRotateConfig />);

      expect(container.firstChild).toBeNull();
    });
  });

  describe("Without Subsystem", () => {
    it("should show warning when subsystem is not found", () => {
      const mockZMKApp = createConnectedMockZMKApp({ subsystems: [] });

      render(
        <ZMKAppProvider value={mockZMKApp}>
          <RuntimeSensorRotateConfig />
        </ZMKAppProvider>
      );

      expect(
        screen.getByText(/Subsystem "cormoran_rsr" not found/i)
      ).toBeInTheDocument();
    });
  });

  describe("Unlock flow", () => {
    // eslint-disable-next-line @typescript-eslint/no-require-imports
    const zmkClient = require("@zmkfirmware/zmk-studio-ts-client");

    beforeEach(() => {
      jest.clearAllMocks();
    });

    function mockCallRpc({
      lockState,
      customResult,
    }: {
      lockState: LockState;
      customResult: "unlock-required" | "success";
    }) {
      zmkClient.call_rpc.mockImplementation(
        (
          _connection: unknown,
          req: {
            core?: { getLockState?: boolean };
            custom?: { call?: unknown };
            behaviors?: { listAllBehaviors?: boolean };
          }
        ) => {
          if (req.core?.getLockState) {
            return Promise.resolve({ core: { getLockState: lockState } });
          }
          if (req.behaviors?.listAllBehaviors) {
            return Promise.resolve({
              behaviors: { listAllBehaviors: { behaviors: [] } },
            });
          }
          if (req.custom?.call) {
            if (customResult === "unlock-required") {
              return Promise.reject(new zmkClient.MetaError(UNLOCK_REQUIRED));
            }
            const payload = Response.encode(
              Response.create({
                getAllLayerBindings: { bindings: [{ layer: 0 }] },
              })
            ).finish();
            return Promise.resolve({ custom: { call: { payload } } });
          }
          return Promise.reject(new Error("unexpected call_rpc request"));
        }
      );
    }

    it("shows the unlock prompt when a secured RPC call is rejected with UNLOCK_REQUIRED", async () => {
      mockCallRpc({
        lockState: LockState.ZMK_STUDIO_CORE_LOCK_STATE_UNLOCKED,
        customResult: "unlock-required",
      });

      const mockZMKApp = createConnectedMockZMKApp({
        subsystems: [SUBSYSTEM_IDENTIFIER],
      });

      render(
        <ZMKAppProvider value={mockZMKApp}>
          <RuntimeSensorRotateConfig />
        </ZMKAppProvider>
      );

      const user = userEvent.setup();
      await waitFor(() => {
        expect(screen.getByText(/Load Configuration/i)).not.toBeDisabled();
      });
      await user.click(screen.getByText(/Load Configuration/i));

      await waitFor(() => {
        expect(screen.getByText(/ZMK Studio is locked/i)).toBeInTheDocument();
      });
      expect(screen.getByText("Retry")).toBeInTheDocument();
    });

    it("auto-retries and loads bindings once a lockStateChanged notification reports unlocked", async () => {
      mockCallRpc({
        lockState: LockState.ZMK_STUDIO_CORE_LOCK_STATE_UNLOCKED,
        customResult: "unlock-required",
      });

      let coreCallback:
        | ((notification: { lockStateChanged?: LockState }) => void)
        | undefined;
      const mockZMKApp = createConnectedMockZMKApp({
        subsystems: [SUBSYSTEM_IDENTIFIER],
      });
      mockZMKApp.onNotification = jest.fn((subscription) => {
        if (subscription.type === "core") {
          coreCallback = subscription.callback;
        }
        return () => {};
      });

      render(
        <ZMKAppProvider value={mockZMKApp}>
          <RuntimeSensorRotateConfig />
        </ZMKAppProvider>
      );

      const user = userEvent.setup();
      await waitFor(() => {
        expect(screen.getByText(/Load Configuration/i)).not.toBeDisabled();
      });
      await user.click(screen.getByText(/Load Configuration/i));

      await waitFor(() => {
        expect(screen.getByText("Retry")).toBeInTheDocument();
      });

      mockCallRpc({
        lockState: LockState.ZMK_STUDIO_CORE_LOCK_STATE_UNLOCKED,
        customResult: "success",
      });

      expect(coreCallback).toBeDefined();
      // The auto-retry effect only fires on an actual locked->unlocked
      // transition -- simulate the device confirming locked, then unlocked
      // (e.g. after the user presses &studio_unlock).
      await act(async () => {
        coreCallback?.({
          lockStateChanged: LockState.ZMK_STUDIO_CORE_LOCK_STATE_LOCKED,
        });
      });
      await act(async () => {
        coreCallback?.({
          lockStateChanged: LockState.ZMK_STUDIO_CORE_LOCK_STATE_UNLOCKED,
        });
      });

      await waitFor(() => {
        expect(screen.queryByText("Retry")).not.toBeInTheDocument();
      });
      expect(screen.getByText(/Layer Configuration/i)).toBeInTheDocument();
    });

    it("retries manually via the Retry button", async () => {
      mockCallRpc({
        lockState: LockState.ZMK_STUDIO_CORE_LOCK_STATE_UNLOCKED,
        customResult: "unlock-required",
      });

      const mockZMKApp = createConnectedMockZMKApp({
        subsystems: [SUBSYSTEM_IDENTIFIER],
      });

      render(
        <ZMKAppProvider value={mockZMKApp}>
          <RuntimeSensorRotateConfig />
        </ZMKAppProvider>
      );

      const user = userEvent.setup();
      await waitFor(() => {
        expect(screen.getByText(/Load Configuration/i)).not.toBeDisabled();
      });
      await user.click(screen.getByText(/Load Configuration/i));

      await waitFor(() => {
        expect(screen.getByText("Retry")).toBeInTheDocument();
      });

      mockCallRpc({
        lockState: LockState.ZMK_STUDIO_CORE_LOCK_STATE_UNLOCKED,
        customResult: "success",
      });

      await user.click(screen.getByText("Retry"));

      await waitFor(() => {
        expect(screen.getByText(/Layer Configuration/i)).toBeInTheDocument();
      });
    });
  });
});
