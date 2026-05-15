"""
Smoke test for Batch E (P0 + P1 from UnrealMCP_API_ExpansionRequest.md).

Runs all 14 new MCP commands end-to-end against a live editor session.
Designed to be re-runnable: each test cleans up after itself via delete_asset.

Usage:
    python test_batch_e_api_expansion.py [--skeleton "/Game/Path/SK.SK"] [--existing-ik-rig "/Game/.../IKR.IKR"]

The script connects directly to the MCP socket at 127.0.0.1:55557 (bypassing the FastMCP
layer) so it's identical to what the LLM tool calls send. That keeps the failure mode
"command wire format works" rather than "FastMCP wrapper works."

Exit code: 0 if every check passes; 1 if anything fails. Failures print the offending
response and a short hint about what the test was verifying.
"""

from __future__ import annotations

import argparse
import json
import socket
import sys
import time
from dataclasses import dataclass, field
from typing import Any, Dict, List, Optional


HOST = "127.0.0.1"
PORT = 55557
TIMEOUT_SECONDS = 15.0

# Default test asset paths. Override via CLI if your project layout differs.
DEFAULT_SKELETON   = "/Engine/EngineMeshes/SkeletalCube.SkeletalCube"  # ships with engine; safe to point at
DEFAULT_PARENT_BP  = "/Game/Blueprints/BP_TestParent.BP_TestParent"    # created by test if missing
TEST_NS            = "/Game/_MCPBatchE"                                 # scratch namespace, all created assets live here


@dataclass
class TestResult:
    name: str
    passed: bool
    detail: str = ""
    response: Optional[Dict[str, Any]] = field(default=None)


def send_command(command_type: str, params: Dict[str, Any]) -> Dict[str, Any]:
    """Send a single command + return parsed JSON. Each command opens a fresh socket
    because the Unreal side closes after each response (mirrors Python/unreal_mcp_server.py)."""
    obj = {"type": command_type, "params": params}
    payload = json.dumps(obj).encode("utf-8")

    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.settimeout(TIMEOUT_SECONDS)
        s.connect((HOST, PORT))
        s.sendall(payload)

        chunks: List[bytes] = []
        deadline = time.monotonic() + TIMEOUT_SECONDS
        while True:
            try:
                chunk = s.recv(8192)
            except socket.timeout:
                if chunks:
                    break
                raise
            if not chunk:
                break
            chunks.append(chunk)
            # Try to parse — Unreal sends JSON in one logical message but may chunk it.
            blob = b"".join(chunks)
            try:
                return json.loads(blob.decode("utf-8"))
            except json.JSONDecodeError:
                if time.monotonic() > deadline:
                    raise RuntimeError(f"Timed out reading response for {command_type}")
                continue

    return json.loads(b"".join(chunks).decode("utf-8"))


def is_success(resp: Dict[str, Any]) -> bool:
    if resp.get("status") == "error":
        return False
    if resp.get("success") is False:
        return False
    # `success` wrapped in `result` (typical when the handler returned a CreateSuccessResponse)
    result = resp.get("result")
    if isinstance(result, dict) and result.get("success") is False:
        return False
    return True


def unwrap(resp: Dict[str, Any]) -> Dict[str, Any]:
    """Return the inner result dict (or the response itself if no wrapping)."""
    if isinstance(resp.get("result"), dict):
        return resp["result"]
    return resp


# ------------------------------------------------------------------------
# Tests
# ------------------------------------------------------------------------

class BatchE:
    def __init__(self, skeleton: str, existing_ik_rig: Optional[str]):
        self.skeleton = skeleton
        self.existing_ik_rig = existing_ik_rig
        self.results: List[TestResult] = []
        self.created_assets: List[str] = []  # for cleanup

    def record(self, r: TestResult):
        self.results.append(r)
        status = "PASS" if r.passed else "FAIL"
        print(f"  [{status}] {r.name}{(': ' + r.detail) if r.detail else ''}")
        if not r.passed and r.response is not None:
            print(f"    response = {json.dumps(r.response, ensure_ascii=False, indent=2)[:500]}")

    # ------------------------------------------------------------ P0.1
    def test_create_anim_blueprint(self) -> Optional[str]:
        path = f"{TEST_NS}/ABP_BatchE_Smoke.ABP_BatchE_Smoke"
        resp = send_command("create_anim_blueprint", {
            "asset_path": path,
            "target_skeleton": self.skeleton,
        })
        ok = is_success(resp) and unwrap(resp).get("asset_path", "").startswith(TEST_NS)
        self.record(TestResult("create_anim_blueprint", ok, response=resp if not ok else None))
        if ok:
            self.created_assets.append(path)
            return unwrap(resp)["asset_path"]
        return None

    # ------------------------------------------------------------ P0.2 trio
    def test_anim_graph_node_trio(self, anim_bp_path: str):
        # add_anim_graph_node — drop a RetargetPoseFromMesh into the AnimGraph.
        add_resp = send_command("add_anim_graph_node", {
            "blueprint_path": anim_bp_path,
            "node_class": "AnimGraphNode_RetargetPoseFromMesh",
            "graph_name": "AnimGraph",
            "node_position": [-400.0, 0.0],
        })
        add_ok = is_success(add_resp) and unwrap(add_resp).get("node_guid")
        self.record(TestResult("add_anim_graph_node", bool(add_ok), response=add_resp if not add_ok else None))
        if not add_ok:
            return
        new_guid = unwrap(add_resp)["node_guid"]

        # set_anim_graph_node_property (B): write a property binding (the headline use case
        # from the requirements doc — bind SourceMeshComponent to AnimInstance var LeaderMeshComponent).
        bind_resp = send_command("set_anim_graph_node_property", {
            "blueprint_path": anim_bp_path,
            "node_guid": new_guid,
            "property_binding": {
                "property_name": "SourceMeshComponent",
                "property_path": ["LeaderMeshComponent"],
            },
        })
        bind_ok = is_success(bind_resp)
        self.record(TestResult("set_anim_graph_node_property[binding_set]", bind_ok, response=bind_resp if not bind_ok else None))

        # set_anim_graph_node_property (C): clear the binding we just added.
        clear_resp = send_command("set_anim_graph_node_property", {
            "blueprint_path": anim_bp_path,
            "node_guid": new_guid,
            "clear_binding": "SourceMeshComponent",
        })
        clear_ok = is_success(clear_resp)
        self.record(TestResult("set_anim_graph_node_property[binding_clear]", clear_ok, response=clear_resp if not clear_ok else None))

        # Find the OutputPose node so connect_anim_graph_nodes has something to wire into.
        info_resp = send_command("get_anim_blueprint_info", {"blueprint_path": anim_bp_path})
        # Note: AnimBlueprintInfo doesn't currently list AnimGraph nodes by GUID, so we use
        # get_blueprint_function_graph instead.
        graph_resp = send_command("get_blueprint_function_graph", {
            "blueprint_path": anim_bp_path,
            "function_name": "AnimGraph",
        })
        # Walk graph_resp for a node whose class contains "Output" — best effort.
        output_guid = None
        nodes = unwrap(graph_resp).get("nodes") or graph_resp.get("nodes") or []
        for n in nodes:
            if isinstance(n, dict) and "Output" in n.get("class", ""):
                output_guid = n.get("guid")
                break

        connect_ok = False
        if output_guid:
            conn_resp = send_command("connect_anim_graph_nodes", {
                "blueprint_path": anim_bp_path,
                "source_node_id": new_guid,
                "source_pin": "Pose",
                "target_node_id": output_guid,
                "target_pin": "Result",
            })
            connect_ok = is_success(conn_resp)
            self.record(TestResult("connect_anim_graph_nodes", connect_ok, response=conn_resp if not connect_ok else None))
        else:
            self.record(TestResult("connect_anim_graph_nodes", False,
                                   detail="couldn't find OutputPose node in AnimGraph"))

    # ------------------------------------------------------------ P0.3
    def test_create_blueprint_from_parent(self):
        # First make sure a parent blueprint exists. We use create_blueprint to make one
        # (so this works even on a fresh project).
        parent_path = DEFAULT_PARENT_BP
        # If it doesn't exist, create.
        info = send_command("get_blueprint_info", {"blueprint_path": parent_path})
        if not is_success(info):
            create_resp = send_command("create_blueprint", {
                "name": "BP_TestParent",
                "parent_class": "Actor",
            })
            if not is_success(create_resp):
                self.record(TestResult("create_blueprint_from_parent_blueprint", False,
                                       detail="failed to create the parent blueprint test fixture",
                                       response=create_resp))
                return
            self.created_assets.append(parent_path)

        child_path = f"{TEST_NS}/BP_BatchE_Child.BP_BatchE_Child"
        child_resp = send_command("create_blueprint_from_parent_blueprint", {
            "parent_blueprint_path": parent_path,
            "new_asset_path": child_path,
        })
        ok = is_success(child_resp)
        self.record(TestResult("create_blueprint_from_parent_blueprint", ok, response=child_resp if not ok else None))
        if ok:
            self.created_assets.append(child_path)

    # ------------------------------------------------------------ P1: add_blueprint_function_graph
    def test_add_function_graph(self):
        # Reuse an existing test blueprint if possible.
        parent_path = DEFAULT_PARENT_BP
        info = send_command("get_blueprint_info", {"blueprint_path": parent_path})
        if not is_success(info):
            self.record(TestResult("add_blueprint_function_graph", False,
                                   detail="prerequisite parent BP missing"))
            return
        resp = send_command("add_blueprint_function_graph", {
            "blueprint_path": parent_path,
            "function_name": f"MCPBatchE_TestFn_{int(time.time())}",
        })
        ok = is_success(resp)
        self.record(TestResult("add_blueprint_function_graph", ok, response=resp if not ok else None))

    # ------------------------------------------------------------ P1: IK Rig patches
    def test_ik_rig_patches(self):
        # If user didn't provide an existing IK Rig, create a fresh one. We can build chains
        # without a real skeletal mesh as long as we don't validate bone existence.
        rig_path = self.existing_ik_rig
        owned_rig = False
        if not rig_path:
            rig_path = f"{TEST_NS}/IKR_BatchE.IKR_BatchE"
            create_resp = send_command("create_ik_rig", {"asset_path": rig_path})
            if not is_success(create_resp):
                self.record(TestResult("ik_rig_patches[bootstrap]", False,
                                       detail="couldn't create scratch IK Rig",
                                       response=create_resp))
                return
            self.created_assets.append(rig_path)
            owned_rig = True

        # Probe with a chain + goal + solver so we have something to mutate.
        send_command("add_ik_rig_retarget_chain", {
            "asset_path": rig_path, "chain_name": "MCPProbeChain",
            "start_bone": "root", "end_bone": "root",
        })
        send_command("add_ik_rig_goal", {
            "asset_path": rig_path, "goal_name": "MCPProbeGoal", "bone_name": "root",
        })
        send_command("add_ik_rig_solver", {
            "asset_path": rig_path, "solver_struct_name": "IKRigFBIKSolver",
        })

        # connect_ik_rig_goal_to_solver — solver 0 is the FBIK we just added (assuming the
        # rig was freshly created; user-supplied rigs may have other indices).
        if owned_rig:
            conn_resp = send_command("connect_ik_rig_goal_to_solver", {
                "asset_path": rig_path, "goal_name": "MCPProbeGoal", "solver_index": 0,
            })
            self.record(TestResult("connect_ik_rig_goal_to_solver",
                                   is_success(conn_resp), response=conn_resp if not is_success(conn_resp) else None))

            # set_ik_rig_solver_field — bump FBIK iteration count.
            field_resp = send_command("set_ik_rig_solver_field", {
                "asset_path": rig_path, "solver_index": 0,
                "field_path": "Iterations", "value": "16",
            })
            self.record(TestResult("set_ik_rig_solver_field",
                                   is_success(field_resp), response=field_resp if not is_success(field_resp) else None))

        # update_ik_rig_chain — rename + re-set end bone.
        update_resp = send_command("update_ik_rig_chain", {
            "asset_path": rig_path, "chain_name": "MCPProbeChain",
            "new_chain_name": "MCPProbeChainRenamed",
        })
        self.record(TestResult("update_ik_rig_chain",
                               is_success(update_resp), response=update_resp if not is_success(update_resp) else None))

        # delete_ik_rig_chain
        del_chain = send_command("delete_ik_rig_chain", {
            "asset_path": rig_path,
            "chain_name": unwrap(update_resp).get("chain_name", "MCPProbeChainRenamed") if is_success(update_resp) else "MCPProbeChainRenamed",
        })
        self.record(TestResult("delete_ik_rig_chain",
                               is_success(del_chain), response=del_chain if not is_success(del_chain) else None))

        # delete_ik_rig_goal
        del_goal = send_command("delete_ik_rig_goal", {
            "asset_path": rig_path, "goal_name": "MCPProbeGoal",
        })
        self.record(TestResult("delete_ik_rig_goal",
                               is_success(del_goal), response=del_goal if not is_success(del_goal) else None))

        # delete_ik_rig_solver (only meaningful if we added one)
        if owned_rig:
            del_solver = send_command("delete_ik_rig_solver", {
                "asset_path": rig_path, "solver_index": 0,
            })
            self.record(TestResult("delete_ik_rig_solver",
                                   is_success(del_solver), response=del_solver if not is_success(del_solver) else None))

    # ------------------------------------------------------------ P1: save_dirty_assets
    def test_save_dirty_assets(self):
        # Save the assets we just created. The handler skips clean packages so this should
        # exercise both "saved" and "skipped" branches.
        resp = send_command("save_dirty_assets", {"asset_paths": self.created_assets})
        ok = is_success(resp)
        result = unwrap(resp) if ok else resp
        detail = f"saved={result.get('saved_count', '?')} skipped={result.get('skipped_count', '?')} failed={result.get('failed_count', '?')}"
        self.record(TestResult("save_dirty_assets", ok, detail=detail, response=resp if not ok else None))

    # ------------------------------------------------------------ P1: delete_asset (cleanup)
    def cleanup(self):
        # Best-effort cleanup of every asset this run created. Reverse order so leaf assets
        # go before parents that they reference.
        for path in reversed(self.created_assets):
            resp = send_command("delete_asset", {"asset_path": path, "force": True})
            ok = is_success(resp)
            self.record(TestResult(f"delete_asset[{path}]", ok, response=resp if not ok else None))


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--skeleton", default=DEFAULT_SKELETON,
                    help="Skeleton asset path for create_anim_blueprint test")
    ap.add_argument("--existing-ik-rig", default=None,
                    help="If provided, IK Rig tests run against this existing rig instead of creating one")
    args = ap.parse_args()

    print(f"Batch E smoke test → MCP @ {HOST}:{PORT}")
    print(f"  skeleton:        {args.skeleton}")
    print(f"  existing IK Rig: {args.existing_ik_rig or '(creating scratch rig)'}")
    print()

    # Cheap connectivity probe so we fail fast.
    try:
        ping = send_command("ping", {})
        if not ping or ping.get("status") == "error":
            print(f"ping failed: {ping}", file=sys.stderr)
            return 1
    except Exception as e:
        print(f"Could not reach MCP socket: {e}", file=sys.stderr)
        return 1

    runner = BatchE(skeleton=args.skeleton, existing_ik_rig=args.existing_ik_rig)

    print("[P0.1] create_anim_blueprint")
    anim_bp = runner.test_create_anim_blueprint()

    if anim_bp:
        print("[P0.2] AnimGraph write trio")
        runner.test_anim_graph_node_trio(anim_bp)
    else:
        print("[P0.2] skipped — no AnimBP")

    print("[P0.3] create_blueprint_from_parent_blueprint")
    runner.test_create_blueprint_from_parent()

    print("[P1] add_blueprint_function_graph")
    runner.test_add_function_graph()

    print("[P1] IK Rig patches")
    runner.test_ik_rig_patches()

    print("[P1] save_dirty_assets")
    runner.test_save_dirty_assets()

    print("[P1] delete_asset (cleanup)")
    runner.cleanup()

    total = len(runner.results)
    failed = sum(1 for r in runner.results if not r.passed)
    print(f"\nResult: {total - failed} / {total} passed.")
    return 0 if failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
